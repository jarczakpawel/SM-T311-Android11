// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/mm.h>
#include <linux/iosys-map.h>
#include <linux/sync_file.h>
#include <linux/pagemap.h>
#include <linux/shmem_fs.h>
#include <linux/dma-mapping.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>

#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>

#include <drm/lima_drm.h>

#include "lima_drv.h"
#include "lima_gem.h"
#include "lima_vm.h"
#include "lima_mmu.h"
#include "lima_sched.h"

/*
 * GP page-fault heap growth is called with vm != NULL from the recover
 * worker while holding pipe->recovery_lock. The GP timeout worker must
 * acquire recovery_lock before resetting the engine, so a blocking
 * dma_resv_lock() here can keep the timeout stuck behind an unrelated
 * userspace GEM submit holding this BO's reservation lock.
 *
 * Try once in the GPU recovery path. -EBUSY is returned BEFORE any
 * allocation/mapping modification, and the existing caller forwards
 * this failure to drm_sched_fault() for GP/PP TDR handling. Initial
 * heap creation (vm == NULL) keeps the normal blocking lock semantics.
 */
static int lima_heap_lock_reservation(struct dma_resv *resv, bool recovery)
{
	if (recovery)
		return dma_resv_trylock(resv) ? 0 : -EBUSY;

	return dma_resv_lock(resv, NULL);
}

/*
 * A GP page fault does not prove that a PP job or another device has
 * stopped accessing this BO. We must not retire the old DMA scatterlist
 * while any OTHER BO reservation fence is unsignaled. The current GP
 * job's own finished fence is expected to remain unsignaled at the fault,
 * and is the sole explicitly permitted exception.
 *
 * Call only with the BO reservation held. It also serializes this check
 * and SGT replacement against dma_resv_add_fence in a new GEM submit.
 */
static bool lima_heap_other_dma_idle_locked(struct dma_resv *resv,
					    const struct dma_fence *gp_fence)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_assert_held(resv);
	if (!gp_fence)
		return false;

	dma_resv_for_each_fence(&cursor, resv, DMA_RESV_USAGE_READ, fence) {
		if (fence != gp_fence && !dma_fence_is_signaled(fence))
			return false;
	}

	return true;
}

int lima_heap_alloc(struct lima_bo *bo, struct lima_vm *vm,
		    const struct dma_fence *gp_fence)
{
	struct page **pages;
	struct address_space *mapping = bo->base.base.filp->f_mapping;
	struct device *dev = bo->base.base.dev->dev;
	size_t old_size = bo->heap_size;
	size_t new_size;
	struct sg_table *new_sgt, *old_sgt;
	bool bo_locked = false, resv_locked = false;
	int i, ret;

	if (old_size >= bo->base.base.size)
		return -ENOSPC;

	if (old_size)
		new_size = old_size + min(old_size, bo->base.base.size - old_size);
	else
		new_size = min_t(u64, (u64)lima_heap_init_nr_pages << PAGE_SHIFT,
				bo->base.base.size);

	if (new_size <= old_size)
		return -EINVAL;

	/*
	 * Never block the GP reset timeout indefinitely on this GEM lock.
	 * vm != NULL only for a recoverable GP heap page fault; trylock
	 * failure preserves the old heap size, scatterlist and DMA mapping.
	 */
	ret = lima_heap_lock_reservation(bo->base.base.resv, vm != NULL);
	if (ret)
		return ret;

	if (bo->base.pages) {
		pages = bo->base.pages;
	} else {
		pages = kvmalloc_objs(*pages, bo->base.base.size >> PAGE_SHIFT,
				      GFP_KERNEL | __GFP_ZERO);
		if (!pages) {
			dma_resv_unlock(bo->base.base.resv);
			return -ENOMEM;
		}

		bo->base.pages = pages;
		refcount_set(&bo->base.pages_use_count, 1);

		mapping_set_unevictable(mapping);
	}

	for (i = old_size >> PAGE_SHIFT; i < new_size >> PAGE_SHIFT; i++) {
		struct page *page;

		/*
		 * A failed growth attempt can have pinned some of these pages.
		 * Reuse their references: fetching them again would overwrite
		 * the old pointer and leak one shmem page reference per retry.
		 */
		if (pages[i])
			continue;

		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page)) {
			dma_resv_unlock(bo->base.base.resv);
			return PTR_ERR(page);
		}
		pages[i] = page;
	}

	dma_resv_unlock(bo->base.base.resv);

	if (vm) {
		/*
		 * The GP timeout waits for recovery_lock: never wait
		 * unboundedly for a BO VA lock here. Hold this one lock
		 * through staging, VM PTE change and final SGT commit so
		 * no second context VM can attach between check/commit.
		 */
		if (!mutex_trylock(&bo->lock))
			return -EBUSY;
		bo_locked = true;
		if (!lima_vm_bo_exclusive_locked(vm, bo)) {
			ret = -EBUSY;
			goto err_unlock_bo;
		}
	}

	/*
	 * Stage a complete new DMA scatterlist before touching the existing
	 * GPU mapping. The old code unmapped/freed bo->base.sgt first, so a
	 * failed dma_map_sgtable() or VM PTE allocation discarded a valid
	 * old heap and could leave stale GPU PTEs referencing unmapped DMA.
	 */
	new_sgt = kmalloc(sizeof(*new_sgt), GFP_KERNEL);
	if (!new_sgt) {
		ret = -ENOMEM;
		goto err_unlock_bo;
	}

	ret = sg_alloc_table_from_pages(new_sgt, pages, i, 0,
					new_size, GFP_KERNEL);
	if (ret)
		goto err_free_new;

	ret = dma_map_sgtable(dev, new_sgt, DMA_BIDIRECTIONAL, 0);
	if (ret)
		goto err_free_sg;

	/* Validate every staged DMA segment and page before rewriting old PTEs. */
	ret = lima_vm_check_sgt_dma(new_sgt, new_size);
	if (ret)
		goto err_unmap_new;

	old_sgt = bo->base.sgt;
	if (vm) {
		/*
		 * The GP has faulted but PP (or external dma-buf users) may
		 * still use old DMA addresses. Trylock avoids waiting on an
		 * unrelated submit while GP timeout waits for recovery_lock.
		 * Keep dma_resv locked until old DMA retires so new jobs cannot
		 * publish a competing fence between this check and commit.
		 */
		if (!dma_resv_trylock(bo->base.base.resv)) {
			ret = -EBUSY;
			goto err_unmap_new;
		}
		resv_locked = true;
		if (!lima_heap_other_dma_idle_locked(bo->base.base.resv,
						       gp_fence)) {
			ret = -EBUSY;
			goto err_unmap_new;
		}

		/*
		 * Recreate the WHOLE active VM mapping using staged DMA
		 * addresses. Mapping only the new tail is invalid when the DMA
		 * API assigns different bus addresses for pre-existing pages.
		 * The VM helper rolls back a partially written range on error.
		 */
		ret = lima_vm_map_bo_locked(vm, bo, 0, new_sgt);
		if (ret) {
			/*
			 * Reinstall PTEs for the old mapped pages while their DMA
			 * addresses and scatterlist are still alive. Existing page
			 * tables should already be allocated for those old pages.
			 */
			if (old_sgt)
				WARN_ON_ONCE(lima_vm_map_bo_locked(vm, bo, 0,
							       old_sgt));
			goto err_unmap_new;
		}
	}

	/*
	 * The GP recovery worker flushes the MMU TLB BEFORE invoking
	 * task_recover(), but we have just rewritten PTEs for the entire
	 * existing heap with fresh DMA bus addresses. That earlier flush
	 * cannot invalidate translations cached during this PTE update.
	 *
	 * Invalidate again while the GP out-of-memory task is paused and
	 * BEFORE dma_unmap_sgtable(old_sgt), which would otherwise retire
	 * bus addresses potentially still cached by the GP MMU.
	 * The MMU helper reads STATUS back to order the posted register
	 * write. This is not a proof that in-flight hardware DMA is idle.
	 */
	if (vm) {
		struct lima_sched_pipe *pipe = &vm->dev->pipe[lima_pipe_gp];
		int mmu;

		if (pipe->bcast_mmu)
			lima_mmu_flush_tlb(pipe->bcast_mmu);
		else
			for (mmu = 0; mmu < pipe->num_mmu; mmu++)
				lima_mmu_flush_tlb(pipe->mmu[mmu]);
	}

	/*
	 * Commit only after both DMA and VM programming succeed. The GEM
	 * shmem finalizer will own exactly one heap-allocated sg_table,
	 * and the previous live DMA mapping is retired only at commit.
	 */
	bo->base.sgt = new_sgt;
	bo->heap_size = new_size;

	if (old_sgt) {
		dma_unmap_sgtable(dev, old_sgt, DMA_BIDIRECTIONAL, 0);
		sg_free_table(old_sgt);
		kfree(old_sgt);
	}
	if (resv_locked)
		dma_resv_unlock(bo->base.base.resv);
	if (bo_locked)
		mutex_unlock(&bo->lock);
	return 0;

err_unmap_new:
	if (resv_locked)
		dma_resv_unlock(bo->base.base.resv);
	dma_unmap_sgtable(dev, new_sgt, DMA_BIDIRECTIONAL, 0);
err_free_sg:
	sg_free_table(new_sgt);
err_free_new:
	kfree(new_sgt);
err_unlock_bo:
	if (bo_locked)
		mutex_unlock(&bo->lock);
	return ret;
}

/*
 * On ARM32, drm_gem_shmem_create() page-aligns size_t with PAGE_ALIGN().
 * A userspace GEM_CREATE size of 0xfffff001..0xffffffff rounds up to
 * 0x100000000, overflowing size_t back to ZERO before object setup.
 * The Utgard MMU also cannot represent an object larger than 32-bit VA.
 *
 * Reject invalid sizes BEFORE shmem creation or any heap/DMA allocation.
 * This guard is intentionally shared by normal and heap GEM objects.
 */
static int lima_gem_validate_create_size(u32 size)
{
	if (!size || size > (U32_MAX & PAGE_MASK))
		return -EINVAL;

	return 0;
}

int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle)
{
	int err;
	gfp_t mask;
	struct drm_gem_shmem_object *shmem;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	bool is_heap = flags & LIMA_BO_FLAG_HEAP;

	err = lima_gem_validate_create_size(size);
	if (err)
		return err;

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);

	obj = &shmem->base;

	/* Mali Utgard GPU can only support 32bit address space */
	mask = mapping_gfp_mask(obj->filp->f_mapping);
	mask &= ~__GFP_HIGHMEM;
	mask |= __GFP_DMA32;
	mapping_set_gfp_mask(obj->filp->f_mapping, mask);

	if (is_heap) {
		bo = to_lima_bo(obj);
		err = lima_heap_alloc(bo, NULL, NULL);
		if (err)
			goto out;
	} else {
		struct sg_table *sgt = drm_gem_shmem_get_pages_sgt(shmem);

		if (IS_ERR(sgt)) {
			err = PTR_ERR(sgt);
			goto out;
		}
	}

	err = drm_gem_handle_create(file, obj, handle);

out:
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(obj);

	return err;
}

static void lima_gem_free_object(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (!list_empty(&bo->va))
		dev_err(obj->dev->dev, "lima gem free bo still has va\n");

	drm_gem_shmem_free(&bo->base);
}

static int lima_gem_object_open(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	return lima_vm_bo_add(vm, bo, true);
}

static void lima_gem_object_close(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	lima_vm_bo_del(vm, bo);
}

static int lima_gem_pin(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (bo->heap_size)
		return -EINVAL;

	return drm_gem_shmem_pin_locked(&bo->base);
}

static int lima_gem_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (bo->heap_size)
		return -EINVAL;

	return drm_gem_shmem_vmap_locked(&bo->base, map);
}

static int lima_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (bo->heap_size)
		return -EINVAL;

	return drm_gem_shmem_mmap(&bo->base, vma);
}

static const struct drm_gem_object_funcs lima_gem_funcs = {
	.free = lima_gem_free_object,
	.open = lima_gem_object_open,
	.close = lima_gem_object_close,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = lima_gem_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = lima_gem_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = lima_gem_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

struct drm_gem_object *lima_gem_create_object(struct drm_device *dev, size_t size)
{
	struct lima_bo *bo;

	bo = kzalloc_obj(*bo);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	mutex_init(&bo->lock);
	INIT_LIST_HEAD(&bo->va);
	bo->base.map_wc = true;
	bo->base.base.funcs = &lima_gem_funcs;

	return &bo->base.base;
}

int lima_gem_get_info(struct drm_file *file, u32 handle, u32 *va, u64 *offset)
{
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	*va = lima_vm_get_va(vm, bo);

	*offset = drm_vma_node_offset_addr(&obj->vma_node);

	drm_gem_object_put(obj);
	return 0;
}

static int lima_gem_sync_bo(struct lima_sched_task *task, struct lima_bo *bo,
			    bool write, bool explicit)
{
	int err;

	err = dma_resv_reserve_fences(lima_bo_resv(bo), 1);
	if (err)
		return err;

	/* explicit sync use user passed dep fence */
	if (explicit)
		return 0;

	return drm_sched_job_add_implicit_dependencies(&task->base,
						       &bo->base.base,
						       write);
}

static int lima_gem_add_deps(struct drm_file *file, struct lima_submit *submit)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(submit->in_sync); i++) {
		if (!submit->in_sync[i])
			continue;

		err = drm_sched_job_add_syncobj_dependency(&submit->task->base, file,
							   submit->in_sync[i], 0);
		if (err)
			return err;
	}

	return 0;
}

int lima_gem_submit(struct drm_file *file, struct lima_submit *submit)
{
	int i, err = 0;
	struct ww_acquire_ctx ctx;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct drm_syncobj *out_sync = NULL;
	struct dma_fence *fence;
	struct lima_bo **bos = submit->lbos;
	struct lima_sched_pipe *pipe =
		&submit->ctx->dev->pipe[submit->pipe];

	/* The long preparation path is speculative: final admission is
	 * serialized with the timeout handler under submit_lock below.
	 */
	if (READ_ONCE(pipe->hard_reset_failed))
		return -ENODEV;
	if (READ_ONCE(pipe->reset_in_progress))
		return -EBUSY;

	if (submit->out_sync) {
		out_sync = drm_syncobj_find(file, submit->out_sync);
		if (!out_sync)
			return -ENOENT;
	}

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj;
		struct lima_bo *bo;

		obj = drm_gem_object_lookup(file, submit->bos[i].handle);
		if (!obj) {
			err = -ENOENT;
			goto err_out0;
		}

		bo = to_lima_bo(obj);

		/* increase refcnt of gpu va map to prevent unmapped when executing,
		 * will be decreased when task done
		 */
		err = lima_vm_bo_add(vm, bo, false);
		if (err) {
			drm_gem_object_put(obj);
			goto err_out0;
		}

		bos[i] = bo;
	}

	err = drm_gem_lock_reservations((struct drm_gem_object **)bos,
					submit->nr_bos, &ctx);
	if (err)
		goto err_out0;

	/*
	 * Reset failure could have happened while importing and locking BOs.
	 * On failure, err_out1 unlocks and err_out0 drops exactly those
	 * lookup refs and VM mappings; no uncompletable fence is installed.
	 */
	if (READ_ONCE(pipe->hard_reset_failed)) {
		err = -ENODEV;
		goto err_out1;
	}
	if (READ_ONCE(pipe->reset_in_progress)) {
		err = -EBUSY;
		goto err_out1;
	}

	err = lima_sched_task_init(
		submit->task, submit->ctx->context + submit->pipe,
		bos, submit->nr_bos, vm, file->client_id);
	if (err)
		goto err_out1;

	err = lima_gem_add_deps(file, submit);
	if (err)
		goto err_out2;

	for (i = 0; i < submit->nr_bos; i++) {
		err = lima_gem_sync_bo(
			submit->task, bos[i],
			submit->bos[i].flags & LIMA_SUBMIT_BO_WRITE,
			submit->flags & LIMA_SUBMIT_FLAG_EXPLICIT_FENCE);
		if (err)
			goto err_out2;
	}

	/* The reset may have failed while resolving explicit or implicit
	 * dependencies. Never publish a fence which the stopped scheduler
	 * cannot signal. err_out2 undoes the armed job, and err_out0 drops
	 * the VM/lookup references after releasing all BO reservations.
	 */
	/*
	 * This brief critical section closes the check-vs-push race:
	 * timeout recovery publishes reset_in_progress under the SAME
	 * mutex BEFORE stopping the scheduler. It never acquires GEM
	 * reservations while holding the mutex. On failure, unwind the
	 * armed job, BO locks and imported GEM/VM refs in err_out2.
	 */
	mutex_lock(&pipe->submit_lock);
	if (READ_ONCE(pipe->hard_reset_failed)) {
		err = -ENODEV;
	} else if (READ_ONCE(pipe->reset_in_progress)) {
		err = -EBUSY;
	} else {
		fence = lima_sched_context_queue_task(submit->task);
		err = 0;
	}
	mutex_unlock(&pipe->submit_lock);
	if (err)
		goto err_out2;

	for (i = 0; i < submit->nr_bos; i++) {
		dma_resv_add_fence(lima_bo_resv(bos[i]), fence,
				   submit->bos[i].flags & LIMA_SUBMIT_BO_WRITE ?
				   DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ);
	}

	drm_gem_unlock_reservations((struct drm_gem_object **)bos,
				    submit->nr_bos, &ctx);

	for (i = 0; i < submit->nr_bos; i++)
		drm_gem_object_put(&bos[i]->base.base);

	if (out_sync) {
		drm_syncobj_replace_fence(out_sync, fence);
		drm_syncobj_put(out_sync);
	}

	dma_fence_put(fence);

	return 0;

err_out2:
	lima_sched_task_fini(submit->task);
err_out1:
	drm_gem_unlock_reservations((struct drm_gem_object **)bos,
				    submit->nr_bos, &ctx);
err_out0:
	for (i = 0; i < submit->nr_bos; i++) {
		if (!bos[i])
			break;
		lima_vm_bo_del(vm, bos[i]);
		drm_gem_object_put(&bos[i]->base.base);
	}
	if (out_sync)
		drm_syncobj_put(out_sync);
	return err;
}

int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, s64 timeout_ns)
{
	bool write = op & LIMA_GEM_WAIT_WRITE;
	long ret, timeout;

	if (!op)
		return 0;

	timeout = drm_timeout_abs_to_jiffies(timeout_ns);

	ret = drm_gem_dma_resv_wait(file, handle, write, timeout);
	if (ret == -ETIME)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	return ret;
}
