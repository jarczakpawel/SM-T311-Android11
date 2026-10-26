// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/reboot.h>
#include <linux/samsung-tab3-xmm6262.h>

#include <linux/cacheflush.h>
#include <asm/outercache.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#define SAMSUNG_INFORM2_OFFSET 0x0808
#define SAMSUNG_INFORM3_OFFSET 0x080c
#define SAMSUNG_INFORM4_OFFSET 0x0810
#define SAMSUNG_REBOOT_CPU_FLOOR_KHZ 1000000
#define SAMSUNG_REBOOT_VALID 0x12345678
#define SAMSUNG_REBOOT_PREFIX 0x12345670
#define SAMSUNG_REBOOT_NORMAL (SAMSUNG_REBOOT_PREFIX | 0)
#define SAMSUNG_REBOOT_DOWNLOAD (SAMSUNG_REBOOT_PREFIX | 1)
#define SAMSUNG_REBOOT_UPLOAD (SAMSUNG_REBOOT_PREFIX | 2)
#define SAMSUNG_REBOOT_CHARGING (SAMSUNG_REBOOT_PREFIX | 3)
#define SAMSUNG_REBOOT_RECOVERY (SAMSUNG_REBOOT_PREFIX | 4)
#define SAMSUNG_REBOOT_FOTA (SAMSUNG_REBOOT_PREFIX | 5)
#define SAMSUNG_REBOOT_FOTA_BL (SAMSUNG_REBOOT_PREFIX | 6)
#define SAMSUNG_REBOOT_SECURE (SAMSUNG_REBOOT_PREFIX | 7)
#define SAMSUNG_REBOOT_SET_PREFIX 0xabc00000
#define SAMSUNG_REBOOT_SET_DEBUG 0x000d0000
#define SAMSUNG_REBOOT_SET_SWSEL 0x000e0000
#define SAMSUNG_REBOOT_SET_SUD 0x000f0000
#define SAMSUNG_SWRESET_VALUE 0x1

struct samsung_sec_reboot {
	struct device *dev;
	void __iomem *pmu_base;
	struct notifier_block reboot_nb;
	struct notifier_block usb_preactive_nb;
	struct notifier_block usb_detach_nb;
	struct notifier_block restart_nb;
	struct freq_qos_request cpu_min_req;
	bool cpu_min_req_active;
};

/*
 * SM-T311 warm-reboot diagnostics: the Exynos4212 USB PHY20 DEVICE and
 * EHCI/OHCI host users share one physical domain.  Capture owner counts and
 * PHY registers on either side of the existing pre-active operation, before
 * XMM6262 CP-off and USB driver detach.  This is deliberately read-only:
 * no PHY register writes, extra resets, or changes to reboot ordering.
 */
static void samsung_sec_t311_phy_reboot_trace(struct device *dev,
                                              const char *phase)
{
	struct tab3_xmm6262_phy_diag phy;
	int ret;

	ret = tab3_t311_phy_diag_snapshot(&phy);
	if (ret) {
		dev_emerg(dev, "T311_REBOOT_PHY phase=%s unavailable=%d\n",
			  phase, ret);
		return;
	}

	dev_emerg(dev,
		  "T311_REBOOT_PHY phase=%s owners=%d,%d,%d,%d runtime=%08x ohci_suspended=%u pwr=%08x clk=%08x rst=%08x tune0=%08x tune1=%08x\n",
		  phase, phy.ext_cnt[0], phy.ext_cnt[1], phy.ext_cnt[2],
		  phy.ext_cnt[3], phy.host_runtime_flags,
		  phy.ohci_runtime_suspended, phy.uphypwr, phy.uphyclk,
		  phy.uphyrst, phy.phy0_phytune, phy.phy1_phytune);
}

static int samsung_sec_usb_preactive_prepare(struct notifier_block *nb,
					     unsigned long action, void *data)
{
	struct samsung_sec_reboot *sec =
		container_of(nb, struct samsung_sec_reboot, usb_preactive_nb);
	const char *cmd = data;
	int ret;

	if (action != SYS_RESTART)
		return NOTIFY_DONE;

	/*
	 * Keep the EHCI -> OHCI runtime-active ordering for recovery too:
	 * recovery also detaches both hosts in the following notifier.
	 * A suspended host can otherwise stall the driver-core detach.
	 */
	if (cmd && *cmd && strcmp(cmd, "shell") && strcmp(cmd, "normal") &&
	    strcmp(cmd, "recovery"))
		return NOTIFY_DONE;

	dev_emerg(sec->dev,
		  "TAB3_V172_PREACTIVE_USB start action=%lu cmd=%s\n",
		  action, (cmd && *cmd) ? cmd : "normal");
	samsung_sec_t311_phy_reboot_trace(sec->dev, "before_preactive");
	ret = tab3_t311_usb_platform_drivers_prepare_active();
	samsung_sec_t311_phy_reboot_trace(sec->dev, "after_preactive");
	dev_emerg(sec->dev, "TAB3_V172_PREACTIVE_USB done ret=%d\n", ret);

	return NOTIFY_DONE;
}

static bool samsung_sec_cmd_is(const char *cmd, const char *target)
{
	size_t len;

	if (!cmd)
		return false;

	len = strlen(target);
	return !strncmp(cmd, target, len) &&
		   (cmd[len] == '\0' || cmd[len] == ',');
}

static u32 samsung_sec_reboot_mode(const char *cmd)
{
	unsigned long value;

	if (!cmd || !*cmd || samsung_sec_cmd_is(cmd, "normal"))
		return SAMSUNG_REBOOT_NORMAL;
	if (samsung_sec_cmd_is(cmd, "recovery"))
		return SAMSUNG_REBOOT_RECOVERY;
	if (samsung_sec_cmd_is(cmd, "bootloader") ||
	    samsung_sec_cmd_is(cmd, "download") ||
	    samsung_sec_cmd_is(cmd, "fastboot"))
		return SAMSUNG_REBOOT_DOWNLOAD;
	if (samsung_sec_cmd_is(cmd, "upload"))
		return SAMSUNG_REBOOT_UPLOAD;
	if (samsung_sec_cmd_is(cmd, "charging"))
		return SAMSUNG_REBOOT_CHARGING;
	if (samsung_sec_cmd_is(cmd, "fota") ||
	    samsung_sec_cmd_is(cmd, "arm11_fota"))
		return SAMSUNG_REBOOT_FOTA;
	if (samsung_sec_cmd_is(cmd, "fota_bl"))
		return SAMSUNG_REBOOT_FOTA_BL;
	if (samsung_sec_cmd_is(cmd, "secure"))
		return SAMSUNG_REBOOT_SECURE;
	if (!strncmp(cmd, "emergency", 9))
		return 0;
	if (!strncmp(cmd, "debug", 5) && !kstrtoul(cmd + 5, 0, &value))
		return SAMSUNG_REBOOT_SET_PREFIX | SAMSUNG_REBOOT_SET_DEBUG | value;
	if (!strncmp(cmd, "swsel", 5) && !kstrtoul(cmd + 5, 0, &value))
		return SAMSUNG_REBOOT_SET_PREFIX | SAMSUNG_REBOOT_SET_SWSEL | value;
	if (!strncmp(cmd, "sud", 3) && !kstrtoul(cmd + 3, 0, &value))
		return SAMSUNG_REBOOT_SET_PREFIX | SAMSUNG_REBOOT_SET_SUD | value;

	return SAMSUNG_REBOOT_NORMAL;
}

static int samsung_sec_reboot_prepare(struct notifier_block *nb,
				      unsigned long action, void *data)
{
	struct samsung_sec_reboot *sec =
		container_of(nb, struct samsung_sec_reboot, reboot_nb);
	struct cpufreq_policy *policy;
	const char *cmd = data;
	u32 inform4 = 0;
	unsigned int before_khz = 0;
	unsigned int after_khz = 0;
	int qos_ret = 0;
	int target_ret = 0;

	if (action != SYS_RESTART)
		return NOTIFY_DONE;

	/*
	 * T311 board code writes INFORM4 from its reboot notifier, before
	 * device_shutdown(): 0 for normal reboot and 0xf for recovery.
	 */
	if (cmd && !strcmp(cmd, "recovery"))
		inform4 = 0xf;
	writel(inform4, sec->pmu_base + SAMSUNG_INFORM4_OFFSET);
	wmb();

	if (cmd && !strcmp(cmd, "recovery")) {
		writel(SAMSUNG_REBOOT_VALID,
		       sec->pmu_base + SAMSUNG_INFORM2_OFFSET);
		writel(SAMSUNG_REBOOT_RECOVERY,
		       sec->pmu_base + SAMSUNG_INFORM3_OFFSET);
		wmb();
		dev_emerg(sec->dev,
			  "T311_RECOVERY_INTENT_EARLY INFORM2=%08x INFORM3=%08x\n",
			  readl(sec->pmu_base + SAMSUNG_INFORM2_OFFSET),
			  readl(sec->pmu_base + SAMSUNG_INFORM3_OFFSET));
	}

	/*
	 * Samsung Exynos4 CPUFREQ takes the PM DVFS lock at L6 (1 GHz) in
	 * its reboot notifier.  Recreate the same minimum-frequency floor
	 * with the modern cpufreq/QoS API and force it immediately if needed.
	 */
	policy = cpufreq_cpu_get(0);
	if (policy) {
		before_khz = policy->cur;
		if (!sec->cpu_min_req_active) {
			qos_ret = freq_qos_add_request(&policy->constraints,
					      &sec->cpu_min_req, FREQ_QOS_MIN,
					      SAMSUNG_REBOOT_CPU_FLOOR_KHZ);
			if (!qos_ret)
				sec->cpu_min_req_active = true;
		}

		if (before_khz < SAMSUNG_REBOOT_CPU_FLOOR_KHZ)
			target_ret = cpufreq_driver_target(policy,
					SAMSUNG_REBOOT_CPU_FLOOR_KHZ,
					CPUFREQ_RELATION_L);
		after_khz = policy->cur;
		cpufreq_cpu_put(policy);
	}

	dev_emerg(sec->dev,
		  "TAB3_V166_REBOOT_PREP action=%lu cmd=%s INFORM4=%08x cpu_before=%u cpu_after=%u qos_ret=%d target_ret=%d\n",
		  action, (cmd && *cmd) ? cmd : "normal",
		  readl(sec->pmu_base + SAMSUNG_INFORM4_OFFSET),
		  before_khz, after_khz, qos_ret, target_ret);

	return NOTIFY_DONE;
}

static int samsung_sec_usb_detach_prepare(struct notifier_block *nb,
					 unsigned long action, void *data)
{
	struct samsung_sec_reboot *sec =
		container_of(nb, struct samsung_sec_reboot, usb_detach_nb);
	int ret;

	if (action != SYS_RESTART && action != SYS_HALT && action != SYS_POWER_OFF)
		return NOTIFY_DONE;

	dev_emerg(sec->dev, "TAB3_V170_PLATFORM_UNBIND_USB start action=%lu cmd=%s\n",
		  action, (data && *(const char *)data) ? (const char *)data : "normal");
	ret = tab3_t311_usb_platform_drivers_detach();
	dev_emerg(sec->dev, "TAB3_V170_PLATFORM_UNBIND_USB done ret=%d\n", ret);
	return NOTIFY_DONE;
}

static int samsung_sec_restart(struct notifier_block *nb,
			       unsigned long action, void *data)
{
	struct samsung_sec_reboot *sec =
		container_of(nb, struct samsung_sec_reboot, restart_nb);
	const char *cmd = data;
	u32 mode = samsung_sec_reboot_mode(cmd);
	u32 inform2 = 0;
	u32 inform3 = 0;
	int attempt;

	/*
	 * Samsung 3.0.31 writes the reboot mode through direct PMU MMIO.
	 * Keep this path lockless: machine_restart() has already stopped the
	 * secondary CPU and disabled IRQs before restart handlers are called.
	 */
	writel(SAMSUNG_REBOOT_VALID,
	       sec->pmu_base + SAMSUNG_INFORM2_OFFSET);
	writel(mode, sec->pmu_base + SAMSUNG_INFORM3_OFFSET);

	inform2 = readl(sec->pmu_base + SAMSUNG_INFORM2_OFFSET);
	inform3 = readl(sec->pmu_base + SAMSUNG_INFORM3_OFFSET);

	dev_emerg(sec->dev,
		  "T311_SEC_REBOOT_V163 cmd=%s action=%lu INFORM2=%08x INFORM3=%08x\n",
		  (cmd && *cmd) ? cmd : "normal", action, inform2, inform3);

	flush_cache_all();
	outer_flush_all();

	/*
	 * Exact Exynos4 Samsung BSP reset policy: exynos4_sw_reset() retries
	 * SWRESET three times with a 500 ms dwell.  A successful write never
	 * returns; later iterations are hardware-failure retries.
	 */
	wmb();
	for (attempt = 0; attempt < 3; attempt++) {
		__raw_writel(SAMSUNG_SWRESET_VALUE,
			     sec->pmu_base + EXYNOS_SWRESET);
		mdelay(500);
	}

	dev_emerg(sec->dev,
		  "T311_SEC_REBOOT_V163 triple raw SWRESET failed\n");
	return NOTIFY_DONE;
}

static int samsung_sec_reboot_probe(struct platform_device *pdev)
{
	struct samsung_sec_reboot *sec;
	int ret;

	sec = devm_kzalloc(&pdev->dev, sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;

	sec->dev = &pdev->dev;
	sec->pmu_base = of_iomap(pdev->dev.parent->of_node, 0);
	if (!sec->pmu_base)
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed to map PMU registers\n");

	sec->reboot_nb.notifier_call = samsung_sec_reboot_prepare;
	/* BUSFREQ prep is 320; XMM early CP-off is 255. */
	sec->reboot_nb.priority = 300;
	ret = devm_register_reboot_notifier(&pdev->dev, &sec->reboot_nb);
	if (ret) {
		iounmap(sec->pmu_base);
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register reboot preparation notifier\n");
	}

	sec->usb_preactive_nb.notifier_call = samsung_sec_usb_preactive_prepare;
	/* Resume/hold USB hosts while the XMM runtime device is still alive. */
	sec->usb_preactive_nb.priority = 270;
	ret = devm_register_reboot_notifier(&pdev->dev, &sec->usb_preactive_nb);
	if (ret) {
		iounmap(sec->pmu_base);
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register T311 USB pre-active notifier\n");
	}

	sec->usb_detach_nb.notifier_call = samsung_sec_usb_detach_prepare;
	sec->usb_detach_nb.priority = 240;
	ret = devm_register_reboot_notifier(&pdev->dev, &sec->usb_detach_nb);
	if (ret) {
		iounmap(sec->pmu_base);
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register T311 platform USB detach notifier\n");
	}

	sec->restart_nb.notifier_call = samsung_sec_restart;
	sec->restart_nb.priority = 255;
	ret = register_restart_handler(&sec->restart_nb);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register restart handler\n");

	platform_set_drvdata(pdev, sec);
	dev_info(&pdev->dev, "T311 Samsung SEC reboot enabled\n");
	return 0;
}

static void samsung_sec_reboot_remove(struct platform_device *pdev)
{
	struct samsung_sec_reboot *sec = platform_get_drvdata(pdev);

	unregister_restart_handler(&sec->restart_nb);
	if (sec->cpu_min_req_active) {
		freq_qos_remove_request(&sec->cpu_min_req);
		sec->cpu_min_req_active = false;
	}
	iounmap(sec->pmu_base);
}

static const struct of_device_id samsung_sec_reboot_of_match[] = {
	{ .compatible = "samsung,exynos-sec-reboot" },
	{ }
};
MODULE_DEVICE_TABLE(of, samsung_sec_reboot_of_match);

static struct platform_driver samsung_sec_reboot_driver = {
	.probe = samsung_sec_reboot_probe,
	.remove = samsung_sec_reboot_remove,
	.driver = {
		.name = "samsung-sec-reboot",
		.of_match_table = samsung_sec_reboot_of_match,
	},
};
module_platform_driver(samsung_sec_reboot_driver);

MODULE_DESCRIPTION("Samsung Exynos SEC reboot protocol for SM-T311");
MODULE_LICENSE("GPL");
