// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include "lima_device.h"
#include "lima_vm.h"
#include "lima_gem.h"
#include "lima_regs.h"

struct lima_bo_va {
	struct list_head list;
	unsigned int ref_count;

	struct drm_mm_node node;

	struct lima_vm *vm;
};

#define LIMA_VM_PD_SHIFT 22
#define LIMA_VM_PT_SHIFT 12
#define LIMA_VM_PB_SHIFT (LIMA_VM_PD_SHIFT + LIMA_VM_NUM_PT_PER_BT_SHIFT)
#define LIMA_VM_BT_SHIFT LIMA_VM_PT_SHIFT

#define LIMA_VM_PT_MASK ((1 << LIMA_VM_PD_SHIFT) - 1)
#define LIMA_VM_BT_MASK ((1 << LIMA_VM_PB_SHIFT) - 1)

#define LIMA_PDE(va) (va >> LIMA_VM_PD_SHIFT)
#define LIMA_PTE(va) ((va & LIMA_VM_PT_MASK) >> LIMA_VM_PT_SHIFT)
#define LIMA_PBE(va) (va >> LIMA_VM_PB_SHIFT)
#define LIMA_BTE(va) ((va & LIMA_VM_BT_MASK) >> LIMA_VM_BT_SHIFT)


static void lima_vm_unmap_range(struct lima_vm *vm, u32 start, u32 end)
{
	/*
	 * Mali-400 exposes the entire 32-bit VM address space. A mapping
	 * can end at 0xffffffff, while incrementing a u32 last-page
	 * address 0xfffff000 by PAGE_SIZE wraps back to zero. The old
	 * addr <= end loop would then never terminate and could clear
	 * unrelated page tables (or dereference an absent BT).
	 *
	 * Keep the range arguments in the original inclusive u32 form,
	 * but advance the cursor in u64 so crossing the 4 GiB boundary
	 * terminates without wrapping into address zero.
	 */
	u64 addr;

	for (addr = start; addr <= end; addr += LIMA_PAGE_SIZE) {
		u32 pbe = LIMA_PBE(addr);
		u32 bte = LIMA_BTE(addr);

		vm->bts[pbe].cpu[bte] = 0;
	}
}

/*
 * Prepare a PTE storage block without modifying any existing PTE.
 * The GP heap SGT replacement must finish every potentially failing
 * page-table allocation before it rewrites OLD working DMA addresses.
 */
static int lima_vm_prepare_pte(struct lima_vm *vm, u32 va)
{
	u32 pbe = LIMA_PBE(va);

	if (!vm->bts[pbe].cpu) {
		dma_addr_t pts;
		u32 *pd;
		int j;

		vm->bts[pbe].cpu = dma_alloc_wc(
			vm->dev->dev, LIMA_PAGE_SIZE << LIMA_VM_NUM_PT_PER_BT_SHIFT,
			&vm->bts[pbe].dma, GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
		if (!vm->bts[pbe].cpu)
			return -ENOMEM;

		pts = vm->bts[pbe].dma;
		pd = vm->pd.cpu + (pbe << LIMA_VM_NUM_PT_PER_BT_SHIFT);
		for (j = 0; j < LIMA_VM_NUM_PT_PER_BT; j++) {
			pd[j] = pts | LIMA_VM_FLAG_PRESENT;
			pts += LIMA_PAGE_SIZE;
		}
	}

	return 0;
}

static int lima_vm_map_page(struct lima_vm *vm, dma_addr_t pa, u32 va)
{
	int ret = lima_vm_prepare_pte(vm, va);

	if (ret)
		return ret;

	vm->bts[LIMA_PBE(va)].cpu[LIMA_BTE(va)] =
		pa | LIMA_VM_FLAGS_CACHE;
	return 0;
}

/*
 * drm_mm stores full 64-bit node offsets and can place Mali-400 buffers
 * adjacent to the 4 GiB GPU VA limit. Old signed-int per-page offsets
 * could overflow at 2 GiB, and a shifted int pageoff could wrap BEFORE
 * a map-table index was computed.
 *
 * Validate the byte offset against the ACTUAL reserved drm_mm node, and
 * verify that the final page VA can still be represented by hardware.
 * The caller checks this BEFORE writing any PTE for that page.
 */
static int lima_vm_checked_page_va(const struct drm_mm_node *node,
				    u64 byte_offset, u32 *va)
{
	if (byte_offset >= node->size ||
	    node->start > U32_MAX ||
	    byte_offset > (u64)U32_MAX - node->start)
		return -EINVAL;

	*va = (u32)(node->start + byte_offset);
	return 0;
}

static struct lima_bo_va *
lima_vm_bo_find(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va, *ret = NULL;

	list_for_each_entry(bo_va, &bo->va, list) {
		if (bo_va->vm == vm) {
			ret = bo_va;
			break;
		}
	}

	return ret;
}

/*
 * A foreign PRIME dma-buf may supply an SGT that covers fewer (or more)
 * DMA pages than the imported GEM object's advertised size. Without
 * verifying the coverage, bo_add could publish a VA for an incompletely
 * backed object, leaving unmapped PTEs that a GP/PP job may access.
 *
 * GP heaps reserve the whole BO size but only map heap_size pages.
 * Count the currently resident heap prefix rather than the maximum BO.
 *
 * Reused for new BO VA mapping and staged GP heap SGT replacement;
 * callers reject invalid DMA BEFORE drm_mm_insert_node or GPU PTE writes.
 */
int lima_vm_check_sgt_dma(struct sg_table *sgt, size_t size)
{
	struct sg_dma_page_iter sg_iter;
	struct scatterlist *sg;
	size_t expected, actual = 0;
	unsigned int i;

	if (!sgt || !size || !IS_ALIGNED(size, PAGE_SIZE))
		return -EINVAL;

	/*
	 * The DMA page iterator determines its page count from
	 * PAGE_ALIGN(sg->offset + sg_dma_len(sg)), but obtains each PTE
	 * address from sg_dma_address(sg) + page_offset * PAGE_SIZE.
	 *
	 * A malformed PRIME attachment could appear to cover the right
	 * number of pages yet supply an unaligned physical DMA address,
	 * a partial final page or an address above the 32-bit Mali MMU
	 * aperture. The GPU PTE stores only 32 bits. Reject such inputs
	 * BEFORE publishing a new GPU virtual address or any PTE.
	 */
	for_each_sgtable_dma_sg(sgt, sg, i) {
		u64 dma = sg_dma_address(sg);
		u64 len = sg_dma_len(sg);

		if (sg->offset || !len ||
		    !IS_ALIGNED(dma, PAGE_SIZE) ||
		    !IS_ALIGNED(len, PAGE_SIZE) ||
		    dma >= (u64)U32_MAX + 1 ||
		    len > ((u64)U32_MAX + 1) - dma)
			return -EINVAL;
	}

	expected = size >> PAGE_SHIFT;
	for_each_sgtable_dma_page(sgt, &sg_iter, 0) {
		if (actual == expected)
			return -EINVAL;
		actual++;
	}

	return actual == expected ? 0 : -EINVAL;
}

static int lima_vm_check_bo_sgt_pages(struct lima_bo *bo)
{
	lockdep_assert_held(&bo->lock);
	return lima_vm_check_sgt_dma(bo->base.sgt,
		bo->heap_size ? bo->heap_size : lima_bo_size(bo));
}

int lima_vm_bo_add(struct lima_vm *vm, struct lima_bo *bo, bool create)
{
	struct lima_bo_va *bo_va;
	struct sg_dma_page_iter sg_iter;
	u64 offset = 0;
	int err;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	if (bo_va) {
		bo_va->ref_count++;
		mutex_unlock(&bo->lock);
		return 0;
	}

	/* should not create new bo_va if not asked by caller */
	if (!create) {
		mutex_unlock(&bo->lock);
		return -ENOENT;
	}

	/*
	 * For imported PRIME, reject a truncated or oversized mapped SGT
	 * before reserving GPU VA or modifying page tables. Native GP heap
	 * creation intentionally maps only the allocated heap prefix.
	 */
	err = lima_vm_check_bo_sgt_pages(bo);
	if (err)
		goto err_out0;

	bo_va = kzalloc_obj(*bo_va);
	if (!bo_va) {
		err = -ENOMEM;
		goto err_out0;
	}

	bo_va->vm = vm;
	bo_va->ref_count = 1;

	mutex_lock(&vm->lock);

	err = drm_mm_insert_node(&vm->mm, &bo_va->node, lima_bo_size(bo));
	if (err)
		goto err_out1;

	for_each_sgtable_dma_page(bo->base.sgt, &sg_iter, 0) {
		u32 va;

		err = lima_vm_checked_page_va(&bo_va->node, offset, &va);
		if (err)
			goto err_out2;

		err = lima_vm_map_page(vm, sg_page_iter_dma_address(&sg_iter),
				       va);
		if (err)
			goto err_out2;

		offset += PAGE_SIZE;
	}

	mutex_unlock(&vm->lock);

	list_add_tail(&bo_va->list, &bo->va);

	mutex_unlock(&bo->lock);
	return 0;

err_out2:
	if (offset)
		lima_vm_unmap_range(vm, (u32)bo_va->node.start,
				    (u32)(bo_va->node.start + offset - 1));
	drm_mm_remove_node(&bo_va->node);
err_out1:
	mutex_unlock(&vm->lock);
	kfree(bo_va);
err_out0:
	mutex_unlock(&bo->lock);
	return err;
}

void lima_vm_bo_del(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;
	u32 size;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	if (--bo_va->ref_count > 0) {
		mutex_unlock(&bo->lock);
		return;
	}

	mutex_lock(&vm->lock);

	size = bo->heap_size ? bo->heap_size : bo_va->node.size;
	lima_vm_unmap_range(vm, bo_va->node.start,
			    bo_va->node.start + size - 1);

	drm_mm_remove_node(&bo_va->node);

	mutex_unlock(&vm->lock);

	list_del(&bo_va->list);

	mutex_unlock(&bo->lock);

	kfree(bo_va);
}

u32 lima_vm_get_va(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;
	u32 ret;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	ret = bo_va->node.start;

	mutex_unlock(&bo->lock);

	return ret;
}

struct lima_vm *lima_vm_create(struct lima_device *dev)
{
	struct lima_vm *vm;

	vm = kzalloc_obj(*vm);
	if (!vm)
		return NULL;

	vm->dev = dev;
	mutex_init(&vm->lock);
	kref_init(&vm->refcount);

	vm->pd.cpu = dma_alloc_wc(dev->dev, LIMA_PAGE_SIZE, &vm->pd.dma,
				  GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
	if (!vm->pd.cpu)
		goto err_out0;

	if (dev->dlbu_cpu) {
		int err = lima_vm_map_page(
			vm, dev->dlbu_dma, LIMA_VA_RESERVE_DLBU);
		if (err)
			goto err_out1;
	}

	drm_mm_init(&vm->mm, dev->va_start, dev->va_end - dev->va_start);

	return vm;

err_out1:
	dma_free_wc(dev->dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);
err_out0:
	kfree(vm);
	return NULL;
}

void lima_vm_release(struct kref *kref)
{
	struct lima_vm *vm = container_of(kref, struct lima_vm, refcount);
	int i;

	drm_mm_takedown(&vm->mm);

	for (i = 0; i < LIMA_VM_NUM_BT; i++) {
		if (vm->bts[i].cpu)
			dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE << LIMA_VM_NUM_PT_PER_BT_SHIFT,
				    vm->bts[i].cpu, vm->bts[i].dma);
	}

	if (vm->pd.cpu)
		dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);

	kfree(vm);
}

void lima_vm_print(struct lima_vm *vm)
{
	int i, j, k;
	u32 *pd, *pt;

	if (!vm->pd.cpu)
		return;

	pd = vm->pd.cpu;
	for (i = 0; i < LIMA_VM_NUM_BT; i++) {
		if (!vm->bts[i].cpu)
			continue;

		pt = vm->bts[i].cpu;
		for (j = 0; j < LIMA_VM_NUM_PT_PER_BT; j++) {
			int idx = (i << LIMA_VM_NUM_PT_PER_BT_SHIFT) + j;

			printk(KERN_INFO "lima vm pd %03x:%08x\n", idx, pd[idx]);

			for (k = 0; k < LIMA_PAGE_ENT_NUM; k++) {
				u32 pte = *pt++;

				if (pte)
					printk(KERN_INFO "  pt %03x:%08x\n", k, pte);
			}
		}
	}
}

/*
 * A GEM object can have one VA record in each separate Lima context VM.
 * Updating only the faulting GP VM and then unmapping old DMA would
 * invalidate every foreign VM's PTEs. Reject a cross-VM heap grow
 * conservatively without altering old working DMA or either VM.
 *
 * The BO lock also prevents another context attaching a VA while its
 * GP owner performs an exclusive replacement of the DMA scatterlist.
 */
bool lima_vm_bo_exclusive_locked(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;
	bool found = false;

	lockdep_assert_held(&bo->lock);

	list_for_each_entry(bo_va, &bo->va, list) {
		if (bo_va->vm != vm)
			return false;
		found = true;
	}

	return found;
}

int lima_vm_map_bo_locked(struct lima_vm *vm, struct lima_bo *bo,
			  int pageoff, struct sg_table *sgt)
{
	struct lima_bo_va *bo_va;
	struct sg_dma_page_iter sg_iter;
	u64 offset = 0, first_byte;
	int err;

	lockdep_assert_held(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	if (!bo_va) {
		err = -ENOENT;
		goto err_out0;
	}

	/*
	 * Cast pageoff BEFORE shifting: a signed 32-bit page offset may
	 * represent more than 2 GiB of bytes, and the shift itself must
	 * not overflow or produce a negative GPU address. Do not modify
	 * any GPU PTE on invalid/unrepresentable input.
	 */
	if (pageoff < 0 || (u64)pageoff >=
	    (bo_va->node.size >> PAGE_SHIFT)) {
		err = -EINVAL;
		goto err_out0;
	}
	first_byte = (u64)pageoff << PAGE_SHIFT;

	mutex_lock(&vm->lock);

	/*
	 * Phase 1: preallocate every PTE table needed for the staged SGT.
	 * dma_alloc_wc() may fail with -ENOMEM. Until ALL table allocations
	 * succeed, do not overwrite even one PTE referencing the OLD DMA.
	 * Empty newly allocated page tables may remain until VM teardown on
	 * allocation failure, but old PTEs and SGT remain valid.
	 */
	for_each_sgtable_dma_page(sgt, &sg_iter, pageoff) {
		u32 va;

		err = lima_vm_checked_page_va(&bo_va->node,
					       first_byte + offset, &va);
		if (err)
			goto err_unlock_vm;

		err = lima_vm_prepare_pte(vm, va);
		if (err)
			goto err_unlock_vm;
		offset += PAGE_SIZE;
	}

	/*
	 * Phase 2: all destination PTE tables now exist. Programming PTEs
	 * is infallible under vm->lock: no partially committed PTE range
	 * that needs a fallible restore after a staged DMA-map failure.
	 */
	offset = 0;
	for_each_sgtable_dma_page(sgt, &sg_iter, pageoff) {
		/*
		 * The exact same SGT and byte offsets already passed every
		 * range check and allocated every BT in Phase 1.
		 */
		u32 va = (u32)(bo_va->node.start + first_byte + offset);

		vm->bts[LIMA_PBE(va)].cpu[LIMA_BTE(va)] =
			sg_page_iter_dma_address(&sg_iter) | LIMA_VM_FLAGS_CACHE;
		offset += PAGE_SIZE;
	}

	mutex_unlock(&vm->lock);
	return 0;

err_unlock_vm:
	mutex_unlock(&vm->lock);
err_out0:
	return err;
}
