// SPDX-License-Identifier: GPL-2.0+
/*
 * Samsung Exynos USB HOST EHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-samsung-usb2.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/samsung-tab3-xmm6262.h>
#include <linux/timekeeping.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"

#define DRIVER_DESC "EHCI Exynos driver"

#define EHCI_INSNREG00(base)			(base + 0x90)
#define EHCI_INSNREG00_ENA_INCR16		(0x1 << 25)
#define EHCI_INSNREG00_ENA_INCR8		(0x1 << 24)
#define EHCI_INSNREG00_ENA_INCR4		(0x1 << 23)
#define EHCI_INSNREG00_ENA_INCRX_ALIGN		(0x1 << 22)
#define EHCI_INSNREG00_ENABLE_DMA_BURST	\
	(EHCI_INSNREG00_ENA_INCR16 | EHCI_INSNREG00_ENA_INCR8 |	\
	 EHCI_INSNREG00_ENA_INCR4 | EHCI_INSNREG00_ENA_INCRX_ALIGN)
#define EHCI_INSNREG00_OHCI_SUSP_LGCY		(0x1 << 20)

static struct hc_driver __read_mostly exynos_ehci_hc_driver;

#define PHY_NUMBER 3

struct exynos_ehci_hcd {
	struct clk *clk;
	struct device_node *of_node;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
	bool power_on;
	bool cbd_compat_link;
};

#define to_exynos_ehci(hcd) (struct exynos_ehci_hcd *)(hcd_to_ehci(hcd)->priv)

#ifdef CONFIG_SAMSUNG_TAB3_XMM6262
static int t311_ehci_irq = -1;
static int t311_ehci_cpuhp_state = -1;

static int t311_ehci_cpu_online(unsigned int cpu)
{
	int irq = READ_ONCE(t311_ehci_irq);

	if (cpu == 1 && irq >= 0)
		irq_set_affinity(irq, cpumask_of(1));

	return 0;
}

static int t311_ehci_cpu_offline(unsigned int cpu)
{
	int irq = READ_ONCE(t311_ehci_irq);

	if (cpu == 1 && irq >= 0)
		irq_set_affinity(irq, cpumask_of(0));

	return 0;
}

int tab3_t311_ehci_clock_snapshot(struct usb_hcd *hcd, u32 *usbhost_rate,
				  u32 *aclk133_rate, u32 *mout_rate,
				  u32 *aclk133_div_minus_one)
{
	struct exynos_ehci_hcd *exynos_ehci;
	struct clk_hw *usbhost_hw, *aclk133_hw, *mout_hw;
	unsigned long div;

	if (!hcd || !usbhost_rate || !aclk133_rate || !mout_rate ||
	    !aclk133_div_minus_one ||
	    !of_machine_is_compatible("samsung,t311"))
		return -EINVAL;

	exynos_ehci = to_exynos_ehci(hcd);
	if (!exynos_ehci || IS_ERR_OR_NULL(exynos_ehci->clk))
		return -ENODEV;

	/*
	 * v177 is recorder-only. Use CCF's cached, lockless HW rates so an
	 * EP0 completion snapshot cannot sleep, recalculate a clock, or take
	 * the global clock prepare lock. usb_host is parented by aclk133 on
	 * Exynos4; its parent is mout_aclk133.
	 */
	usbhost_hw = __clk_get_hw(exynos_ehci->clk);
	if (!usbhost_hw)
		return -ENODEV;

	aclk133_hw = clk_hw_get_parent(usbhost_hw);
	mout_hw = aclk133_hw ? clk_hw_get_parent(aclk133_hw) : NULL;
	*usbhost_rate = (u32)clk_hw_get_rate(usbhost_hw);
	*aclk133_rate = aclk133_hw ? (u32)clk_hw_get_rate(aclk133_hw) : 0;
	*mout_rate = mout_hw ? (u32)clk_hw_get_rate(mout_hw) : 0;
	if (*aclk133_rate && *mout_rate) {
		div = DIV_ROUND_CLOSEST(*mout_rate, *aclk133_rate);
		*aclk133_div_minus_one = div ? (u32)(div - 1) : U32_MAX;
	} else {
		*aclk133_div_minus_one = U32_MAX;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tab3_t311_ehci_clock_snapshot);
#endif

static int exynos_ehci_get_phy(struct device *dev,
				struct exynos_ehci_hcd *exynos_ehci)
{
	struct phy *phy;
	int phy_number, num_phys;
	int ret;

	/* Get PHYs for the controller */
	num_phys = of_count_phandle_with_args(dev->of_node, "phys",
					      "#phy-cells");
	if (num_phys > PHY_NUMBER)
		return -EINVAL;
	for (phy_number = 0; phy_number < num_phys; phy_number++) {
		phy = devm_of_phy_get_by_index(dev, dev->of_node, phy_number);
		if (IS_ERR(phy))
			return PTR_ERR(phy);
		exynos_ehci->phy[phy_number] = phy;
	}
	if (num_phys > 0)
		return 0;

	/* Get PHYs using legacy bindings */
	for_each_available_child_of_node_scoped(dev->of_node, child) {
		ret = of_property_read_u32(child, "reg", &phy_number);
		if (ret) {
			dev_err(dev, "Failed to parse device tree\n");
			return ret;
		}

		if (phy_number >= PHY_NUMBER) {
			dev_err(dev, "Invalid number of PHYs\n");
			return -EINVAL;
		}

		phy = devm_of_phy_optional_get(dev, child, NULL);
		exynos_ehci->phy[phy_number] = phy;
		if (IS_ERR(phy))
			return PTR_ERR(phy);
	}

	exynos_ehci->legacy_phy = true;
	return 0;
}

static int exynos_ehci_phy_init(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		ret = phy_init(exynos_ehci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			phy_exit(exynos_ehci->phy[i]);

	return ret;
}

static void exynos_ehci_phy_exit(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		phy_exit(exynos_ehci->phy[i]);
}

static int exynos_ehci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int i;
	int ret = 0;

	if (of_machine_is_compatible("samsung,t311")) {
		ret = samsung_usb2_phy_host_init(exynos_ehci->phy[0],
					SAMSUNG_USB2_PHY_HOST_EHCI);
		if (ret)
			return ret;
	}

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		ret = phy_power_on(exynos_ehci->phy[i]);
	if (ret) {
		for (i--; i >= 0; i--)
			phy_power_off(exynos_ehci->phy[i]);
		if (of_machine_is_compatible("samsung,t311"))
			samsung_usb2_phy_host_exit(exynos_ehci->phy[0],
					   SAMSUNG_USB2_PHY_HOST_EHCI);
	}

	return ret;
}

static void exynos_ehci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	int i;

	for (i = 0; i < PHY_NUMBER; i++)
		phy_power_off(exynos_ehci->phy[i]);

	if (of_machine_is_compatible("samsung,t311"))
		samsung_usb2_phy_host_exit(exynos_ehci->phy[0],
				   SAMSUNG_USB2_PHY_HOST_EHCI);
}

/*
 * Samsung T311 has one physical PHY20 domain with independent
 * EHCI/OHCI/DEVICE logical users.  Mainline exposes HOST/HSIC0/HSIC1 as
 * generic PHYs; the T311 provider folds those references back into the
 * stock shared-domain power contract.  OHCI ownership is held by the real
 * exynos-ohci HCD, not by EHCI compatibility bookkeeping.
 */

static unsigned int exynos_ehci_t311_wait_regs(struct device *dev,
					       struct usb_hcd *hcd)
{
	unsigned int delay_count = 0;

	while (!readl(hcd->regs) && delay_count < 200) {
		udelay(1);
		delay_count++;
	}

	dev_dbg(dev,
		 "SAMSUNG_BSP_EHCI_REG_WAIT us=%u limit_us=200\n",
		 delay_count);
	return delay_count;
}

static void exynos_ehci_t311_wait_cp_resume(struct device *dev,
					    struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 portsc = 0;
	unsigned int retries = 0;

	if (!of_machine_is_compatible("samsung,t311"))
		return;

	/* Exact CONFIG_LINK_DEVICE_HSIC board notification ordering. */
	tab3_xmm6262_hsic_host_state(true);
	if (!tab3_xmm6262_phone_active()) {
		/* Stock force-powers CP port 2 off when PHONE_ACTIVE is low. */
		(void)ehci_hub_control(hcd, ClearPortFeature,
				       USB_PORT_FEAT_POWER, 2, NULL, 0);
		(void)ehci_readl(ehci, &ehci->regs->command);
		dev_dbg(dev,
			 "SAMSUNG_BSP_CP_RESUME phone_active=0 action=PORT2_POWER_OFF_AND_SKIP_CONNECT_WAIT\n");
		return;
	}

	do {
		usleep_range(10000, 11000);
		portsc = ehci_readl(ehci, &ehci->regs->port_status[1]);
	} while (++retries < 30 && !(portsc & PORT_CONNECT));

	dev_dbg(dev,
		 "SAMSUNG_BSP_CP_RESUME phone_active=1 retries=%u port2=%08x connect=%u max_wait_ms=300\n",
		 retries, portsc, !!(portsc & PORT_CONNECT));
}

static ssize_t ehci_power_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);

	return sysfs_emit(buf, "EHCI Power %s\n",
			  exynos_ehci->power_on ? "on" : "off");
}

static int exynos_ehci_set_power(struct device *dev, bool power_on)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci;
	bool t311 = of_machine_is_compatible("samsung,t311");
	bool was_on;
	u64 t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
	int rh_before = -1, rh_after = -1;
	int dev_before = -1, dev_after = -1;
	int irq;
	int ret = 0;
	bool teardown_reserved = false;

	if (!hcd)
		return -ENODEV;
	exynos_ehci = to_exynos_ehci(hcd);

	device_lock(dev);

	if (!power_on) {
		if (!exynos_ehci->power_on)
			goto out;
		if (t311) {
			ret = samsung_usb2_phy_t311_host_teardown_begin(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
			if (ret)
				goto out;
			teardown_reserved = true;
		}

		if (t311)
			tab3_xmm6262_handoff_trace_event(
				TAB3_XMM_HANDOFF_EHCI_OFF_ENTER,
				exynos_ehci->power_on,
				dev->power.runtime_status,
				hcd->self.root_hub ?
					hcd->self.root_hub->dev.power.runtime_status : ~0U,
				0);
		if (t311)
			tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_EHCI_OFF_ENTER);
		if (t311) {
			t0 = ktime_get_ns();
			dev_before = dev->power.runtime_status;
			if (hcd->self.root_hub)
				rh_before = hcd->self.root_hub->dev.power.runtime_status;
		}
		if (hcd->self.root_hub)
			pm_runtime_forbid(&hcd->self.root_hub->dev);
		if (t311) {
			t1 = ktime_get_ns();
			if (hcd->self.root_hub)
				rh_after = hcd->self.root_hub->dev.power.runtime_status;
		}
		pm_runtime_forbid(dev);
		if (t311) {
			t2 = ktime_get_ns();
			dev_after = dev->power.runtime_status;
		}
		exynos_ehci->power_on = false;
		if (of_machine_is_compatible("samsung,t311"))
			samsung_usb2_phy_t311_hcd_ready(exynos_ehci->phy[0],
						 SAMSUNG_USB2_PHY_HOST_EHCI, false);
		usb_remove_hcd(hcd);
		if (t311) {
			tab3_xmm6262_handoff_trace_event(
				TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_REMOVE,
				exynos_ehci->power_on,
				dev->power.runtime_status,
				hcd->self.root_hub ?
					hcd->self.root_hub->dev.power.runtime_status : ~0U,
				0);
			tab3_xmm6262_handoff_quiesce_end();
			tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_EHCI_OFF_AFTER_REMOVE);
			t3 = ktime_get_ns();
		}

		exynos_ehci_phy_disable(dev);
		exynos_ehci_phy_exit(dev);
		if (t311) {
			t4 = ktime_get_ns();
			tab3_xmm6262_handoff_trace_event(
				TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_PHY,
				exynos_ehci->power_on,
				dev->power.runtime_status, 0, 0);
			tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_EHCI_OFF_AFTER_PHY);
		}

		/*
		 * Successful 3.0.31 T311 traces expose LINK_ACTIVE low as soon as
		 * the real remove/PHY-exit boundary is complete.  Do not impose the
		 * old sampled 48 ms minimum here.
		 */
		tab3_xmm6262_hsic_host_state(false);

		if (exynos_ehci->legacy_phy)
			pdev->dev.of_node = exynos_ehci->of_node;

		if (t311) {
			dev_info(dev,
				 "T311_EHCI_OFF_TIMING rh_before=%d rh_after=%d dev_before=%d dev_after=%d rh_forbid_us=%llu dev_forbid_us=%llu remove_us=%llu phy_us=%llu total_us=%llu\n",
				 rh_before, rh_after, dev_before, dev_after,
				 (unsigned long long)div_u64(t1 - t0, 1000),
				 (unsigned long long)div_u64(t2 - t1, 1000),
				 (unsigned long long)div_u64(t3 - t2, 1000),
				 (unsigned long long)div_u64(t4 - t3, 1000),
				 (unsigned long long)div_u64(t4 - t0, 1000));
			dev_dbg(dev, "XMM6262 EHCI off: HCD removed\n");
		}
		goto out;
	}

	was_on = exynos_ehci->power_on;
	if (t311)
		tab3_xmm6262_handoff_trace_event(
			TAB3_XMM_HANDOFF_EHCI_ON_ENTER,
			was_on, dev->power.runtime_status, 0, 0);
	if (t311)
		tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_EHCI_ON_ENTER);

	if (was_on) {
		if (t311) {
			ret = samsung_usb2_phy_t311_host_teardown_begin(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
			if (ret)
				goto out_unlock;
			teardown_reserved = true;
		}
		pm_runtime_forbid(dev);
		if (of_machine_is_compatible("samsung,t311"))
			samsung_usb2_phy_t311_hcd_ready(exynos_ehci->phy[0],
						 SAMSUNG_USB2_PHY_HOST_EHCI, false);
		usb_remove_hcd(hcd);
		tab3_xmm6262_hsic_host_state(false);
	} else {
		tab3_xmm6262_hsic_phy_init_notify();
		ret = exynos_ehci_phy_init(dev);
		if (ret)
			goto out_unlock;
		ret = exynos_ehci_phy_enable(dev);
		if (ret)
			goto err_phy_exit;
		if (t311) {
			exynos_ehci_t311_wait_regs(dev, hcd);
			tab3_xmm6262_handoff_trace_event(
				TAB3_XMM_HANDOFF_EHCI_ON_AFTER_PHY,
				0, dev->power.runtime_status, 0, 0);
			tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_EHCI_ON_AFTER_PHY);
		}
		if (t311) {
			u32 before = readl(EHCI_INSNREG00(hcd->regs));
			u32 after = before | EHCI_INSNREG00_ENABLE_DMA_BURST;

			writel(after, EHCI_INSNREG00(hcd->regs));
		} else {
			writel(readl(EHCI_INSNREG00(hcd->regs)) |
			       EHCI_INSNREG00_ENABLE_DMA_BURST,
			       EHCI_INSNREG00(hcd->regs));
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_power_on;
	}
	if (exynos_ehci->legacy_phy)
		pdev->dev.of_node = NULL;
	if (t311)
		tab3_xmm6262_handoff_trace_event(
			TAB3_XMM_HANDOFF_EHCI_ADD_BEGIN,
			irq, was_on, 0, 0);
	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_power_on;
	if (t311)
		samsung_usb2_phy_t311_hcd_ready(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI, true);
	if (t311 && num_possible_cpus() > 1)
		irq_set_affinity(irq, cpumask_of(cpu_online(1) ? 1 : 0));

	exynos_ehci->power_on = true;
	if (t311)
		tab3_xmm6262_handoff_trace_event(
			TAB3_XMM_HANDOFF_EHCI_ADD_END,
			irq, exynos_ehci->power_on, 0, 0);
	if (t311)
		tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_EHCI_ADD_END);
	if (hcd->self.root_hub) {
		/* The PM API takes milliseconds, not jiffies. HZ=200 made
		 * the old 500-jiffies expression only 100 ms on T311.
		 */
		if (t311)
			pm_runtime_set_autosuspend_delay(&hcd->self.root_hub->dev,
							 500);
		else
			pm_runtime_set_autosuspend_delay(&hcd->self.root_hub->dev, 100);
	}

	tab3_xmm6262_hsic_host_state(true);
	pm_runtime_forbid(dev);
	if (t311)
		dev_dbg(dev, "XMM6262 EHCI on: HCD added, autosuspend=%u ms\n",
			500U);
	goto out;

err_power_on:
	if (exynos_ehci->legacy_phy)
		pdev->dev.of_node = exynos_ehci->of_node;
	exynos_ehci->power_on = false;
	exynos_ehci_phy_disable(dev);
err_phy_exit:
	exynos_ehci_phy_exit(dev);
out_unlock:
	if (teardown_reserved)
		samsung_usb2_phy_t311_host_teardown_end(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
	device_unlock(dev);
	return ret;
out:
	if (teardown_reserved)
		samsung_usb2_phy_t311_host_teardown_end(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
	device_unlock(dev);
	return ret;
}


static struct device *t311_ehci_platform_dev;

static ssize_t ehci_power_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	int power_on;
	int ret;

	if (!hcd)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &power_on);
	if (ret)
		return ret;

	ret = exynos_ehci_set_power(dev, !!power_on);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(ehci_power);

static ssize_t port_power_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci;
	struct ehci_hcd *ehci;
	int power_on, port, ret;

	if (!hcd)
		return -ENODEV;
	exynos_ehci = to_exynos_ehci(hcd);
	ehci = hcd_to_ehci(hcd);

	if (sscanf(buf, "%d %d", &power_on, &port) != 2 ||
	    port < 1 || port > 3 || power_on < 0 || power_on > 1)
		return -EINVAL;

	device_lock(dev);
	if (!exynos_ehci->power_on || !hcd->self.root_hub) {
		ret = -ENODEV;
		goto out_unlock;
	}
	ret = ehci_hub_control(hcd,
			       power_on ? SetPortFeature : ClearPortFeature,
			       USB_PORT_FEAT_POWER, port, NULL, 0);
	if (!ret) {
		(void)ehci_readl(ehci, &ehci->regs->command);
		if (of_machine_is_compatible("samsung,t311") && port == 2)
			tab3_xmm6262_hsic_host_state(!!power_on);
		dev_dbg(dev,
			 "SAMSUNG_BSP_PORT_POWER port=%d power=%d host_state_notify=%u\n",
			 port, power_on,
			 of_machine_is_compatible("samsung,t311") && port == 2);
	}

out_unlock:
	device_unlock(dev);
	return ret ? ret : count;
}
static DEVICE_ATTR(port_power, 0220, NULL, port_power_store);

static void exynos_setup_vbus_gpio(struct device *dev)
{
	struct gpio_desc *gpio;
	int err;

	gpio = devm_gpiod_get_optional(dev, "samsung,vbus", GPIOD_OUT_HIGH);
	err = PTR_ERR_OR_ZERO(gpio);
	if (err)
		dev_err(dev, "can't request ehci vbus gpio: %d\n", err);
}

static int exynos_ehci_probe(struct platform_device *pdev)
{
	struct exynos_ehci_hcd *exynos_ehci;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource *res;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	err = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	exynos_setup_vbus_gpio(&pdev->dev);

	hcd = usb_create_hcd(&exynos_ehci_hc_driver,
			     &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}
	exynos_ehci = to_exynos_ehci(hcd);

	err = exynos_ehci_get_phy(&pdev->dev, exynos_ehci);
	if (err)
		goto fail_io;

	exynos_ehci->clk = devm_clk_get_enabled(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ehci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ehci->clk);
		goto fail_io;
	}

	hcd->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto fail_io;
	}

	platform_set_drvdata(pdev, hcd);
	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;
	exynos_ehci->of_node = pdev->dev.of_node;

	/* Normal boot: safe no-op until XMM control is actually bound/powered. */
	tab3_xmm6262_hsic_phy_init_notify();
	err = exynos_ehci_phy_init(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize USB phy\n");
		goto fail_io;
	}

	err = exynos_ehci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_phy_init;
	}

	if (of_machine_is_compatible("samsung,t311"))
		exynos_ehci_t311_wait_regs(&pdev->dev, hcd);

	/*
	 * Workaround: reset of_node pointer to avoid conflict between legacy
	 * Exynos EHCI port subnodes and generic USB device bindings
	 */
	if (exynos_ehci->legacy_phy)
		pdev->dev.of_node = NULL;

	/* DMA burst Enable; T311 keeps Samsung's read-modify-write semantics. */
	if (of_machine_is_compatible("samsung,t311")) {
		u32 before = readl(EHCI_INSNREG00(hcd->regs));
		u32 after = before | EHCI_INSNREG00_ENABLE_DMA_BURST;

		writel(after, EHCI_INSNREG00(hcd->regs));
	} else {
		writel(EHCI_INSNREG00_ENABLE_DMA_BURST,
		       EHCI_INSNREG00(hcd->regs));
	}

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	if (of_machine_is_compatible("samsung,t311"))
		samsung_usb2_phy_t311_hcd_ready(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI, true);
	if (of_machine_is_compatible("samsung,t311") && num_possible_cpus() > 1)
		irq_set_affinity(irq, cpumask_of(cpu_online(1) ? 1 : 0));
	exynos_ehci->power_on = true;
	/* Stock T311 programs its vendor autosuspend value only on later ehci_power=1. */
	tab3_xmm6262_hsic_host_state(true);
	if (of_machine_is_compatible("samsung,t311")) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
		pm_runtime_forbid(&pdev->dev);
	}
	device_wakeup_enable(hcd->self.controller);

	err = device_create_file(&pdev->dev, &dev_attr_ehci_power);
	if (err)
		goto fail_power_attr;

	if (of_machine_is_compatible("samsung,t311")) {
		err = device_create_file(&pdev->dev, &dev_attr_port_power);
		if (err)
			goto fail_port_power_attr;
	}

	err = sysfs_create_link(&platform_bus.kobj, &pdev->dev.kobj,
				 "s5p-ehci");
	if (err) {
		dev_warn(&pdev->dev,
			 "CBD sysfs compatibility link unavailable: %d\n",
			 err);
	} else {
		exynos_ehci->cbd_compat_link = true;
		dev_dbg(&pdev->dev,
			 "CBD_EHCI_COMPAT=READY path=/sys/devices/platform/s5p-ehci/ehci_power target=%s\n",
			 dev_name(&pdev->dev));
	}


	if (of_machine_is_compatible("samsung,t311")) {
		WRITE_ONCE(t311_ehci_platform_dev, &pdev->dev);
#ifdef CONFIG_SAMSUNG_TAB3_XMM6262
		if (num_possible_cpus() > 1) {
			WRITE_ONCE(t311_ehci_irq, irq);
			t311_ehci_cpuhp_state =
				cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
							  "usb/ehci-t311:online",
							  t311_ehci_cpu_online,
							  t311_ehci_cpu_offline);
			if (t311_ehci_cpuhp_state < 0)
				dev_warn(&pdev->dev,
					 "failed to register EHCI IRQ CPU hotplug state: %d\n",
					 t311_ehci_cpuhp_state);
		}
#endif
	}

	dev_dbg(&pdev->dev, "SAMSUNG_BSP_CONTRACT=READY scope=T311 profile=COHERENT_STOCK_SEMANTICS\n");
	dev_dbg(&pdev->dev, "T311_REAL_OHCI=READY owner=REAL_HCD host_phy=OHCI_DRIVER\n");
	if (of_machine_is_compatible("samsung,t311"))
		dev_dbg(&pdev->dev,
			 "T311_USB_CORE=READY synthetic_hub_barrier=OFF reenum_hold=OFF fake_port_status=OFF host_state=SAMSUNG_STOCK\n");
	dev_dbg(&pdev->dev, "SAMSUNG_BSP_INSNREG00=READY stock=DMA_INCR_ONLY ohci_suspend_legacy=0\n");
	dev_dbg(&pdev->dev, "SAMSUNG_BSP_PORT_POWER=READY path=/sys/devices/platform/s5p-ehci/port_power cp_port=2\n");
	return 0;

fail_port_power_attr:
	device_remove_file(&pdev->dev, &dev_attr_port_power);
fail_power_attr:
	if (of_machine_is_compatible("samsung,t311"))
		pm_runtime_disable(&pdev->dev);
	exynos_ehci->power_on = false;
	if (of_machine_is_compatible("samsung,t311"))
		samsung_usb2_phy_t311_hcd_ready(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI, false);
	usb_remove_hcd(hcd);
fail_add_hcd:
	exynos_ehci_phy_disable(&pdev->dev);
fail_phy_init:
	exynos_ehci_phy_exit(&pdev->dev);
	pdev->dev.of_node = exynos_ehci->of_node;
fail_io:
	platform_set_drvdata(pdev, NULL);
	usb_put_hcd(hcd);
	return err;
}

static void exynos_ehci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);

	if (of_machine_is_compatible("samsung,t311")) {
#ifdef CONFIG_SAMSUNG_TAB3_XMM6262
		if (t311_ehci_cpuhp_state >= 0) {
			cpuhp_remove_state_nocalls(t311_ehci_cpuhp_state);
			t311_ehci_cpuhp_state = -1;
		}
		WRITE_ONCE(t311_ehci_irq, -1);
#endif
		WRITE_ONCE(t311_ehci_platform_dev, NULL);
	}

	if (exynos_ehci->cbd_compat_link)
		sysfs_remove_link(&platform_bus.kobj, "s5p-ehci");

	if (of_machine_is_compatible("samsung,t311")) {
		device_remove_file(&pdev->dev, &dev_attr_port_power);
	}
	device_remove_file(&pdev->dev, &dev_attr_ehci_power);
	pdev->dev.of_node = exynos_ehci->of_node;


	if (of_machine_is_compatible("samsung,t311"))
		pm_runtime_disable(&pdev->dev);

	if (exynos_ehci->power_on) {
		if (of_machine_is_compatible("samsung,t311"))
			samsung_usb2_phy_t311_hcd_ready(exynos_ehci->phy[0],
						 SAMSUNG_USB2_PHY_HOST_EHCI, false);
		usb_remove_hcd(hcd);
		exynos_ehci_phy_disable(&pdev->dev);
		exynos_ehci_phy_exit(&pdev->dev);
		exynos_ehci->power_on = false;
	}

	usb_put_hcd(hcd);
}

static int exynos_ehci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int rc;

	if (!exynos_ehci->power_on)
		return 0;

	if (of_machine_is_compatible("samsung,t311")) {
		rc = samsung_usb2_phy_t311_host_teardown_begin(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
		if (rc)
			return rc;
	}

	rc = ehci_suspend(hcd, do_wakeup);
	if (rc) {
		if (of_machine_is_compatible("samsung,t311"))
			samsung_usb2_phy_t311_host_teardown_end(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
		return rc;
	}

	exynos_ehci_phy_disable(dev);
	tab3_xmm6262_hsic_host_state(false);

	clk_disable_unprepare(exynos_ehci->clk);
	if (of_machine_is_compatible("samsung,t311"))
		samsung_usb2_phy_t311_host_teardown_end(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);

	return rc;
}

static int exynos_ehci_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci = to_exynos_ehci(hcd);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	bool t311 = of_machine_is_compatible("samsung,t311");
	bool lost_power;
	int ret;

	if (!exynos_ehci->power_on)
		return 0;

	ret = clk_prepare_enable(exynos_ehci->clk);
	if (ret)
		return ret;

	tab3_xmm6262_hsic_phy_init_notify();
	ret = exynos_ehci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exynos_ehci->clk);
		return ret;
	}


	if (t311)
		exynos_ehci_t311_wait_regs(dev, hcd);

	/* DMA burst Enable; T311 keeps Samsung's read-modify-write semantics. */
	if (t311) {
		u32 before = readl(EHCI_INSNREG00(hcd->regs));
		u32 after = before | EHCI_INSNREG00_ENABLE_DMA_BURST;

		writel(after, EHCI_INSNREG00(hcd->regs));
	} else {
		writel(EHCI_INSNREG00_ENABLE_DMA_BURST,
		       EHCI_INSNREG00(hcd->regs));
	}

	lost_power = ehci_readl(ehci, &ehci->regs->configured_flag) != FLAG_CF;
	ehci_resume(hcd, false);

	if (t311 && lost_power) {
		/* Stock calls s5p_wait_for_cp_resume only on lost-power restart. */
		exynos_ehci_t311_wait_cp_resume(dev, hcd);
		if (hcd->self.root_hub)
			pm_runtime_mark_last_busy(&hcd->self.root_hub->dev);
	}

	dev_dbg(dev,
		 "SAMSUNG_BSP_RESUME lost_power=%u cp_connect_wait=%u\n",
		 lost_power, t311 && lost_power);
	return 0;
}

static int exynos_ehci_runtime_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci;

	if (!hcd || !of_machine_is_compatible("samsung,t311"))
		return 0;
	exynos_ehci = to_exynos_ehci(hcd);
	if (!exynos_ehci->power_on)
		return 0;

	return samsung_usb2_phy_runtime_suspend(exynos_ehci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_EHCI);
}

static int exynos_ehci_runtime_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ehci_hcd *exynos_ehci;
	int ret;

	if (!hcd || !of_machine_is_compatible("samsung,t311"))
		return 0;
	exynos_ehci = to_exynos_ehci(hcd);
	if (!exynos_ehci->power_on || dev->power.is_suspended)
		return 0;

	ret = samsung_usb2_phy_runtime_resume(exynos_ehci->phy[0],
					      SAMSUNG_USB2_PHY_HOST_EHCI);
	if (ret < 0)
		return ret;
	if (ret > 0) {
		ret = ehci_resume(hcd, true);
		if (ret < 0)
			return ret;
		exynos_ehci_t311_wait_cp_resume(dev, hcd);
	}

	return 0;
}

/*
 * Controlled v169 A/B proved that driver-core platform unbind, not merely
 * usb_remove_hcd(), is the reliable precondition for T311 reboot after XMM
 * has been active.  Detach OHCI first and EHCI second, exactly like the
 * successful sysfs-unbind experiment, while still in reboot-notifier process
 * context and before the generic device_shutdown() walk.
 */
int tab3_t311_usb_platform_drivers_detach(void)
{
	struct device *dev;
	int ohci_ret;
	int ehci_ret = 0;

	ohci_ret = tab3_t311_ohci_platform_driver_detach();

	dev = READ_ONCE(t311_ehci_platform_dev);
	if (dev) {
		get_device(dev);
		if (READ_ONCE(dev->driver)) {
			dev_emerg(dev, "TAB3_V170_PLATFORM_UNBIND_EHCI start driver=%s runtime=%d\n",
				  dev->driver->name, dev->power.runtime_status);
			device_driver_detach(dev);
			if (READ_ONCE(dev->driver))
				ehci_ret = -EBUSY;
			dev_emerg(dev, "TAB3_V170_PLATFORM_UNBIND_EHCI done ret=%d bound=%u\n",
				  ehci_ret, !!READ_ONCE(dev->driver));
		}
		put_device(dev);
	}

	return ohci_ret ? ohci_ret : ehci_ret;
}
EXPORT_SYMBOL_GPL(tab3_t311_usb_platform_drivers_detach);

int tab3_t311_usb_platform_drivers_prepare_active(void)
{
	struct device *dev;
	int before;
	int ehci_ret = 0;
	int ohci_ret;

	/*
	 * The v171 controlled A/B reproduced the reboot hang with both platform
	 * hosts runtime-suspended, then made the same reboot return reliably when
	 * EHCI followed by OHCI were forced active while the XMM runtime device was
	 * still ONLINE.  Keep that exact ordering here.  pm_runtime_forbid() holds
	 * an extra usage reference after the synchronous resume, so the later
	 * driver-core detach cannot perform the harmful resume -> suspend cycle
	 * around its own pm_runtime_get_sync()/pm_runtime_put_sync() pair.
	 */
	dev = READ_ONCE(t311_ehci_platform_dev);
	if (dev) {
		get_device(dev);
		if (READ_ONCE(dev->driver)) {
			before = dev->power.runtime_status;
			pm_runtime_forbid(dev);
			if (!pm_runtime_active(dev))
				ehci_ret = -EIO;

			dev_emerg(dev,
				  "TAB3_V172_PREACTIVE_EHCI before=%d after=%d ret=%d\n",
				  before, dev->power.runtime_status, ehci_ret);
		}
		put_device(dev);
	}

	ohci_ret = tab3_t311_ohci_runtime_prepare_active();
	return ehci_ret ? ehci_ret : ohci_ret;
}
EXPORT_SYMBOL_GPL(tab3_t311_usb_platform_drivers_prepare_active);

static void exynos_ehci_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ehci_hcd *exynos_ehci;

	if (!hcd)
		return;
	exynos_ehci = to_exynos_ehci(hcd);
	if (!exynos_ehci->power_on)
		return;
	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static const struct dev_pm_ops exynos_ehci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_ehci_suspend, exynos_ehci_resume)
	SET_RUNTIME_PM_OPS(exynos_ehci_runtime_suspend,
			   exynos_ehci_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id exynos_ehci_match[] = {
	{ .compatible = "samsung,exynos4210-ehci" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_ehci_match);
#endif

static struct platform_driver exynos_ehci_driver = {
	.probe		= exynos_ehci_probe,
	.remove		= exynos_ehci_remove,
	.shutdown	= exynos_ehci_shutdown,
	.driver = {
		.name	= "exynos-ehci",
		.pm	= pm_ptr(&exynos_ehci_pm_ops),
		.of_match_table = of_match_ptr(exynos_ehci_match),
	}
};
static const struct ehci_driver_overrides exynos_overrides __initconst = {
	.extra_priv_size = sizeof(struct exynos_ehci_hcd),
};

static int __init ehci_exynos_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ehci_init_driver(&exynos_ehci_hc_driver, &exynos_overrides);
	if (of_machine_is_compatible("samsung,t311")) {
		exynos_ehci_hc_driver.flags &= ~HCD_BH;
		/*
		 * The platform unbind sysfs file cannot reject removal of a
		 * HOST PHY carrying a MAX77693 5V lease. Reboot's existing
		 * kernel-internal ordered detach path is NOT this sysfs ABI.
		 */
		exynos_ehci_driver.driver.suppress_bind_attrs = true;
	}
	return platform_driver_register(&exynos_ehci_driver);
}
#if IS_BUILTIN(CONFIG_SAMSUNG_TAB3_XMM6262)
int __init ohci_exynos_init(void);

static int __init t311_exynos_usb_host_init(void)
{
	int ret;

	/* Stock T311 initializes EHCI before OHCI. */
	ret = ehci_exynos_init();
	if (ret)
		return ret;

	ret = ohci_exynos_init();
	if (ret)
		platform_driver_unregister(&exynos_ehci_driver);

	return ret;
}
module_init(t311_exynos_usb_host_init);
#else
module_init(ehci_exynos_init);
#endif

static void __exit ehci_exynos_cleanup(void)
{
	platform_driver_unregister(&exynos_ehci_driver);
}
module_exit(ehci_exynos_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:exynos-ehci");
MODULE_AUTHOR("Jingoo Han");
MODULE_AUTHOR("Joonyoung Shim");
MODULE_LICENSE("GPL v2");
