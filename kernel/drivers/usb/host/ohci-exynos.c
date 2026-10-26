// SPDX-License-Identifier: GPL-2.0+
/*
 * SAMSUNG EXYNOS USB HOST OHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-samsung-usb2.h>
#include <linux/samsung-tab3-xmm6262.h>
#include <linux/timekeeping.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI Exynos driver"

static struct hc_driver __read_mostly exynos_ohci_hc_driver;

#define to_exynos_ohci(hcd) (struct exynos_ohci_hcd *)(hcd_to_ohci(hcd)->priv)

#define PHY_NUMBER 3

struct exynos_ohci_hcd {
	struct clk *clk;
	struct device_node *of_node;
	struct phy *phy[PHY_NUMBER];
	bool legacy_phy;
	bool t311;
	bool hcd_added;
	bool phy_inited;
	bool phy_on;
	bool cbd_compat_link;
};

static struct device *t311_ohci_platform_dev;

static int exynos_ohci_get_phy(struct device *dev,
				struct exynos_ohci_hcd *exynos_ohci)
{
	struct phy *phy;
	int phy_number, num_phys;
	int ret;

	/* Get PHYs for the controller */
	num_phys = of_count_phandle_with_args(dev->of_node, "phys",
					      "#phy-cells");
	/* The PHY slots are fixed-size: reject malformed DT before writing. */
	if (num_phys > PHY_NUMBER)
		return -EINVAL;
	for (phy_number = 0; phy_number < num_phys; phy_number++) {
		phy = devm_of_phy_get_by_index(dev, dev->of_node, phy_number);
		if (IS_ERR(phy))
			return PTR_ERR(phy);
		exynos_ohci->phy[phy_number] = phy;
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
		exynos_ohci->phy[phy_number] = phy;
		if (IS_ERR(phy))
			return PTR_ERR(phy);
	}

	exynos_ohci->legacy_phy = true;
	return 0;
}

static int exynos_ohci_phy_init_t311(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	if (!exynos_ohci->t311)
		return 0;

	return phy_init(exynos_ohci->phy[0]);
}

static void exynos_ohci_phy_exit_t311(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	if (exynos_ohci->t311)
		phy_exit(exynos_ohci->phy[0]);
}

static int exynos_ohci_phy_enable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	int i;
	int ret = 0;

	/*
	 * Samsung T311 ohci-s5p owns only S5P_USB_PHY_HOST.  Do not let
	 * the generic Exynos OHCI glue acquire HSIC0/HSIC1 even if a future
	 * DT accidentally exposes additional PHY phandles.
	 */
	if (exynos_ohci->t311) {
		ret = samsung_usb2_phy_host_init(exynos_ohci->phy[0],
					SAMSUNG_USB2_PHY_HOST_OHCI);
		if (ret)
			return ret;
		ret = phy_power_on(exynos_ohci->phy[0]);
		if (ret)
			samsung_usb2_phy_host_exit(exynos_ohci->phy[0],
					   SAMSUNG_USB2_PHY_HOST_OHCI);
		return ret;
	}

	for (i = 0; ret == 0 && i < PHY_NUMBER; i++)
		ret = phy_power_on(exynos_ohci->phy[i]);
	if (ret)
		for (i--; i >= 0; i--)
			phy_power_off(exynos_ohci->phy[i]);

	return ret;
}

static void exynos_ohci_phy_disable(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	int i;

	if (exynos_ohci->t311) {
		phy_power_off(exynos_ohci->phy[0]);
		samsung_usb2_phy_host_exit(exynos_ohci->phy[0],
				   SAMSUNG_USB2_PHY_HOST_OHCI);
		return;
	}

	for (i = 0; i < PHY_NUMBER; i++)
		phy_power_off(exynos_ohci->phy[i]);
}

static ssize_t ohci_power_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	return sysfs_emit(buf, "OHCI Power %s\n",
			  exynos_ohci->hcd_added ? "on" : "off");
}

static int exynos_ohci_set_power(struct device *dev, bool power_on)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci;
	u64 t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
	int rh_before = -1, rh_after = -1;
	int dev_before = -1, dev_after = -1;
	int irq;
	int ret = 0;
	bool teardown_reserved = false;

	if (!hcd)
		return -ENODEV;
	exynos_ohci = to_exynos_ohci(hcd);

	device_lock(dev);

	if (!power_on) {
		if (!exynos_ohci->hcd_added && !exynos_ohci->phy_on)
			goto out;
		if (exynos_ohci->t311) {
			ret = samsung_usb2_phy_t311_host_teardown_begin(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
			if (ret)
				goto err;
			teardown_reserved = true;
		}

		if (exynos_ohci->t311) {
			t0 = ktime_get_ns();
			dev_before = dev->power.runtime_status;
			if (hcd->self.root_hub)
				rh_before = hcd->self.root_hub->dev.power.runtime_status;
		}
		if (hcd->self.root_hub)
			pm_runtime_forbid(&hcd->self.root_hub->dev);
		if (exynos_ohci->t311) {
			t1 = ktime_get_ns();
			if (hcd->self.root_hub)
				rh_after = hcd->self.root_hub->dev.power.runtime_status;
		}
		pm_runtime_forbid(dev);
		if (exynos_ohci->t311) {
			t2 = ktime_get_ns();
			dev_after = dev->power.runtime_status;
		}

		if (exynos_ohci->hcd_added) {
			exynos_ohci->hcd_added = false;
			if (exynos_ohci->t311)
				samsung_usb2_phy_t311_hcd_ready(exynos_ohci->phy[0],
							 SAMSUNG_USB2_PHY_HOST_OHCI, false);
			usb_remove_hcd(hcd);
		}
		if (exynos_ohci->t311)
			t3 = ktime_get_ns();
		if (exynos_ohci->phy_on) {
			exynos_ohci_phy_disable(dev);
			exynos_ohci->phy_on = false;
		}
		if (exynos_ohci->phy_inited) {
			exynos_ohci_phy_exit_t311(dev);
			exynos_ohci->phy_inited = false;
		}
		if (exynos_ohci->legacy_phy)
			pdev->dev.of_node = exynos_ohci->of_node;

		if (exynos_ohci->t311) {
			t4 = ktime_get_ns();
			dev_info(dev,
				 "T311_OHCI_OFF_TIMING rh_before=%d rh_after=%d dev_before=%d dev_after=%d rh_forbid_us=%llu dev_forbid_us=%llu remove_us=%llu phy_us=%llu total_us=%llu\n",
				 rh_before, rh_after, dev_before, dev_after,
				 (unsigned long long)div_u64(t1 - t0, 1000),
				 (unsigned long long)div_u64(t2 - t1, 1000),
				 (unsigned long long)div_u64(t3 - t2, 1000),
				 (unsigned long long)div_u64(t4 - t3, 1000),
				 (unsigned long long)div_u64(t4 - t0, 1000));
		}
		dev_dbg(dev, "XMM6262 OHCI off: HCD removed\n");
		goto out;
	}

	if (exynos_ohci->hcd_added) {
		if (exynos_ohci->t311) {
			ret = samsung_usb2_phy_t311_host_teardown_begin(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
			if (ret)
				goto err;
			teardown_reserved = true;
		}
		pm_runtime_forbid(dev);
		exynos_ohci->hcd_added = false;
		if (exynos_ohci->t311)
			samsung_usb2_phy_t311_hcd_ready(exynos_ohci->phy[0],
						 SAMSUNG_USB2_PHY_HOST_OHCI, false);
		usb_remove_hcd(hcd);
	}

	if (!exynos_ohci->phy_inited) {
		ret = exynos_ohci_phy_init_t311(dev);
		if (ret)
			goto err;
		exynos_ohci->phy_inited = true;
	}
	if (!exynos_ohci->phy_on) {
		ret = exynos_ohci_phy_enable(dev);
		if (ret)
			goto err_phy_exit;
		exynos_ohci->phy_on = true;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_phy_disable;
	}

	if (exynos_ohci->legacy_phy)
		pdev->dev.of_node = NULL;
	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_restore_node;
	if (exynos_ohci->t311)
		samsung_usb2_phy_t311_hcd_ready(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI, true);

	exynos_ohci->hcd_added = true;
	pm_runtime_allow(dev);
	dev_dbg(dev, "XMM6262 OHCI on: HCD added\n");
	goto out;

err_restore_node:
	if (exynos_ohci->legacy_phy)
		pdev->dev.of_node = exynos_ohci->of_node;
err_phy_disable:
	/* A failed HCD re-add must not leave the USB host PHY powered. */
	if (exynos_ohci->phy_on) {
		exynos_ohci_phy_disable(dev);
		exynos_ohci->phy_on = false;
	}
err_phy_exit:
	if (exynos_ohci->phy_inited && !exynos_ohci->phy_on) {
		exynos_ohci_phy_exit_t311(dev);
		exynos_ohci->phy_inited = false;
	}
err:
	if (teardown_reserved)
		samsung_usb2_phy_t311_host_teardown_end(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
	device_unlock(dev);
	return ret;
out:
	if (teardown_reserved)
		samsung_usb2_phy_t311_host_teardown_end(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
	device_unlock(dev);
	return 0;
}


int tab3_t311_ohci_platform_driver_detach(void)
{
	struct device *dev = READ_ONCE(t311_ohci_platform_dev);
	int ret = 0;

	if (!dev)
		return 0;

	get_device(dev);
	if (READ_ONCE(dev->driver)) {
		dev_emerg(dev, "TAB3_V170_PLATFORM_UNBIND_OHCI start driver=%s runtime=%d\n",
			  dev->driver->name, dev->power.runtime_status);
		device_driver_detach(dev);
		if (READ_ONCE(dev->driver))
			ret = -EBUSY;
		dev_emerg(dev, "TAB3_V170_PLATFORM_UNBIND_OHCI done ret=%d bound=%u\n",
			  ret, !!READ_ONCE(dev->driver));
	}
	put_device(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_t311_ohci_platform_driver_detach);

int tab3_t311_ohci_runtime_prepare_active(void)
{
	struct device *dev = READ_ONCE(t311_ohci_platform_dev);
	int before;
	int ret = 0;

	if (!dev)
		return 0;

	get_device(dev);
	if (!READ_ONCE(dev->driver))
		goto out;

	before = dev->power.runtime_status;
	pm_runtime_forbid(dev);
	if (!pm_runtime_active(dev))
		ret = -EIO;

	dev_emerg(dev,
		  "TAB3_V172_PREACTIVE_OHCI before=%d after=%d ret=%d\n",
		  before, dev->power.runtime_status, ret);
out:
	put_device(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_t311_ohci_runtime_prepare_active);

static ssize_t ohci_power_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci;
	int power_on;
	int ret;

	if (!hcd)
		return -ENODEV;
	exynos_ohci = to_exynos_ohci(hcd);

	ret = kstrtoint(buf, 0, &power_on);
	if (ret)
		return ret;

	ret = exynos_ohci_set_power(dev, !!power_on);
	return ret ? ret : count;
}
static DEVICE_ATTR(ohci_power, 0664, ohci_power_show, ohci_power_store);

static int exynos_ohci_probe(struct platform_device *pdev)
{
	struct exynos_ohci_hcd *exynos_ohci;
	struct usb_hcd *hcd;
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

	hcd = usb_create_hcd(&exynos_ohci_hc_driver,
				&pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	exynos_ohci = to_exynos_ohci(hcd);
	exynos_ohci->t311 = of_machine_is_compatible("samsung,t311");
	if (exynos_ohci->t311)
		hcd_to_ohci(hcd)->flags |= OHCI_QUIRK_T311_STOCK_RH;

	err = exynos_ohci_get_phy(&pdev->dev, exynos_ohci);
	if (err)
		goto fail_io;

	exynos_ohci->clk = devm_clk_get_enabled(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ohci->clk);
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
	exynos_ohci->of_node = pdev->dev.of_node;

	if (exynos_ohci->t311) {
		err = exynos_ohci_phy_init_t311(&pdev->dev);
		if (err) {
			dev_err(&pdev->dev, "Failed to initialize USB phy\n");
			goto fail_io;
		}
		exynos_ohci->phy_inited = true;
	}

	err = exynos_ohci_phy_enable(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable USB phy\n");
		goto fail_phy_init;
	}
	exynos_ohci->phy_on = true;

	/*
	 * Workaround: reset of_node pointer to avoid conflict between legacy
	 * Exynos OHCI port subnodes and generic USB device bindings
	 */
	if (exynos_ohci->legacy_phy)
		pdev->dev.of_node = NULL;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}
	if (exynos_ohci->t311)
		samsung_usb2_phy_t311_hcd_ready(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI, true);
	exynos_ohci->hcd_added = true;
	device_wakeup_enable(hcd->self.controller);

	if (exynos_ohci->t311) {
		err = device_create_file(&pdev->dev, &dev_attr_ohci_power);
		if (err)
			goto fail_power_attr;

		err = sysfs_create_link(&platform_bus.kobj, &pdev->dev.kobj,
					 "s5p-ohci");
		if (err) {
			dev_warn(&pdev->dev,
				 "OHCI CBD compatibility link unavailable: %d\n", err);
		} else {
			exynos_ohci->cbd_compat_link = true;
		}

		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
		dev_dbg(&pdev->dev,
			 "T311_OHCI_RUNTIME_PM=READY suspend=STOCK_PHY_FLAGS resume=STOCK_PHY_FLAGS handoff=CBD_DIRECT\n");
		dev_dbg(&pdev->dev,
			 "OHCI_STOCK_COMPANION=READY path=/sys/devices/platform/s5p-ohci/ohci_power target=%s\n",
			 dev_name(&pdev->dev));
		WRITE_ONCE(t311_ohci_platform_dev, &pdev->dev);
	}


	return 0;

fail_power_attr:
	exynos_ohci->hcd_added = false;
	if (exynos_ohci->t311)
		samsung_usb2_phy_t311_hcd_ready(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI, false);
	usb_remove_hcd(hcd);
fail_add_hcd:
	exynos_ohci_phy_disable(&pdev->dev);
	exynos_ohci->phy_on = false;
fail_phy_init:
	if (exynos_ohci->phy_inited) {
		exynos_ohci_phy_exit_t311(&pdev->dev);
		exynos_ohci->phy_inited = false;
	}
	pdev->dev.of_node = exynos_ohci->of_node;
fail_io:
	platform_set_drvdata(pdev, NULL);
	usb_put_hcd(hcd);
	return err;
}

static void exynos_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	if (exynos_ohci->t311)
		WRITE_ONCE(t311_ohci_platform_dev, NULL);

	if (exynos_ohci->t311) {
		pm_runtime_disable(&pdev->dev);
		if (exynos_ohci->cbd_compat_link)
			sysfs_remove_link(&platform_bus.kobj, "s5p-ohci");
		device_remove_file(&pdev->dev, &dev_attr_ohci_power);
	}

	pdev->dev.of_node = exynos_ohci->of_node;

	if (exynos_ohci->hcd_added) {
		if (exynos_ohci->t311)
			samsung_usb2_phy_t311_hcd_ready(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI, false);
		usb_remove_hcd(hcd);
	}

	if (exynos_ohci->phy_on)
		exynos_ohci_phy_disable(&pdev->dev);
	if (exynos_ohci->phy_inited)
		exynos_ohci_phy_exit_t311(&pdev->dev);

	usb_put_hcd(hcd);
}

static void exynos_ohci_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);

	if (!exynos_ohci->hcd_added)
		return;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static int exynos_ohci_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci = to_exynos_ohci(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int rc;

	if (!exynos_ohci->hcd_added)
		return 0;

	if (exynos_ohci->t311) {
		rc = samsung_usb2_phy_t311_host_teardown_begin(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
		if (rc)
			return rc;
	}

	rc = ohci_suspend(hcd, do_wakeup);
	if (rc) {
		if (exynos_ohci->t311)
			samsung_usb2_phy_t311_host_teardown_end(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
		return rc;
	}

	if (exynos_ohci->phy_on) {
		exynos_ohci_phy_disable(dev);
		exynos_ohci->phy_on = false;
	}

	clk_disable_unprepare(exynos_ohci->clk);
	if (exynos_ohci->t311)
		samsung_usb2_phy_t311_host_teardown_end(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);

	return 0;
}

static int exynos_ohci_resume(struct device *dev)
{
	struct usb_hcd *hcd			= dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci	= to_exynos_ohci(hcd);
	int ret;

	if (!exynos_ohci->hcd_added)
		return 0;

	ret = clk_prepare_enable(exynos_ohci->clk);
	if (ret)
		return ret;

	ret = exynos_ohci_phy_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable USB phy\n");
		clk_disable_unprepare(exynos_ohci->clk);
		return ret;
	}
	exynos_ohci->phy_on = true;

	ret = ohci_resume(hcd, false);
	if (ret) {
		exynos_ohci_phy_disable(dev);
		exynos_ohci->phy_on = false;
		clk_disable_unprepare(exynos_ohci->clk);
		return ret;
	}

	return 0;
}

/* Samsung 3.x s5p-ohci has platform runtime PM in addition to root-hub PM. */
static int exynos_ohci_runtime_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci;
	struct ohci_hcd *ohci;
	unsigned long flags;
	u32 irq_enabled;
	bool hw_accessible;
	int ret;

	if (!hcd)
		return 0;
	exynos_ohci = to_exynos_ohci(hcd);
	if (!exynos_ohci->t311 || !exynos_ohci->hcd_added)
		return 0;

	/* Exact Samsung 3.0.31 ohci_hcd_s5p_drv_runtime_suspend(). */
	ohci = hcd_to_ohci(hcd);
	spin_lock_irqsave(&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED && hcd->state != HC_STATE_HALT) {
		spin_unlock_irqrestore(&ohci->lock, flags);
		return 0;
	}

	irq_enabled = ohci_readl(ohci, &ohci->regs->intrenable);
	hw_accessible = test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&ohci->lock, flags);

	ret = samsung_usb2_phy_runtime_suspend(exynos_ohci->phy[0],
					 SAMSUNG_USB2_PHY_HOST_OHCI);
	if (ret) {
		spin_lock_irqsave(&ohci->lock, flags);
		if (hw_accessible)
			set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		if (irq_enabled & OHCI_INTR_MIE) {
			ohci_writel(ohci, OHCI_INTR_MIE,
				   &ohci->regs->intrenable);
			(void)ohci_readl(ohci, &ohci->regs->intrenable);
		}
		spin_unlock_irqrestore(&ohci->lock, flags);
	}
	return ret;
}

static int exynos_ohci_runtime_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct exynos_ohci_hcd *exynos_ohci;
	int ret;

	if (!hcd)
		return 0;
	exynos_ohci = to_exynos_ohci(hcd);
	if (!exynos_ohci->t311 || !exynos_ohci->hcd_added)
		return 0;

	if (dev->power.is_suspended)
		return 0;

	ret = samsung_usb2_phy_runtime_resume(exynos_ohci->phy[0],
					      SAMSUNG_USB2_PHY_HOST_OHCI);
	if (ret < 0)
		return ret;
	if (ret > 0)
		return ohci_resume(hcd, true);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_finish_controller_resume(hcd);

	return 0;
}

static const struct ohci_driver_overrides exynos_overrides __initconst = {
	.extra_priv_size =	sizeof(struct exynos_ohci_hcd),
};

static const struct dev_pm_ops exynos_ohci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_ohci_suspend, exynos_ohci_resume)
	SET_RUNTIME_PM_OPS(exynos_ohci_runtime_suspend,
			   exynos_ohci_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id exynos_ohci_match[] = {
	{ .compatible = "samsung,exynos4210-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_ohci_match);
#endif

static struct platform_driver exynos_ohci_driver = {
	.probe		= exynos_ohci_probe,
	.remove		= exynos_ohci_remove,
	.shutdown	= exynos_ohci_shutdown,
	.driver = {
		.name	= "exynos-ohci",
		.pm	= pm_ptr(&exynos_ohci_pm_ops),
		.of_match_table	= of_match_ptr(exynos_ohci_match),
	}
};

/* Called by the T311 combined host init when the modem stack is built-in. */
int __init ohci_exynos_init(void);

int __init ohci_exynos_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ohci_init_driver(&exynos_ohci_hc_driver, &exynos_overrides);
	/* T311: do not expose an unvetoable userspace HCD unbind path. */
	if (of_machine_is_compatible("samsung,t311"))
		exynos_ohci_driver.driver.suppress_bind_attrs = true;
	return platform_driver_register(&exynos_ohci_driver);
}
#if !IS_BUILTIN(CONFIG_SAMSUNG_TAB3_XMM6262)
module_init(ohci_exynos_init);
#endif

static void __exit ohci_exynos_cleanup(void)
{
	platform_driver_unregister(&exynos_ohci_driver);
}
module_exit(ohci_exynos_cleanup);

MODULE_ALIAS("platform:exynos-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("OHCI support for Samsung S5P/Exynos SoC Series");
MODULE_LICENSE("GPL v2");
