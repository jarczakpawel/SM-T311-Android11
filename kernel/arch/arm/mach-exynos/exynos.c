// SPDX-License-Identifier: GPL-2.0
//
// Samsung Exynos Flattened Device Tree enabled machine
//
// Copyright (c) 2010-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com

#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/sizes.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

#define S3C_ADDR_BASE	0xF6000000
#define S3C_ADDR(x)	((void __iomem __force *)S3C_ADDR_BASE + (x))
#define S5P_VA_CHIPID	S3C_ADDR(0x02000000)

static struct platform_device exynos_cpuidle = {
	.name              = "exynos_cpuidle",
#ifdef CONFIG_ARM_EXYNOS_CPUIDLE
	.dev.platform_data = exynos_enter_aftr,
#endif
	.id                = -1,
};

void __iomem *sysram_base_addr __ro_after_init;
phys_addr_t sysram_base_phys __ro_after_init;
void __iomem *sysram_ns_base_addr __ro_after_init;

unsigned long exynos_cpu_id;
static unsigned int exynos_cpu_rev;

void __init memblock_free_pages(unsigned long pfn, unsigned int order);

#define T311_SAFE_RAM_BASE      ((phys_addr_t)0x40000000)
#define T311_SAFE_RAM_SIZE      ((phys_addr_t)0x3fc00000)
#define T311_SAFE_RAM_END       (T311_SAFE_RAM_BASE + T311_SAFE_RAM_SIZE)
#define T311_EXTRA_RAM_BASE     ((phys_addr_t)0x80000000)
#define T311_EXTRA_RAM_SIZE     ((phys_addr_t)0x40000000)
#define T311_EXTRA_RAM_END      (T311_EXTRA_RAM_BASE + T311_EXTRA_RAM_SIZE)
#define T311_EXTRA_START_PFN    ((unsigned long)(T311_EXTRA_RAM_BASE >> PAGE_SHIFT))
#define T311_EXTRA_END_PFN      ((unsigned long)(T311_EXTRA_RAM_END >> PAGE_SHIFT))
#define T311_EXTRA_SPAN_PAGES   (T311_EXTRA_RAM_SIZE >> PAGE_SHIFT)
#define T311_EXTRA_VALID_PAGES  (T311_EXTRA_SPAN_PAGES / 2)

static bool exynos_t311_fullram_enabled __initdata;
static bool exynos_t311_fullram_selftest_enabled __initdata;
static bool exynos_t311_extra_ram_staged __initdata;
static bool exynos_t311_ram_holes_active __ro_after_init;

/* Cross-translation-unit hooks used by ARM and generic MM code. */
bool arm_pfn_is_hole(unsigned long pfn);
unsigned long __init arch_pfn_hole_count(int nid,
					 unsigned long start_pfn,
					 unsigned long end_pfn);
void __init arch_memmap_init_pfn_holes(void);
unsigned long __init arm_free_staged_highmem(void);

static int __init exynos_t311_fullram_setup(char *value)
{
	if (!value)
		return -EINVAL;

	return kstrtobool(value, &exynos_t311_fullram_enabled);
}
early_param("t311_fullram", exynos_t311_fullram_setup);

static int __init exynos_t311_fullram_selftest_setup(char *value)
{
	if (!value)
		return -EINVAL;

	return kstrtobool(value, &exynos_t311_fullram_selftest_enabled);
}
early_param("t311_fullram_selftest", exynos_t311_fullram_selftest_setup);

bool arm_pfn_is_hole(unsigned long pfn)
{
	return exynos_t311_ram_holes_active &&
	       pfn >= T311_EXTRA_START_PFN &&
	       pfn < T311_EXTRA_END_PFN &&
	       (pfn & 1UL);
}

unsigned long __init arch_pfn_hole_count(int nid, unsigned long start_pfn,
					 unsigned long end_pfn)
{
	unsigned long first;

	(void)nid;
	if (!exynos_t311_ram_holes_active)
		return 0;

	start_pfn = max(start_pfn, T311_EXTRA_START_PFN);
	end_pfn = min(end_pfn, T311_EXTRA_END_PFN);
	if (start_pfn >= end_pfn)
		return 0;

	first = start_pfn | 1UL;
	if (first >= end_pfn)
		return 0;

	return (end_pfn - first + 1) / 2;
}

void __init arch_memmap_init_pfn_holes(void)
{
	unsigned long pfn, holes = 0;

	if (!exynos_t311_ram_holes_active)
		return;

	for (pfn = T311_EXTRA_START_PFN | 1UL;
	     pfn < T311_EXTRA_END_PFN; pfn += 2) {
		__SetPageReserved(pfn_to_page(pfn));
		holes++;
	}

	if (holes != T311_EXTRA_VALID_PAGES)
		panic("T311 full-RAM: wrong hole-page count: %lu", holes);

	pr_info("T311 full-RAM: marked %lu odd PFNs reserved in mem_map\n",
		holes);
}


#define T311_SELFTEST_SALT_A    ((u32)0x243f6a88)
#define T311_SELFTEST_SALT_B    ((u32)0x9e3779b9)

static u32 __init t311_selftest_pattern(unsigned long pfn,
                                        unsigned int word, u32 salt)
{
    u32 x = (u32)pfn * 0x85ebca6bU;

    x ^= (u32)word * 0xc2b2ae35U;
    x ^= salt;
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static void __init t311_selftest_fill(u32 salt)
{
    unsigned long pfn, tested = 0;

    for (pfn = T311_EXTRA_START_PFN; pfn < T311_EXTRA_END_PFN; pfn += 2) {
        struct page *page = pfn_to_page(pfn);
        u32 *mapped;
        unsigned int word;

        if (!pfn_valid(pfn) || !PageReserved(page) ||
            !memblock_is_reserved((phys_addr_t)pfn << PAGE_SHIFT))
            panic("T311 full-RAM exact selftest: PFN %lu not safely staged",
                  pfn);

        mapped = kmap_local_page(page);
        for (word = 0; word < PAGE_SIZE / sizeof(*mapped); word++)
            WRITE_ONCE(mapped[word],
                       t311_selftest_pattern(pfn, word, salt));
        kunmap_local(mapped);

        tested++;
        if (!(tested & 0x3fffUL))
            pr_info("T311 full-RAM exact selftest: fill-A progress %lu/%lu pages\n",
                    tested, (unsigned long)T311_EXTRA_VALID_PAGES);
    }

    if (tested != T311_EXTRA_VALID_PAGES)
        panic("T311 full-RAM exact selftest: fill-A count %lu", tested);
}

static void __init t311_selftest_verify_and_fill(u32 expected_salt,
                                                  u32 next_salt,
                                                  const char *stage)
{
    unsigned long pfn, tested = 0, errors = 0;

    for (pfn = T311_EXTRA_START_PFN; pfn < T311_EXTRA_END_PFN; pfn += 2) {
        struct page *page = pfn_to_page(pfn);
        u32 *mapped;
        unsigned int word;

        mapped = kmap_local_page(page);
        for (word = 0; word < PAGE_SIZE / sizeof(*mapped); word++) {
            u32 expected = t311_selftest_pattern(pfn, word, expected_salt);
            u32 actual = READ_ONCE(mapped[word]);

            if (unlikely(actual != expected)) {
                if (errors < 16)
                    pr_emerg("T311 full-RAM exact selftest: %s mismatch pfn=%lu word=%u expected=%08x actual=%08x\n",
                             stage, pfn, word, expected, actual);
                errors++;
            }
            WRITE_ONCE(mapped[word],
                       t311_selftest_pattern(pfn, word, next_salt));
        }
        kunmap_local(mapped);

        tested++;
        if (!(tested & 0x3fffUL))
            pr_info("T311 full-RAM exact selftest: verify-%s/write-B progress %lu/%lu pages\n",
                    stage, tested, (unsigned long)T311_EXTRA_VALID_PAGES);
    }

    if (tested != T311_EXTRA_VALID_PAGES || errors)
        panic("T311 full-RAM exact selftest: verify-%s failed pages=%lu errors=%lu",
              stage, tested, errors);
}

static void __init t311_selftest_verify_and_zero(u32 expected_salt,
                                                  const char *stage)
{
    unsigned long pfn, tested = 0, errors = 0;

    for (pfn = T311_EXTRA_START_PFN; pfn < T311_EXTRA_END_PFN; pfn += 2) {
        struct page *page = pfn_to_page(pfn);
        u32 *mapped;
        unsigned int word;

        mapped = kmap_local_page(page);
        for (word = 0; word < PAGE_SIZE / sizeof(*mapped); word++) {
            u32 expected = t311_selftest_pattern(pfn, word, expected_salt);
            u32 actual = READ_ONCE(mapped[word]);

            if (unlikely(actual != expected)) {
                if (errors < 16)
                    pr_emerg("T311 full-RAM exact selftest: %s mismatch pfn=%lu word=%u expected=%08x actual=%08x\n",
                             stage, pfn, word, expected, actual);
                errors++;
            }
            WRITE_ONCE(mapped[word], 0);
        }
        kunmap_local(mapped);

        tested++;
        if (!(tested & 0x3fffUL))
            pr_info("T311 full-RAM exact selftest: verify-%s/zero progress %lu/%lu pages\n",
                    stage, tested, (unsigned long)T311_EXTRA_VALID_PAGES);
    }

    if (tested != T311_EXTRA_VALID_PAGES || errors)
        panic("T311 full-RAM exact selftest: verify-%s failed pages=%lu errors=%lu",
              stage, tested, errors);
}

static void __init t311_selftest_verify_zero(void)
{
    unsigned long pfn, tested = 0, errors = 0;

    for (pfn = T311_EXTRA_START_PFN; pfn < T311_EXTRA_END_PFN; pfn += 2) {
        struct page *page = pfn_to_page(pfn);
        u32 *mapped;
        unsigned int word;

        mapped = kmap_local_page(page);
        for (word = 0; word < PAGE_SIZE / sizeof(*mapped); word++) {
            u32 actual = READ_ONCE(mapped[word]);

            if (unlikely(actual != 0)) {
                if (errors < 16)
                    pr_emerg("T311 full-RAM exact selftest: zero mismatch pfn=%lu word=%u actual=%08x\n",
                             pfn, word, actual);
                errors++;
            }
        }
        kunmap_local(mapped);

        tested++;
        if (!(tested & 0x3fffUL))
            pr_info("T311 full-RAM exact selftest: verify-zero progress %lu/%lu pages\n",
                    tested, (unsigned long)T311_EXTRA_VALID_PAGES);
    }

    if (tested != T311_EXTRA_VALID_PAGES || errors)
        panic("T311 full-RAM exact selftest: zero verification failed pages=%lu errors=%lu",
              tested, errors);
}

static void __init exynos_t311_run_exact_selftest(void)
{
    if (!exynos_t311_fullram_selftest_enabled) {
        pr_info("T311 full-RAM exact selftest: skipped by command line\n");
        return;
    }

    pr_info("T311 full-RAM exact selftest: BEGIN pages=%lu bytes=%llu patterns=2 passes=4 alias_check=yes\n",
            (unsigned long)T311_EXTRA_VALID_PAGES,
            (unsigned long long)T311_EXTRA_VALID_PAGES << PAGE_SHIFT);

    t311_selftest_fill(T311_SELFTEST_SALT_A);
    t311_selftest_verify_and_fill(T311_SELFTEST_SALT_A,
                                  T311_SELFTEST_SALT_B, "A");
    t311_selftest_verify_and_zero(T311_SELFTEST_SALT_B, "B");
    t311_selftest_verify_zero();

    pr_info("T311 full-RAM exact selftest: PASS pages=%lu bytes=%llu patterns=2 passes=4 zeroed=yes\n",
            (unsigned long)T311_EXTRA_VALID_PAGES,
            (unsigned long long)T311_EXTRA_VALID_PAGES << PAGE_SHIFT);
}

unsigned long __init arm_free_staged_highmem(void)
{
	unsigned long pfn, valid = 0, holes = 0;

	if (!exynos_t311_ram_holes_active)
		return 0;
	if (!exynos_t311_extra_ram_staged)
		panic("T311 full-RAM: upper span lost its staging reservation");
	if (!memblock_is_region_reserved(T311_EXTRA_RAM_BASE,
					 T311_EXTRA_RAM_SIZE))
		panic("T311 full-RAM: upper span is not reserved before release");

	exynos_t311_run_exact_selftest();

	/*
	 * The generic free-range pass has completed at this point. Keep the
	 * complete sparse span reserved in memblock until memblock metadata is
	 * discarded. This prevents every odd physical hole from becoming visible
	 * to any later early allocator. Valid even PFNs are handed directly to
	 * the buddy allocator below.
	 */
	for (pfn = T311_EXTRA_START_PFN; pfn < T311_EXTRA_END_PFN; pfn++) {
		struct page *page = pfn_to_page(pfn);

		if (!memblock_is_reserved((phys_addr_t)pfn << PAGE_SHIFT))
			panic("T311 full-RAM: PFN %lu escaped staging reservation", pfn);

		if (arm_pfn_is_hole(pfn)) {
			__SetPageReserved(page);
			holes++;
			continue;
		}

		if (!pfn_valid(pfn))
			panic("T311 full-RAM: valid PFN %lu rejected", pfn);
		if (!PageReserved(page))
			panic("T311 full-RAM: valid PFN %lu was not staged", pfn);

		memblock_free_pages(pfn, 0);
		valid++;
	}

	if (valid != T311_EXTRA_VALID_PAGES ||
	    holes != T311_EXTRA_VALID_PAGES)
		panic("T311 full-RAM: release geometry mismatch valid=%lu holes=%lu",
		      valid, holes);

	exynos_t311_extra_ram_staged = false;
	pr_info("T311 full-RAM: released %lu valid highmem pages (%lu MiB); %lu odd PFNs remain holes; memblock reservation retained\n",
		valid, (valid << PAGE_SHIFT) >> 20, holes);
	return valid;
}

unsigned int exynos_rev(void)
{
	return exynos_cpu_rev;
}

void __init exynos_sysram_init(void)
{
	for_each_compatible_node_scoped(node, NULL, "samsung,exynos4210-sysram") {
		struct resource res;
		if (!of_device_is_available(node))
			continue;

		of_address_to_resource(node, 0, &res);
		sysram_base_addr = ioremap(res.start, resource_size(&res));
		sysram_base_phys = res.start;
		break;
	}

	for_each_compatible_node_scoped(node, NULL, "samsung,exynos4210-sysram-ns") {
		if (!of_device_is_available(node))
			continue;
		sysram_ns_base_addr = of_iomap(node, 0);
		break;
	}
}

static int __init exynos_fdt_map_chipid(unsigned long node, const char *uname,
					int depth, void *data)
{
	struct map_desc iodesc;
	const __be32 *reg;
	int len;

	if (!of_flat_dt_is_compatible(node, "samsung,exynos4210-chipid"))
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &len);
	if (reg == NULL || len != (sizeof(unsigned long) * 2))
		return 0;

	iodesc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iodesc.length = be32_to_cpu(reg[1]) - 1;
	iodesc.virtual = (unsigned long)S5P_VA_CHIPID;
	iodesc.type = MT_DEVICE;
	iotable_init(&iodesc, 1);
	return 1;
}

static void __init exynos_init_io(void)
{
	debug_ll_io_init();

	of_scan_flat_dt(exynos_fdt_map_chipid, NULL);

	/* detect cpu id and rev. */
	exynos_cpu_id = readl_relaxed(S5P_VA_CHIPID);
	exynos_cpu_rev = exynos_cpu_id & 0xFF;

	pr_info("Samsung CPU ID: 0x%08lx\n", exynos_cpu_id);

}

/*
 * Set or clear the USE_DELAYED_RESET_ASSERTION option. Used by smp code
 * and suspend.
 *
 * This is necessary only on Exynos4 SoCs. When system is running
 * USE_DELAYED_RESET_ASSERTION should be set so the ARM CLK clock down
 * feature could properly detect global idle state when secondary CPU is
 * powered down.
 *
 * However this should not be set when such system is going into suspend.
 */
void exynos_set_delayed_reset_assertion(bool enable)
{
	if (of_machine_is_compatible("samsung,exynos4")) {
		unsigned int tmp, core_id;

		for (core_id = 0; core_id < num_possible_cpus(); core_id++) {
			tmp = pmu_raw_readl(EXYNOS_ARM_CORE_OPTION(core_id));
			if (enable)
				tmp |= S5P_USE_DELAYED_RESET_ASSERTION;
			else
				tmp &= ~(S5P_USE_DELAYED_RESET_ASSERTION);
			pmu_raw_writel(tmp, EXYNOS_ARM_CORE_OPTION(core_id));
		}
	}
}

/*
 * Apparently, these SoCs are not able to wake-up from suspend using
 * the PMU. Too bad. Should they suddenly become capable of such a
 * feat, the matches below should be moved to suspend.c.
 */
static const struct of_device_id exynos_dt_pmu_match[] = {
	{ .compatible = "samsung,exynos5260-pmu" },
	{ .compatible = "samsung,exynos5410-pmu" },
	{ /*sentinel*/ },
};

static void exynos_map_pmu(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, exynos_dt_pmu_match);
	if (np)
		pmu_base_addr = of_iomap(np, 0);
	of_node_put(np);
}

static void __init exynos_init_irq(void)
{
	irqchip_init();
	/*
	 * Since platsmp.c needs pmu base address by the time
	 * DT is not unflatten so we can't use DT APIs before
	 * init_irq
	 */
	exynos_map_pmu();
}

static void __init exynos_dt_machine_init(void)
{
	/*
	 * This is called from smp_prepare_cpus if we've built for SMP, but
	 * we still need to set it up for PM and firmware ops if not.
	 */
	if (!IS_ENABLED(CONFIG_SMP))
		exynos_sysram_init();

#if defined(CONFIG_SMP) && defined(CONFIG_ARM_EXYNOS_CPUIDLE)
	if (of_machine_is_compatible("samsung,exynos4210") ||
	    of_machine_is_compatible("samsung,exynos3250"))
		exynos_cpuidle.dev.platform_data = &cpuidle_coupled_exynos_data;
#endif
	if (of_machine_is_compatible("samsung,exynos4210") ||
	    of_machine_is_compatible("samsung,exynos4212") ||
	    (of_machine_is_compatible("samsung,exynos4412") &&
	     (of_machine_is_compatible("samsung,trats2") ||
		  of_machine_is_compatible("samsung,midas") ||
		  of_machine_is_compatible("samsung,p4note"))) ||
	    of_machine_is_compatible("samsung,exynos3250") ||
	    of_machine_is_compatible("samsung,exynos5250"))
		platform_device_register(&exynos_cpuidle);
}

static void __init exynos_reserve(void)
{
	const unsigned long root = of_get_flat_dt_root();
	phys_addr_t extra_start = T311_EXTRA_RAM_BASE;
	phys_addr_t extra_end = T311_EXTRA_RAM_END - 1;

	if (!IS_ENABLED(CONFIG_EXYNOS_T311_RAM_HOLES) ||
	    !of_flat_dt_is_compatible(root, "samsung,t311"))
		return;

	BUILD_BUG_ON(PAGE_SIZE != SZ_4K);
	BUILD_BUG_ON(T311_EXTRA_SPAN_PAGES != 262144);
	BUILD_BUG_ON(T311_EXTRA_VALID_PAGES != 131072);

	if (!memblock_is_region_memory(T311_SAFE_RAM_BASE, T311_SAFE_RAM_SIZE) ||
	    memblock_is_region_memory(T311_SAFE_RAM_END,
				      T311_EXTRA_RAM_BASE - T311_SAFE_RAM_END) ||
	    !memblock_is_region_memory(T311_EXTRA_RAM_BASE,
				       T311_EXTRA_RAM_SIZE))
		panic("T311 full-RAM: unexpected DT memory geometry");

	if (!exynos_t311_fullram_enabled) {
		if (memblock_remove(T311_EXTRA_RAM_BASE, T311_EXTRA_RAM_SIZE))
			panic("T311 full-RAM: failed to select safe 1020 MiB mode");

		pr_info("T311 full-RAM: disabled; using proven 1020 MiB layout\n");
		return;
	}

	exynos_t311_ram_holes_active = true;
	if (memblock_reserve(T311_EXTRA_RAM_BASE, T311_EXTRA_RAM_SIZE))
		panic("T311 full-RAM: failed to reserve sparse upper span");
	exynos_t311_extra_ram_staged = true;

	pr_info("T311 full-RAM: staged %pa-%pa before CMA; even PFNs valid, odd PFNs holes\n",
		&extra_start, &extra_end);
}

static char const *const exynos_dt_compat[] __initconst = {
	"samsung,exynos3",
	"samsung,exynos3250",
	"samsung,exynos4",
	"samsung,exynos4210",
	"samsung,exynos4212",
	"samsung,exynos4412",
	"samsung,exynos5",
	"samsung,exynos5250",
	"samsung,exynos5260",
	"samsung,exynos5420",
	NULL
};

static void __init exynos_dt_fixup(void)
{
	/*
	 * Some versions of uboot pass garbage entries in the memory node,
	 * use the old CONFIG_ARM_NR_BANKS
	 */
	of_fdt_limit_memory(8);
}

DT_MACHINE_START(EXYNOS_DT, "Samsung Exynos (Flattened Device Tree)")
	.l2c_aux_val	= 0x08400000,
	.l2c_aux_mask	= 0xf60fffff,
	.smp		= smp_ops(exynos_smp_ops),
	.map_io		= exynos_init_io,
	.reserve	= exynos_reserve,
	.init_early	= exynos_firmware_init,
	.init_irq	= exynos_init_irq,
	.init_machine	= exynos_dt_machine_init,
	.init_late	= exynos_pm_init,
	.dt_compat	= exynos_dt_compat,
	.dt_fixup	= exynos_dt_fixup,
MACHINE_END
