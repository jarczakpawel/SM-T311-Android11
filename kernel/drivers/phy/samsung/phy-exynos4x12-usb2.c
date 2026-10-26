// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung SoC USB 1.1/2.0 PHY driver - Exynos 4x12 support
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Kamil Debski <k.debski@samsung.com>
 */

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/samsung-tab3-xmm6262.h>
#include "phy-samsung-usb2.h"

/* Exynos USB PHY registers */

/* PHY power control */
#define EXYNOS_4x12_UPHYPWR			0x0

#define EXYNOS_4x12_UPHYPWR_PHY0_SUSPEND	BIT(0)
#define EXYNOS_4x12_UPHYPWR_PHY0_PWR		BIT(3)
#define EXYNOS_4x12_UPHYPWR_PHY0_OTG_PWR	BIT(4)
#define EXYNOS_4x12_UPHYPWR_PHY0_SLEEP		BIT(5)
#define EXYNOS_4x12_UPHYPWR_PHY0 ( \
	EXYNOS_4x12_UPHYPWR_PHY0_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_PHY0_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY0_OTG_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY0_SLEEP)

#define EXYNOS_4x12_UPHYPWR_PHY1_SUSPEND	BIT(6)
#define EXYNOS_4x12_UPHYPWR_PHY1_PWR		BIT(7)
#define EXYNOS_4x12_UPHYPWR_PHY1_SLEEP		BIT(8)
#define EXYNOS_4x12_UPHYPWR_PHY1 ( \
	EXYNOS_4x12_UPHYPWR_PHY1_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_PHY1_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY1_SLEEP)

#define EXYNOS_4x12_UPHYPWR_HSIC0_SUSPEND	BIT(9)
#define EXYNOS_4x12_UPHYPWR_HSIC0_PWR		BIT(10)
#define EXYNOS_4x12_UPHYPWR_HSIC0_SLEEP		BIT(11)
#define EXYNOS_4x12_UPHYPWR_HSIC0 ( \
	EXYNOS_4x12_UPHYPWR_HSIC0_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_HSIC0_PWR | \
	EXYNOS_4x12_UPHYPWR_HSIC0_SLEEP)

#define EXYNOS_4x12_UPHYPWR_HSIC1_SUSPEND	BIT(12)
#define EXYNOS_4x12_UPHYPWR_HSIC1_PWR		BIT(13)
#define EXYNOS_4x12_UPHYPWR_HSIC1_SLEEP		BIT(14)
#define EXYNOS_4x12_UPHYPWR_HSIC1 ( \
	EXYNOS_4x12_UPHYPWR_HSIC1_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_HSIC1_PWR | \
	EXYNOS_4x12_UPHYPWR_HSIC1_SLEEP)

/* PHY clock control */
#define EXYNOS_4x12_UPHYCLK			0x4

#define EXYNOS_4x12_UPHYCLK_PHYFSEL_MASK	(0x7 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_OFFSET	0
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_9MHZ6	(0x0 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_10MHZ	(0x1 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_12MHZ	(0x2 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_19MHZ2	(0x3 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_20MHZ	(0x4 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_24MHZ	(0x5 << 0)
#define EXYNOS_4x12_UPHYCLK_PHYFSEL_50MHZ	(0x7 << 0)

#define EXYNOS_3250_UPHYCLK_REFCLKSEL		(0x2 << 8)

#define EXYNOS_4x12_UPHYCLK_PHY0_ID_PULLUP	BIT(3)
#define EXYNOS_4x12_UPHYCLK_PHY0_COMMON_ON	BIT(4)
#define EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON	BIT(7)

#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_MASK	(0x7f << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_OFFSET  10
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_12MHZ	(0x24 << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_15MHZ	(0x1c << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_16MHZ	(0x1a << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_19MHZ2	(0x15 << 10)
#define EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_20MHZ	(0x14 << 10)

/* PHY reset control */
#define EXYNOS_4x12_UPHYRST			0x8

/* Samsung Exynos4x12 USB PHY analog tune registers. */
#define EXYNOS_4x12_PHY1_PHYTUNE		0x20
#define EXYNOS_4x12_PHY0_PHYTUNE		0x24
#define EXYNOS_4x12_SQRXTUNE_MASK		(0x7 << 11)
#define EXYNOS_4212_T311_PHY0_SQRXTUNE		(0x5 << 11)
#define EXYNOS_4212_T311_PHY1_SQRXTUNE		(0x3 << 11)

#define EXYNOS_4x12_URSTCON_PHY0		BIT(0)
#define EXYNOS_4x12_URSTCON_OTG_HLINK		BIT(1)
#define EXYNOS_4x12_URSTCON_OTG_PHYLINK		BIT(2)
#define EXYNOS_4x12_URSTCON_HOST_PHY		BIT(3)
/* The following bit defines are presented in the
 * order taken from the Exynos4412 reference manual.
 *
 * During experiments with the hardware and debugging
 * it was determined that the hardware behaves contrary
 * to the manual.
 *
 * The following bit values were chaned accordingly to the
 * results of real hardware experiments.
 */
#define EXYNOS_4x12_URSTCON_PHY1		BIT(4)
#define EXYNOS_4x12_URSTCON_HSIC0		BIT(6)
#define EXYNOS_4x12_URSTCON_HSIC1		BIT(5)
#define EXYNOS_4x12_URSTCON_HOST_LINK_ALL	BIT(7)
#define EXYNOS_4x12_URSTCON_HOST_LINK_P0	BIT(10)
#define EXYNOS_4x12_URSTCON_HOST_LINK_P1	BIT(9)
#define EXYNOS_4x12_URSTCON_HOST_LINK_P2	BIT(8)

/* Isolation, configured in the power management unit */
#define EXYNOS_4x12_USB_ISOL_OFFSET		0x704
#define EXYNOS_4x12_USB_ISOL_OTG		BIT(0)
#define EXYNOS_4x12_USB_ISOL_HSIC0_OFFSET	0x708
#define EXYNOS_4x12_USB_ISOL_HSIC0		BIT(0)
#define EXYNOS_4x12_USB_ISOL_HSIC1_OFFSET	0x70c
#define EXYNOS_4x12_USB_ISOL_HSIC1		BIT(0)

/* Mode switching SUB Device <-> Host */
#define EXYNOS_4x12_MODE_SWITCH_OFFSET		0x21c
#define EXYNOS_4x12_MODE_SWITCH_MASK		1
#define EXYNOS_4x12_MODE_SWITCH_DEVICE		0
#define EXYNOS_4x12_MODE_SWITCH_HOST		1

/*
 * Samsung stock setup-usb-phy.c drives XuhostOVERCUR inactive before
 * host-PHY init by enabling the ETC6PUD[15:14] pull-up. ETC6PUD lives in
 * the Exynos4x12 GPIO2 block at 0x11000000 + 0x228. Keep this exact stock
 * quirk T311-only; no generic Exynos board should inherit it.
 */
#define EXYNOS_4x12_T311_ETC6PUD_PA		0x11000228
#define EXYNOS_4x12_T311_ETC6PUD_OVERCUR_MASK	(0x3 << 14)

enum exynos4x12_phy_id {
	EXYNOS4x12_DEVICE,
	EXYNOS4x12_HOST,
	EXYNOS4x12_HSIC0,
	EXYNOS4x12_HSIC1,
	EXYNOS4x12_NUM_PHYS,
};

/*
 * exynos4x12_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static int exynos4x12_rate_to_clk(unsigned long rate, u32 *reg)
{
	/* EXYNOS_4x12_UPHYCLK_PHYFSEL_MASK */

	switch (rate) {
	case 9600 * KHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_9MHZ6;
		break;
	case 10 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_10MHZ;
		break;
	case 12 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_12MHZ;
		break;
	case 19200 * KHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_19MHZ2;
		break;
	case 20 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_20MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_24MHZ;
		break;
	case 50 * MHZ:
		*reg = EXYNOS_4x12_UPHYCLK_PHYFSEL_50MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exynos4x12_isol(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 offset;
	u32 mask;

	switch (inst->cfg->id) {
	case EXYNOS4x12_DEVICE:
	case EXYNOS4x12_HOST:
		offset = EXYNOS_4x12_USB_ISOL_OFFSET;
		mask = EXYNOS_4x12_USB_ISOL_OTG;
		break;
	case EXYNOS4x12_HSIC0:
		offset = EXYNOS_4x12_USB_ISOL_HSIC0_OFFSET;
		mask = EXYNOS_4x12_USB_ISOL_HSIC0;
		break;
	case EXYNOS4x12_HSIC1:
		offset = EXYNOS_4x12_USB_ISOL_HSIC1_OFFSET;
		mask = EXYNOS_4x12_USB_ISOL_HSIC1;
		break;
	default:
		return;
	}

	regmap_update_bits(drv->reg_pmu, offset, mask, on ? 0 : mask);
}

static void exynos4x12_setup_clk(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 clk;

	/*
	 * Exact LT01/T311 Samsung policy is a full PHYCLK assignment.
	 * Generic mainline preserves firmware HSIC_REFCLK bits via RMW.
	 */
	if (of_machine_is_compatible("samsung,t311")) {
		clk = drv->ref_reg_val << EXYNOS_4x12_UPHYCLK_PHYFSEL_OFFSET;
		clk &= ~EXYNOS_4x12_UPHYCLK_PHY0_COMMON_ON;
		clk |= EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON;
		writel(clk, drv->reg_phy + EXYNOS_4x12_UPHYCLK);

		dev_dbg(drv->dev,
			"PHYCLK_STOCK_T311 value=%08x ref=%08x hsic_refclk=%08x policy=FULL_ASSIGN\n",
			 readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK),
			 drv->ref_reg_val,
			 readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK) &
			 EXYNOS_4x12_UPHYCLK_HSIC_REFCLK_MASK);
		return;
	}

	clk = readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK);
	clk &= ~EXYNOS_4x12_UPHYCLK_PHYFSEL_MASK;

	if (drv->cfg->has_refclk_sel)
		clk = EXYNOS_3250_UPHYCLK_REFCLKSEL;

	clk |= drv->ref_reg_val << EXYNOS_4x12_UPHYCLK_PHYFSEL_OFFSET;
	clk |= EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON;
	writel(clk, drv->reg_phy + EXYNOS_4x12_UPHYCLK);
}

static void exynos4x12_phy_pwr(struct samsung_usb2_phy_instance *inst, bool on)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 rstbits = 0;
	u32 phypwr = 0;
	u32 rst;
	u32 pwr;

	switch (inst->cfg->id) {
	case EXYNOS4x12_DEVICE:
		phypwr =	EXYNOS_4x12_UPHYPWR_PHY0;
		rstbits =	EXYNOS_4x12_URSTCON_PHY0;
		break;
	case EXYNOS4x12_HOST:
		phypwr =	EXYNOS_4x12_UPHYPWR_PHY1;
		rstbits =	EXYNOS_4x12_URSTCON_HOST_PHY |
				EXYNOS_4x12_URSTCON_PHY1 |
				EXYNOS_4x12_URSTCON_HOST_LINK_P0;
		break;
	case EXYNOS4x12_HSIC0:
		phypwr =	EXYNOS_4x12_UPHYPWR_HSIC0;
		rstbits =	EXYNOS_4x12_URSTCON_HSIC0 |
				EXYNOS_4x12_URSTCON_HOST_LINK_P1;
		break;
	case EXYNOS4x12_HSIC1:
		phypwr =	EXYNOS_4x12_UPHYPWR_HSIC1;
		rstbits =	EXYNOS_4x12_URSTCON_HSIC1 |
				EXYNOS_4x12_URSTCON_HOST_LINK_P1;
		break;
	}

	if (on) {
		pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
		pwr &= ~phypwr;
		writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

		rst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);
		rst |= rstbits;
		writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
		udelay(10);
		rst &= ~rstbits;
		writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
		/* The following delay is necessary for the reset sequence to be
		 * completed */
		udelay(80);
	} else {
		pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
		pwr |= phypwr;
		writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	}
}

static void exynos4x12_power_on_int(struct samsung_usb2_phy_instance *inst)
{
	if (inst->int_cnt++ > 0)
		return;

	exynos4x12_setup_clk(inst);
	exynos4x12_isol(inst, 0);
	exynos4x12_phy_pwr(inst, 1);
}

/*
 * SM-T311 stock 3.4 treats Exynos4212 USB PHY20 as one physical domain
 * shared by DEVICE, EHCI and OHCI owners. Mainline exposes DEVICE/HOST/
 * HSIC0/HSIC1 as independent generic PHYs, which lets an EHCI HCD cycle
 * partially power-cycle only HSIC0 while the rest of the stock domain stays
 * alive. That is not a state the vendor driver can create.
 *
 * Keep the generic split model on every other board. On T311, ext_cnt is the
 * logical-owner bookkeeping and the physical domain is switched only on the
 * first owner and the last owner, matching usb_phy_control.usage semantics.
 */
#define EXYNOS_4x12_URSTCON_T311_STOCK_DEVICE	( \
	EXYNOS_4x12_URSTCON_PHY0 | \
	EXYNOS_4x12_URSTCON_OTG_HLINK | \
	EXYNOS_4x12_URSTCON_OTG_PHYLINK)
#define EXYNOS_4x12_URSTCON_T311_STOCK_HOST	0x000007f8
/*
 * Samsung LT01/T311 diverged from upstream 3.0.31 here on purpose:
 * upstream PHY0_NORMAL_MASK was 0x39, vendor changed it to 0x29 and kept
 * PHY0_OTG_DISABLE (BIT(4)) asserted while PHY20 is operational.
 *
 * Do not use the generic EXYNOS_4x12_UPHYPWR_PHY0 mask for the T311 shared
 * domain: it contains BIT(4) and would clear the vendor OTG-disable state.
 */
#define EXYNOS_4x12_UPHYPWR_T311_STOCK_PHY0_NORMAL	( \
	EXYNOS_4x12_UPHYPWR_PHY0_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_PHY0_PWR | \
	EXYNOS_4x12_UPHYPWR_PHY0_SLEEP)
#define EXYNOS_4x12_UPHYPWR_T311_STOCK_ALL	( \
	EXYNOS_4x12_UPHYPWR_T311_STOCK_PHY0_NORMAL | \
	EXYNOS_4x12_UPHYPWR_PHY1 | \
	EXYNOS_4x12_UPHYPWR_HSIC0 | \
	EXYNOS_4x12_UPHYPWR_HSIC1)


static bool t311_rearm_host_domain = true;
module_param(t311_rearm_host_domain, bool, 0644);

static struct samsung_usb2_phy_driver *tab3_t311_phy_diag_drv;

static u32 exynos4x12_t311_trace_meta(struct samsung_usb2_phy_driver *drv,
				      int users)
{
	u32 meta = users & 0xff;

	meta |= ((u32)drv->host_runtime_flags & 0xffff) << 8;
	if (drv->ohci_runtime_suspended)
		meta |= BIT(31);
	return meta;
}

static void exynos4x12_t311_trace_owner(
	struct samsung_usb2_phy_instance *inst, bool on, bool after, int users)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;

	WRITE_ONCE(tab3_t311_phy_diag_drv, drv);
	tab3_xmm6262_handoff_trace_event(
		after ? TAB3_XMM_HANDOFF_PHY_OWNER_AFTER :
			TAB3_XMM_HANDOFF_PHY_OWNER_BEFORE,
		on ? 1U : 0U, inst->cfg->id, (u32)inst->ext_cnt,
		exynos4x12_t311_trace_meta(drv, users));
}

static void exynos4x12_t311_trace_runtime(
	struct samsung_usb2_phy_driver *drv, unsigned int op,
	unsigned int owner, unsigned long flags_before, int ret)
{
	u32 a = op | (((u32)ret & 0xff) << 8);
	u32 d = (u32)drv->host_runtime_flags;

	WRITE_ONCE(tab3_t311_phy_diag_drv, drv);
	if (drv->ohci_runtime_suspended)
		d |= BIT(31);
	tab3_xmm6262_handoff_trace_event(TAB3_XMM_HANDOFF_PHY_HOST_RUNTIME,
					 a, owner, (u32)flags_before, d);
}

int tab3_t311_phy_diag_snapshot(struct tab3_xmm6262_phy_diag *diag)
{
	struct samsung_usb2_phy_driver *drv;
	int i;

	if (!diag)
		return -EINVAL;
	drv = READ_ONCE(tab3_t311_phy_diag_drv);
	if (!drv)
		return -ENODEV;

	diag->uphypwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	diag->uphyclk = readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK);
	diag->uphyrst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);
	diag->phy0_phytune = readl(drv->reg_phy + EXYNOS_4x12_PHY0_PHYTUNE);
	diag->phy1_phytune = readl(drv->reg_phy + EXYNOS_4x12_PHY1_PHYTUNE);
	for (i = 0; i < ARRAY_SIZE(diag->ext_cnt) &&
		    i < drv->cfg->num_phys; i++)
		diag->ext_cnt[i] = drv->instances[i].ext_cnt;
	diag->host_runtime_flags = (u32)drv->host_runtime_flags;
	diag->ohci_runtime_suspended = drv->ohci_runtime_suspended;
	diag->host_hcd_ready_flags = (u32)drv->t311_hcd_ready_flags;
	diag->mode_switch_errno = regmap_read(drv->reg_sys,
			EXYNOS_4x12_MODE_SWITCH_OFFSET, &diag->mode_switch);
	return 0;
}
EXPORT_SYMBOL_GPL(tab3_t311_phy_diag_snapshot);

static int exynos4x12_t311_shared_users(struct samsung_usb2_phy_driver *drv)
{
	bool device_owner;
	bool host_owner;

	/*
	 * Exact Samsung PHY20 usage semantics: DEVICE is one owner and the
	 * complete EHCI/OHCI host block is one owner.  Mainline exposes HOST,
	 * HSIC0 and HSIC1 as separate generic PHY handles, but those three
	 * handles must not become three physical-domain usage bits on T311.
	 *
	 * A nested generic HOST reference is used by the OHCI compatibility
	 * shim.  The generic PHY core keeps the provider HOST ext_cnt asserted
	 * until both EHCI and logical-OHCI references are gone, reproducing the
	 * stock OR(EHCI, OHCI) decision without enabling the unsafe legacy HCD.
	 */
	device_owner = drv->instances[EXYNOS4x12_DEVICE].ext_cnt > 0;
	host_owner = drv->instances[EXYNOS4x12_HOST].ext_cnt > 0 ||
		     drv->instances[EXYNOS4x12_HSIC0].ext_cnt > 0 ||
		     drv->instances[EXYNOS4x12_HSIC1].ext_cnt > 0;

	return device_owner + host_owner;
}

static void exynos4x12_t311_stock_two_stage_reset(
		struct samsung_usb2_phy_driver *drv)
{
	u32 rst, rst_assert;

	/* Stock: PHY0 + OTG link/PHY-link first (0x7). */
	rst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);
	rst_assert = rst | EXYNOS_4x12_URSTCON_T311_STOCK_DEVICE;
	writel(rst_assert, drv->reg_phy + EXYNOS_4x12_UPHYRST);
	udelay(10);
	rst = rst_assert & ~EXYNOS_4x12_URSTCON_T311_STOCK_DEVICE;
	writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);

	/* Then the complete host/HSIC link domain (0x7f8). */
	rst_assert = rst | EXYNOS_4x12_URSTCON_T311_STOCK_HOST;
	writel(rst_assert, drv->reg_phy + EXYNOS_4x12_UPHYRST);
	udelay(10);
	rst = rst_assert & ~EXYNOS_4x12_URSTCON_T311_STOCK_HOST;
	writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
	udelay(80);
}

static int exynos4x12_t311_prepare(struct samsung_usb2_phy_driver *drv)
{
	if (!of_machine_is_compatible("samsung,t311"))
		return 0;

	drv->t311_etc6pud = devm_ioremap(drv->dev,
		EXYNOS_4x12_T311_ETC6PUD_PA, sizeof(u32));
	if (!drv->t311_etc6pud) {
		dev_err(drv->dev, "T311_ETC6PUD map failed phys=%08x\n",
			EXYNOS_4x12_T311_ETC6PUD_PA);
		return -ENOMEM;
	}
	return 0;
}

static void exynos4x12_t311_stock_overcurrent_pullup(
		struct samsung_usb2_phy_driver *drv)
{
	void __iomem *etc6pud;
	u32 before, after;

	etc6pud = drv->t311_etc6pud;
	if (!etc6pud)
		return;

	before = readl(etc6pud);
	after = (before & ~EXYNOS_4x12_T311_ETC6PUD_OVERCUR_MASK) |
		EXYNOS_4x12_T311_ETC6PUD_OVERCUR_MASK;
	writel(after, etc6pud);
	after = readl(etc6pud);

	dev_dbg(drv->dev,
		"T311_ETC6PUD before=%08x after=%08x overcur_pull=%u policy=STOCK_PULLUP\n",
		 before, after,
		 (after & EXYNOS_4x12_T311_ETC6PUD_OVERCUR_MASK) >> 14);

}

static void exynos4x12_t311_shared_domain_on(
		struct samsung_usb2_phy_driver *drv)
{
	u32 pwr;

	/* Exact Samsung exynos4_usb_phy20_init() physical sequence. */
	exynos4x12_t311_stock_overcurrent_pullup(drv);

	exynos4x12_isol(&drv->instances[EXYNOS4x12_DEVICE], 0);
	exynos4x12_isol(&drv->instances[EXYNOS4x12_HSIC0], 0);
	exynos4x12_isol(&drv->instances[EXYNOS4x12_HSIC1], 0);

	if (drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys,
				   EXYNOS_4x12_MODE_SWITCH_OFFSET,
				   EXYNOS_4x12_MODE_SWITCH_MASK,
				   EXYNOS_4x12_MODE_SWITCH_HOST);

	exynos4x12_setup_clk(&drv->instances[EXYNOS4x12_DEVICE]);

	/* Stock: DEVICE normal first, then HOST + HSIC0 + HSIC1. */
	pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	pwr &= ~EXYNOS_4x12_UPHYPWR_T311_STOCK_PHY0_NORMAL;
	writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

	pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	pwr &= ~(EXYNOS_4x12_UPHYPWR_PHY1 |
		 EXYNOS_4x12_UPHYPWR_HSIC0 |
		 EXYNOS_4x12_UPHYPWR_HSIC1);
	writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

	exynos4x12_t311_stock_two_stage_reset(drv);
}

/*
 * Samsung BSP contract: when another PHY20 owner remains active,
 * stock T311 only updates the logical usage mask.  It does not locally
 * power-cycle or reset HSIC0.  Physical PHY20 sequencing is therefore
 * confined to the first-owner/last-owner transitions below.
 */
static void exynos4x12_t311_shared_domain_off(
		struct samsung_usb2_phy_driver *drv)
{
	u32 pwr;

	/* Exact Samsung exynos4_usb_phy20_exit() physical sequence. */
	pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	pwr |= EXYNOS_4x12_UPHYPWR_T311_STOCK_PHY0_NORMAL;
	writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

	pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	pwr |= EXYNOS_4x12_UPHYPWR_PHY1 |
	       EXYNOS_4x12_UPHYPWR_HSIC0 |
	       EXYNOS_4x12_UPHYPWR_HSIC1;
	writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

	exynos4x12_isol(&drv->instances[EXYNOS4x12_DEVICE], 1);
	exynos4x12_isol(&drv->instances[EXYNOS4x12_HSIC0], 1);
	exynos4x12_isol(&drv->instances[EXYNOS4x12_HSIC1], 1);
}

/*
 * Native CBD handoff uses the stock DEVICE+HOST shared ownership model.
 * No per-HSIC0 physical OFF/ON is permitted while another PHY20 owner remains.
 */
static void exynos4x12_t311_stock_device_tune(
		struct samsung_usb2_phy_driver *drv)
{
	u32 before, value, after;

	/*
	 * SM-T311 is Exynos4212, not Exynos4412.  Samsung's generic
	 * set_exynos_usb_phy_tune() therefore leaves PHY1 at its reset
	 * SQRXTUNE=3 value, while mach-tab3 applies the board-specific
	 * PHY0 SQRXTUNE=5 (-10%) through s5p_usb_phy0_tune().
	 */
	before = readl(drv->reg_phy + EXYNOS_4x12_PHY0_PHYTUNE);
	value = before & ~EXYNOS_4x12_SQRXTUNE_MASK;
	value |= EXYNOS_4212_T311_PHY0_SQRXTUNE;
	udelay(10);
	writel(value, drv->reg_phy + EXYNOS_4x12_PHY0_PHYTUNE);
	after = readl(drv->reg_phy + EXYNOS_4x12_PHY0_PHYTUNE);

	dev_dbg(drv->dev,
		"T311_STOCK_PHY0_TUNE before=%08x write=%08x after=%08x sqrxtune=%u policy=SAMSUNG_EXYNOS4212_TAB3_DEVICE_INIT\n",
		 before, value, after,
		 (after & EXYNOS_4x12_SQRXTUNE_MASK) >> 11);
}

static void exynos4x12_t311_stock_host_tune(
		struct samsung_usb2_phy_driver *drv)
{
	u32 before, value, after;

	/*
	 * Exact working SM-T311/Exynos4212 runtime measures PHY1_PHYTUNE
	 * as 0x002819b3 (SQRXTUNE=3).  The modern PHY20 reset path leaves
	 * this field at SQRXTUNE=6 unless we restore the Samsung value.
	 * Preserve every unrelated analog bit and change only [13:11].
	 */
	before = readl(drv->reg_phy + EXYNOS_4x12_PHY1_PHYTUNE);
	value = before & ~EXYNOS_4x12_SQRXTUNE_MASK;
	value |= EXYNOS_4212_T311_PHY1_SQRXTUNE;
	if (value != before) {
		udelay(10);
		writel(value, drv->reg_phy + EXYNOS_4x12_PHY1_PHYTUNE);
	}
	after = readl(drv->reg_phy + EXYNOS_4x12_PHY1_PHYTUNE);

	dev_dbg(drv->dev,
		"T311_STOCK_PHY1_TUNE before=%08x write=%08x after=%08x sqrxtune=%u policy=WORKING_STOCK_MMIO_PARITY\n",
		 before, value, after,
		 (after & EXYNOS_4x12_SQRXTUNE_MASK) >> 11);
}

static int exynos4x12_t311_host_claim(struct samsung_usb2_phy_instance *inst,
				     bool release)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	unsigned int mux;
	int ret;

	if (!of_machine_is_compatible("samsung,t311") ||
	    inst->cfg->id != EXYNOS4x12_HOST)
		return -EINVAL;
	if (release) {
		drv->t311_otg_host_claimed = false;
		return 0;
	}
	if (drv->t311_otg_host_claimed || drv->t311_host_teardown_flags)
		return -EBUSY;
	/* Never source OTG VBUS while DEVICE/DWC2 owns USB PHY0. */
	/*
	 * host_init() runs before usb_add_hcd(), so it cannot certify a
	 * working root hub. Require both companion HCDs registered, plus
	 * the EHCI runtime owner, before any external 5V may be sourced.
	 */
	if (drv->instances[EXYNOS4x12_DEVICE].ext_cnt != 0 ||
	    drv->instances[EXYNOS4x12_HOST].ext_cnt <= 0 ||
	    !(drv->host_runtime_flags & BIT(SAMSUNG_USB2_PHY_HOST_EHCI)) ||
	    (drv->t311_hcd_ready_flags &
	     (BIT(SAMSUNG_USB2_PHY_HOST_EHCI) |
	      BIT(SAMSUNG_USB2_PHY_HOST_OHCI))) !=
	     (BIT(SAMSUNG_USB2_PHY_HOST_EHCI) |
	      BIT(SAMSUNG_USB2_PHY_HOST_OHCI)))
		return -EBUSY;

	/* Read actual MODE_SWITCH, not just infer it from PHY owner counts. */
	ret = regmap_read(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET, &mux);
	if (ret)
		return ret;
	if ((mux & EXYNOS_4x12_MODE_SWITCH_MASK) !=
	    EXYNOS_4x12_MODE_SWITCH_HOST)
		return -EBUSY;

	drv->t311_otg_host_claimed = true;
	return 0;
}

static int exynos4x12_t311_power_on(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	int users_before, users;
	bool rearm_host = inst->cfg->id != EXYNOS4x12_DEVICE &&
		drv->instances[EXYNOS4x12_DEVICE].ext_cnt > 0 &&
		drv->instances[EXYNOS4x12_HOST].ext_cnt == 0 &&
		drv->instances[EXYNOS4x12_HSIC0].ext_cnt == 0 &&
		drv->instances[EXYNOS4x12_HSIC1].ext_cnt == 0;

	if (inst->cfg->id == EXYNOS4x12_DEVICE &&
	    drv->t311_otg_host_claimed)
		return -EBUSY;

	users_before = exynos4x12_t311_shared_users(drv);
	exynos4x12_t311_trace_owner(inst, true, false, users_before);
	inst->ext_cnt++;
	users = exynos4x12_t311_shared_users(drv);

	if (!users_before && users) {
		exynos4x12_t311_shared_domain_on(drv);
		exynos4x12_t311_stock_host_tune(drv);
	} else if (rearm_host && READ_ONCE(t311_rearm_host_domain) &&
		   tab3_xmm6262_claim_host_rearm()) {
		u32 rst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);

		writel(rst | EXYNOS_4x12_URSTCON_T311_STOCK_HOST,
		       drv->reg_phy + EXYNOS_4x12_UPHYRST);
		udelay(10);
		writel(rst & ~EXYNOS_4x12_URSTCON_T311_STOCK_HOST,
		       drv->reg_phy + EXYNOS_4x12_UPHYRST);
		udelay(80);
		exynos4x12_t311_stock_host_tune(drv);
		dev_info(drv->dev, "T311_HOST_REARM id=%u rst=%08x owners=DEVICE+HOST\n",
			 inst->cfg->id, rst);
	}

	/*
	 * Working stock MMIO parity requires PHY1 SQRXTUNE=3 after the
	 * shared-domain reset.  Restore it explicitly above because the
	 * modern reset path does not reproduce that reset value.
	 */

	/* Stock device wrapper changes host -> device mux after shared init. */
	if (inst->cfg->id == EXYNOS4x12_DEVICE && drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys,
				   EXYNOS_4x12_MODE_SWITCH_OFFSET,
				   EXYNOS_4x12_MODE_SWITCH_MASK,
				   EXYNOS_4x12_MODE_SWITCH_DEVICE);

	if (inst->cfg->id == EXYNOS4x12_DEVICE)
		exynos4x12_t311_stock_device_tune(drv);

	exynos4x12_t311_trace_owner(inst, true, true, users);

	dev_dbg(drv->dev,
		"T311_PHY20_SEQUENCE phase=owner-get id=%u ext=%d users=%d physical=%s\n",
		inst->cfg->id, inst->ext_cnt, users,
		!users_before && users ? "ON_FIRST_OWNER" : "UNCHANGED");

	dev_dbg(drv->dev,
		"T311_SHARED_PHY phase=owner-get id=%u ext=%d users=%d physical=%s\n",
		 inst->cfg->id, inst->ext_cnt, users,
		 !users_before && users ? "ON_FIRST_OWNER" : "UNCHANGED");

	return 0;
}

static int exynos4x12_t311_power_off(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	int users;

	if (inst->ext_cnt <= 0) {
		dev_warn(drv->dev,
			 "T311_SHARED_PHY phase=owner-put-underflow id=%u ext=%d\n",
			 inst->cfg->id, inst->ext_cnt);
		return -EINVAL;
	}

	if (inst->cfg->id == EXYNOS4x12_HOST &&
	    drv->t311_otg_host_claimed)
		return -EBUSY;

	users = exynos4x12_t311_shared_users(drv);
	exynos4x12_t311_trace_owner(inst, false, false, users);
	inst->ext_cnt--;

	users = exynos4x12_t311_shared_users(drv);
	if (!users)
		exynos4x12_t311_shared_domain_off(drv);

	/* Stock DEVICE wrapper switches to HOST after exynos4_usb_phy20_exit(). */
	if (inst->cfg->id == EXYNOS4x12_DEVICE && drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys,
				   EXYNOS_4x12_MODE_SWITCH_OFFSET,
				   EXYNOS_4x12_MODE_SWITCH_MASK,
				   EXYNOS_4x12_MODE_SWITCH_HOST);

	exynos4x12_t311_trace_owner(inst, false, true, users);

	dev_dbg(drv->dev,
		"SAMSUNG_BSP_PHY phase=owner-put id=%u ext=%d logical_users=%d local_hsic_cycle=NONE policy=STOCK_DEVICE_PLUS_HOST_USAGE\n",
		 inst->cfg->id, inst->ext_cnt, users);

	dev_dbg(drv->dev,
		"T311_SHARED_PHY phase=owner-put id=%u ext=%d users=%d physical=%s\n",
		 inst->cfg->id, inst->ext_cnt, users,
		 users ? "UNCHANGED" : "OFF_LAST_OWNER");

	return 0;
}

#define EXYNOS4212_T311_HOST_SUSPEND_MASK ( \
	EXYNOS_4x12_UPHYPWR_PHY1_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_HSIC0_SUSPEND | \
	EXYNOS_4x12_UPHYPWR_HSIC1_SUSPEND)

static int exynos4x12_t311_host_init(struct samsung_usb2_phy_driver *drv,
				    unsigned int owner)
{
	unsigned long flags_before = drv->host_runtime_flags;

	/*
	 * Only teardown_begin/end own t311_host_teardown_flags. Clearing
	 * the bit here would expose a new BOOST claim while a host teardown
	 * is still in flight. A live BOOST lease also forbids host reinit.
	 */
	if (drv->t311_otg_host_claimed)
		return -EBUSY;

	set_bit(owner, &drv->host_runtime_flags);
	if (owner == SAMSUNG_USB2_PHY_HOST_OHCI)
		drv->ohci_runtime_suspended = false;
	exynos4x12_t311_trace_runtime(drv, TAB3_XMM_PHY_RUNTIME_HOST_INIT,
				     owner, flags_before, 0);
	return 0;
}

static void exynos4x12_t311_host_exit(struct samsung_usb2_phy_driver *drv,
				     unsigned int owner)
{
	unsigned long flags_before = drv->host_runtime_flags;

	/*
	 * A forced driver-core detach may call host_exit even after the
	 * provider refused phy_power_off(-EBUSY). Do not falsely drop PHY
	 * ownership while MAX77693 may still source VBUS. This does NOT
	 * make forced detach safe: the HCD can already have been removed.
	 */
	if (drv->t311_otg_host_claimed) {
		dev_err(drv->dev,
			"T311_HOST_EXIT_BLOCKED owner=%u live OTG lease; HCD removal unsafe\n",
			owner);
		return;
	}
	clear_bit(owner, &drv->host_runtime_flags);
	if (owner == SAMSUNG_USB2_PHY_HOST_OHCI)
		drv->ohci_runtime_suspended = false;
	exynos4x12_t311_trace_runtime(drv, TAB3_XMM_PHY_RUNTIME_HOST_EXIT,
				     owner, flags_before, 0);
}

static int exynos4x12_t311_runtime_suspend(struct samsung_usb2_phy_driver *drv,
					 unsigned int owner)
{
	unsigned long flags_before;
	u32 clk, pwr;

	if (!of_machine_is_compatible("samsung,t311"))
		return 0;

	if (drv->t311_otg_host_claimed)
		return -EBUSY;

	flags_before = drv->host_runtime_flags;
	clear_bit(owner, &drv->host_runtime_flags);
	if (owner == SAMSUNG_USB2_PHY_HOST_OHCI)
		drv->ohci_runtime_suspended = true;
	if (drv->host_runtime_flags) {
		exynos4x12_t311_trace_runtime(drv, TAB3_XMM_PHY_RUNTIME_SUSPEND,
					     owner, flags_before, 0);
		return 0;
	}

	if (drv->ohci_runtime_suspended) {
		clk = readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK);
		clk &= ~EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON;
		writel(clk, drv->reg_phy + EXYNOS_4x12_UPHYCLK);
	}

	pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	pwr |= EXYNOS4212_T311_HOST_SUSPEND_MASK;
	writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	exynos4x12_t311_trace_runtime(drv, TAB3_XMM_PHY_RUNTIME_SUSPEND,
				     owner, flags_before, 0);
	return 0;
}


static int exynos4x12_t311_runtime_resume(struct samsung_usb2_phy_driver *drv,
					unsigned int owner)
{
	unsigned long flags_before;
	u32 clk, pwr;
	int ret = 0;

	if (!of_machine_is_compatible("samsung,t311"))
		return 0;

	flags_before = drv->host_runtime_flags;
	if (!drv->host_runtime_flags) {
		clk = readl(drv->reg_phy + EXYNOS_4x12_UPHYCLK);
		clk |= EXYNOS_4x12_UPHYCLK_PHY1_COMMON_ON;
		writel(clk, drv->reg_phy + EXYNOS_4x12_UPHYCLK);

		pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
		if (pwr & EXYNOS_4x12_UPHYPWR_PHY1_PWR) {
			u32 rst;

			exynos4x12_isol(&drv->instances[EXYNOS4x12_DEVICE], 0);
			exynos4x12_isol(&drv->instances[EXYNOS4x12_HSIC0], 0);
			exynos4x12_isol(&drv->instances[EXYNOS4x12_HSIC1], 0);

			pwr &= ~EXYNOS_4x12_UPHYPWR_T311_STOCK_PHY0_NORMAL;
			writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

			rst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);
			rst |= EXYNOS_4x12_URSTCON_PHY0;
			writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
			udelay(10);
			rst &= ~EXYNOS_4x12_URSTCON_PHY0;
			writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);

			pwr = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
			pwr &= ~(EXYNOS_4x12_UPHYPWR_PHY1 |
				 EXYNOS_4x12_UPHYPWR_HSIC0 |
				 EXYNOS_4x12_UPHYPWR_HSIC1);
			writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);

			rst = readl(drv->reg_phy + EXYNOS_4x12_UPHYRST);
			rst |= EXYNOS_4x12_URSTCON_T311_STOCK_HOST;
			writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
			udelay(10);
			rst &= ~EXYNOS_4x12_URSTCON_T311_STOCK_HOST;
			writel(rst, drv->reg_phy + EXYNOS_4x12_UPHYRST);
			ret = 1;
		} else {
			pwr &= ~EXYNOS4212_T311_HOST_SUSPEND_MASK;
			writel(pwr, drv->reg_phy + EXYNOS_4x12_UPHYPWR);
		}
		udelay(80);
	}

	set_bit(owner, &drv->host_runtime_flags);
	if (owner == SAMSUNG_USB2_PHY_HOST_OHCI)
		drv->ohci_runtime_suspended = false;
	exynos4x12_t311_trace_runtime(drv, TAB3_XMM_PHY_RUNTIME_RESUME,
				     owner, flags_before, ret);
	return ret;
}

static void exynos4x12_t311_stock_powered_resume(
		struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;
	u32 before, after;

	if (!of_machine_is_compatible("samsung,t311") ||
	    inst->cfg->id != EXYNOS4x12_HSIC1)
		return;
	if (drv->instances[EXYNOS4x12_HOST].ext_cnt <= 1 ||
	    drv->instances[EXYNOS4x12_HSIC0].ext_cnt <= 1 ||
	    drv->instances[EXYNOS4x12_HSIC1].ext_cnt <= 1)
		return;

	before = readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	after = before & ~EXYNOS4212_T311_HOST_SUSPEND_MASK;
	writel(after, drv->reg_phy + EXYNOS_4x12_UPHYPWR);
	udelay(80);
	dev_dbg(drv->dev,
		 "PHY_STOCK_POWERED_RESUME before=%08x after=%08x mask=%08x delay_us=80\n",
		 before, readl(drv->reg_phy + EXYNOS_4x12_UPHYPWR),
		 (u32)EXYNOS4212_T311_HOST_SUSPEND_MASK);
}

static int exynos4x12_power_on(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;

	if (of_machine_is_compatible("samsung,t311"))
		return exynos4x12_t311_power_on(inst);

	if (inst->ext_cnt++ > 0) {
		exynos4x12_t311_stock_powered_resume(inst);
		return 0;
	}

	if (inst->cfg->id == EXYNOS4x12_HOST) {
		regmap_update_bits(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET,
						EXYNOS_4x12_MODE_SWITCH_MASK,
						EXYNOS_4x12_MODE_SWITCH_HOST);
		exynos4x12_power_on_int(&drv->instances[EXYNOS4x12_DEVICE]);
	}

	if (inst->cfg->id == EXYNOS4x12_DEVICE && drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET,
						EXYNOS_4x12_MODE_SWITCH_MASK,
						EXYNOS_4x12_MODE_SWITCH_DEVICE);

	if (inst->cfg->id == EXYNOS4x12_HSIC0 ||
		inst->cfg->id == EXYNOS4x12_HSIC1) {
		exynos4x12_power_on_int(&drv->instances[EXYNOS4x12_DEVICE]);
		exynos4x12_power_on_int(&drv->instances[EXYNOS4x12_HOST]);
	}

	exynos4x12_power_on_int(inst);

	return 0;
}

static void exynos4x12_power_off_int(struct samsung_usb2_phy_instance *inst)
{
	if (inst->int_cnt-- > 1)
		return;

	exynos4x12_isol(inst, 1);
	exynos4x12_phy_pwr(inst, 0);
}

static int exynos4x12_power_off(struct samsung_usb2_phy_instance *inst)
{
	struct samsung_usb2_phy_driver *drv = inst->drv;

	if (of_machine_is_compatible("samsung,t311"))
		return exynos4x12_t311_power_off(inst);

	if (inst->ext_cnt-- > 1)
		return 0;

	if (inst->cfg->id == EXYNOS4x12_DEVICE && drv->cfg->has_mode_switch)
		regmap_update_bits(drv->reg_sys, EXYNOS_4x12_MODE_SWITCH_OFFSET,
						EXYNOS_4x12_MODE_SWITCH_MASK,
						EXYNOS_4x12_MODE_SWITCH_HOST);

	if (inst->cfg->id == EXYNOS4x12_HOST)
		exynos4x12_power_off_int(&drv->instances[EXYNOS4x12_DEVICE]);

	if (inst->cfg->id == EXYNOS4x12_HSIC0 ||
		inst->cfg->id == EXYNOS4x12_HSIC1) {
		exynos4x12_power_off_int(&drv->instances[EXYNOS4x12_DEVICE]);
		exynos4x12_power_off_int(&drv->instances[EXYNOS4x12_HOST]);
	}

	exynos4x12_power_off_int(inst);

	return 0;
}


static const struct samsung_usb2_common_phy exynos4x12_phys[] = {
	{
		.label		= "device",
		.id		= EXYNOS4x12_DEVICE,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{
		.label		= "host",
		.id		= EXYNOS4x12_HOST,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{
		.label		= "hsic0",
		.id		= EXYNOS4x12_HSIC0,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
	{
		.label		= "hsic1",
		.id		= EXYNOS4x12_HSIC1,
		.power_on	= exynos4x12_power_on,
		.power_off	= exynos4x12_power_off,
	},
};

const struct samsung_usb2_phy_config exynos3250_usb2_phy_config = {
	.has_refclk_sel		= 1,
	.num_phys		= 1,
	.phys			= exynos4x12_phys,
	.rate_to_clk		= exynos4x12_rate_to_clk,
};

const struct samsung_usb2_phy_config exynos4x12_usb2_phy_config = {
	.has_mode_switch	= 1,
	.num_phys		= EXYNOS4x12_NUM_PHYS,
	.phys			= exynos4x12_phys,
	.rate_to_clk		= exynos4x12_rate_to_clk,
	.prepare		= exynos4x12_t311_prepare,
	.host_init		= exynos4x12_t311_host_init,
	.host_exit		= exynos4x12_t311_host_exit,
	.t311_host_claim	= exynos4x12_t311_host_claim,
	.runtime_suspend	= exynos4x12_t311_runtime_suspend,
	.runtime_resume		= exynos4x12_t311_runtime_resume,
};
