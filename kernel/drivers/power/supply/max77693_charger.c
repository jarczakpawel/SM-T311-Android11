// SPDX-License-Identifier: GPL-2.0+
//
// max77693_charger.c - Battery charger driver for the Maxim 77693
//
// Copyright (C) 2014 Samsung Electronics
// Krzysztof Kozlowski <krzk@kernel.org>

#include <linux/atomic.h>
#include <linux/devm-helpers.h>
#include <linux/extcon.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-samsung-usb2.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/samsung-tab3-xmm6262.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77693-private.h>

#define MAX77693_CHARGER_NAME				"max77693-charger"
#define MAX77693_DEFAULT_USB_CURRENT_UA		500000
#define MAX77693_DEFAULT_AC_CURRENT_UA		1800000
static const char *max77693_charger_model		= "MAX77693";
static const char *max77693_charger_manufacturer	= "Maxim Integrated";

static char *max77693_charger_supplied_to[] = {
	"max170xx_battery",
};

struct max77693_charger {
	struct device		*dev;
	struct max77693_dev	*max77693;
	struct power_supply	*charger;
	struct regulator	*charger_regulator;
	struct extcon_dev	*extcon;
	struct notifier_block	extcon_nb;
	struct work_struct	extcon_work;
	struct mutex		lock; /* protects charger state */
	bool			regulator_enabled;
#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	bool			otg_boost_on;
	/*
	 * Regmap can return an I2C error AFTER the PMIC has accepted part of
	 * a BOOST sequence.  Until OFF/CDET/IRQ restoration is confirmed,
	 * normal charger operation is strictly forbidden.
	 */
	bool			otg_cleanup_pending;
	bool			otg_orphan_boost_uncertain;
	bool			otg_shutdown;
	/* Two-phase ownership: never source 5V on raw MUIC attach alone. */
	bool			otg_user_armed;
	struct delayed_work	otg_watchdog_work;
	unsigned long		otg_lease_deadline;
	struct wakeup_source	*otg_wakeup_source;
	bool			otg_wake_held;
	/* A user or role-coordinator fault blocks 5V until HOST detach. */
	bool			otg_user_blocked;
	/*
	 * extcon notifier edges can collapse in schedule_work(): the
	 * charger worker may see HOST=1 after an OFF -> ON replug without
	 * ever observing HOST=0. Save the detachment independently.
	 * Never clear a fault or power VBUS from the notifier itself.
	 */
	bool			otg_host_seen;
	atomic_t		otg_host_detach_edges;
	unsigned int		otg_host_detach_ack;
	/* 0 clear, 1 I2C/PMIC, 2 USB/charger conflict, 3 cleanup,
	 * 4 source enable failed, 5 watchdog, 6 explicit stop, 7 BYP IRQ.
	 */
	u8			otg_block_reason;
	/*
	 * PHY1 lease must be acquired BEFORE any MAX77693 BOOST write.
	 * Even if I2C ACK is lost, retain lease until PMIC OFF is verified.
	 */
	struct phy		*otg_host_phy;
	bool			otg_host_claimed;
	/* Sticky BYP_I overcurrent: no further 5V until host cable detach. */
	bool			otg_overcurrent_latched;
	/* Set only after successful probe and BYP_I IRQ registration. */
	bool			otg_byp_irq_registered;
	struct notifier_block	otg_reboot_nb;
	u8			saved_otg_cdet;
#endif
	u32			active_current_limit;

	u32 constant_volt;
	u32 min_system_volt;
	u32 thermal_regulation_temp;
	u32 batttery_overcurrent;
	u32 charge_input_threshold_volt;
	u32 usb_current_limit;
	u32 ac_current_limit;
	u32 fast_charge_timer;
	u32 top_off_current;
	u32 top_off_timer;
};

static int max77693_get_charger_state(struct regmap *regmap, int *val)
{
	int ret;
	unsigned int data;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_DETAILS_01, &data);
	if (ret < 0)
		return ret;

	data &= CHG_DETAILS_01_CHG_MASK;
	data >>= CHG_DETAILS_01_CHG_SHIFT;

	switch (data) {
	case MAX77693_CHARGING_PREQUALIFICATION:
	case MAX77693_CHARGING_FAST_CONST_CURRENT:
	case MAX77693_CHARGING_FAST_CONST_VOLTAGE:
	case MAX77693_CHARGING_TOP_OFF:
	/* In high temp the charging current is reduced, but still charging */
	case MAX77693_CHARGING_HIGH_TEMP:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case MAX77693_CHARGING_DONE:
		*val = POWER_SUPPLY_STATUS_FULL;
		break;
	case MAX77693_CHARGING_TIMER_EXPIRED:
	case MAX77693_CHARGING_THERMISTOR_SUSPEND:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case MAX77693_CHARGING_OFF:
	case MAX77693_CHARGING_OVER_TEMP:
	case MAX77693_CHARGING_WATCHDOG_EXPIRED:
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case MAX77693_CHARGING_RESERVED:
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return 0;
}

static int max77693_get_charge_type(struct regmap *regmap, int *val)
{
	int ret;
	unsigned int data;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_DETAILS_01, &data);
	if (ret < 0)
		return ret;

	data &= CHG_DETAILS_01_CHG_MASK;
	data >>= CHG_DETAILS_01_CHG_SHIFT;

	switch (data) {
	case MAX77693_CHARGING_PREQUALIFICATION:
	/*
	 * Top-off: trickle or fast? In top-off the current varies between
	 * 100 and 250 mA. It is higher than prequalification current.
	 */
	case MAX77693_CHARGING_TOP_OFF:
		*val = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case MAX77693_CHARGING_FAST_CONST_CURRENT:
	case MAX77693_CHARGING_FAST_CONST_VOLTAGE:
	/* In high temp the charging current is reduced, but still charging */
	case MAX77693_CHARGING_HIGH_TEMP:
		*val = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	case MAX77693_CHARGING_DONE:
	case MAX77693_CHARGING_TIMER_EXPIRED:
	case MAX77693_CHARGING_THERMISTOR_SUSPEND:
	case MAX77693_CHARGING_OFF:
	case MAX77693_CHARGING_OVER_TEMP:
	case MAX77693_CHARGING_WATCHDOG_EXPIRED:
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case MAX77693_CHARGING_RESERVED:
	default:
		*val = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}

	return 0;
}

/*
 * Supported health statuses:
 *  - POWER_SUPPLY_HEALTH_DEAD
 *  - POWER_SUPPLY_HEALTH_GOOD
 *  - POWER_SUPPLY_HEALTH_OVERVOLTAGE
 *  - POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE
 *  - POWER_SUPPLY_HEALTH_UNKNOWN
 *  - POWER_SUPPLY_HEALTH_UNSPEC_FAILURE
 */
static int max77693_get_battery_health(struct regmap *regmap, int *val)
{
	int ret;
	unsigned int data;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_DETAILS_01, &data);
	if (ret < 0)
		return ret;

	data &= CHG_DETAILS_01_BAT_MASK;
	data >>= CHG_DETAILS_01_BAT_SHIFT;

	switch (data) {
	case MAX77693_BATTERY_NOBAT:
		*val = POWER_SUPPLY_HEALTH_DEAD;
		break;
	case MAX77693_BATTERY_PREQUALIFICATION:
	case MAX77693_BATTERY_GOOD:
	case MAX77693_BATTERY_LOWVOLTAGE:
		*val = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case MAX77693_BATTERY_TIMER_EXPIRED:
		/*
		 * Took longer to charge than expected, charging suspended.
		 * Damaged battery?
		 */
		*val = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
		break;
	case MAX77693_BATTERY_OVERVOLTAGE:
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;
	case MAX77693_BATTERY_OVERCURRENT:
		*val = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	case MAX77693_BATTERY_RESERVED:
	default:
		*val = POWER_SUPPLY_HEALTH_UNKNOWN;
		break;
	}

	return 0;
}

static int max77693_get_present(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	/*
	 * Read CHG_INT_OK register. High DETBAT bit here should be
	 * equal to value 0x0 in CHG_DETAILS_01/BAT field.
	 */
	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_INT_OK, &data);
	if (ret < 0)
		return ret;

	*val = (data & CHG_INT_OK_DETBAT_MASK) ? 0 : 1;

	return 0;
}

static int max77693_get_online(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_INT_OK, &data);
	if (ret < 0)
		return ret;

	*val = (data & CHG_INT_OK_CHGIN_MASK) ? 1 : 0;

	return 0;
}

/*
 * There are *two* current limit registers:
 * - CHGIN limit, which limits the input current from the external charger;
 * - Fast charge current limit, which limits the current going to the battery.
 */

static int max77693_get_input_current_limit(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_CNFG_09, &data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_09_CHGIN_ILIM_MASK;
	data >>= CHG_CNFG_09_CHGIN_ILIM_SHIFT;

	if (data <= 0x03)
		/* The first four values (0x00..0x03) are 60mA */
		*val = 60000;
	else
		*val = data * 20000; /* 20mA steps */

	return 0;
}

static int max77693_get_fast_charge_current(struct regmap *regmap, int *val)
{
	unsigned int data;
	int ret;

	ret = regmap_read(regmap, MAX77693_CHG_REG_CHG_CNFG_02, &data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_02_CC_MASK;
	data >>= CHG_CNFG_02_CC_SHIFT;

	*val = data * 33300; /* 33.3mA steps */

	return 0;
}

static int max77693_set_fast_charge_current(struct max77693_charger *chg,
					    unsigned int uamp)
{
	unsigned int max_sel = CHG_CNFG_02_CC_MASK >> CHG_CNFG_02_CC_SHIFT;
	unsigned int data;

	/* CHG_CNFG_02 uses 33.3 mA steps. Round down for safety. */
	data = min(uamp / 33300, max_sel);
	data <<= CHG_CNFG_02_CC_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
				  MAX77693_CHG_REG_CHG_CNFG_02,
				  CHG_CNFG_02_CC_MASK, data);
}

static bool max77693_extcon_active(struct max77693_charger *chg,
				   unsigned int cable)
{
	return extcon_get_state(chg->extcon, cable) > 0;
}

static int max77693_get_usb_type(struct max77693_charger *chg)
{
	if (max77693_extcon_active(chg, EXTCON_CHG_USB_DCP) ||
	    max77693_extcon_active(chg, EXTCON_CHG_USB_FAST))
		return POWER_SUPPLY_USB_TYPE_DCP;

	if (max77693_extcon_active(chg, EXTCON_CHG_USB_CDP))
		return POWER_SUPPLY_USB_TYPE_CDP;

	if (max77693_extcon_active(chg, EXTCON_CHG_USB_SDP) ||
	    max77693_extcon_active(chg, EXTCON_CHG_USB_SLOW))
		return POWER_SUPPLY_USB_TYPE_SDP;

	return POWER_SUPPLY_USB_TYPE_UNKNOWN;
}

static unsigned int max77693_get_cable_current(struct max77693_charger *chg)
{
	switch (max77693_get_usb_type(chg)) {
	case POWER_SUPPLY_USB_TYPE_DCP:
		return chg->ac_current_limit;
	case POWER_SUPPLY_USB_TYPE_CDP:
	case POWER_SUPPLY_USB_TYPE_SDP:
		return chg->usb_current_limit;
	default:
		return 0;
	}
}


#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
/*
 * SM-T311-only, opt-in proof-of-concept for the original Samsung MAX77693
 * charger boost OTG path. Charger lock must be held; never share CHG+BOOST.
 * This does NOT make the physical USB host data/connector path work by itself.
 */
static bool max77693_t311_otg_allowed(struct max77693_charger *chg)
{
	return of_machine_is_compatible("samsung,t311") &&
	       of_property_read_bool(chg->dev->of_node,
				     "t311,otg-boost-experimental");
}

/*
 * The nested regmap-irq domain owns CHG_INT_MASK bits 0..4.
 * Never write its hardware mask directly: that would desynchronize
 * regmap-irq's private mask_buf. Check physical BYP bit0 before BOOST.
 */
static int max77693_t311_byp_irq_ready(struct max77693_charger *chg)
{
	unsigned int mask;
	int ret;

	if (!READ_ONCE(chg->otg_byp_irq_registered))
		return -EBUSY;
	ret = regmap_read(chg->max77693->regmap,
			  MAX77693_CHG_REG_CHG_INT_MASK, &mask);
	if (ret)
		return ret;
	return (mask & CHG_IRQ_BYP_I) ? -EIO : 0;
}

static int max77693_t311_orphan_boost_off(struct max77693_charger *chg)
{
	unsigned int mode, cdet;
	const unsigned int unsafe = CHG_CNFG_00_OTG_MASK |
		CHG_CNFG_00_BOOST_MASK | CHG_CNFG_00_DIS_MUIC_CTRL_MASK;
	int ret;

	ret = regmap_read(chg->max77693->regmap,
			  MAX77693_CHG_REG_CHG_CNFG_00, &mode);
	if (ret)
		goto uncertain;
	if (!(mode & unsafe) && !chg->otg_orphan_boost_uncertain)
		return 0;

	if (!chg->otg_wake_held) {
		__pm_stay_awake(chg->otg_wakeup_source);
		chg->otg_wake_held = true;
	}
	chg->otg_orphan_boost_uncertain = true;
	chg->otg_user_armed = false;
	chg->otg_user_blocked = true;
	chg->otg_block_reason = 1;
	if (mode & unsafe) {
		dev_err(chg->dev, "orphan PMIC BOOST/OTG detected; forcing OFF\n");
		ret = regmap_update_bits(chg->max77693->regmap,
				MAX77693_CHG_REG_CHG_CNFG_00, unsafe, 0);
		if (ret)
			goto uncertain;
		ret = regmap_read(chg->max77693->regmap,
				MAX77693_CHG_REG_CHG_CNFG_00, &mode);
		if (ret)
			goto uncertain;
		if (mode & unsafe) {
			ret = -EIO;
			goto uncertain;
		}
	}
	ret = regmap_update_bits(chg->max77693->regmap_muic,
			MAX77693_MUIC_REG_CDETCTRL1,
			CDETCTRL1_CHGDETEN_MASK, CDETCTRL1_CHGDETEN_MASK);
	if (ret)
		goto uncertain;
	ret = regmap_read(chg->max77693->regmap_muic,
			MAX77693_MUIC_REG_CDETCTRL1, &cdet);
	if (ret)
		goto uncertain;
	if (!(cdet & CDETCTRL1_CHGDETEN_MASK)) {
		ret = -EIO;
		goto uncertain;
	}
	chg->otg_orphan_boost_uncertain = false;
	if (chg->otg_wake_held) {
		__pm_relax(chg->otg_wakeup_source);
		chg->otg_wake_held = false;
	}
	return 0;

uncertain:
	chg->otg_orphan_boost_uncertain = true;
	chg->otg_user_armed = false;
	chg->otg_user_blocked = true;
	chg->otg_block_reason = 1;
	if (!chg->otg_wake_held) {
		__pm_stay_awake(chg->otg_wakeup_source);
		chg->otg_wake_held = true;
	}
	mod_delayed_work(system_wq, &chg->otg_watchdog_work,
			 msecs_to_jiffies(1000));
	return ret;
}

static int max77693_t311_otg_disable(struct max77693_charger *chg)
{
	struct regmap *reg = chg->max77693->regmap;
	unsigned int mode, cdet;
	int ret;

	/*
	 * A lease without a pending/active session has no saved PMIC state
	 * we can safely restore. Never silently release ownership.
	 */
	if (!chg->otg_cleanup_pending && !chg->otg_boost_on) {
		if (chg->otg_host_claimed)
			return -EIO;
		return max77693_t311_orphan_boost_off(chg);
	}

	/*
	 * All attempts to stop BOOST invalidate the previous successful ON
	 * state. If ANY I2C operation/readback fails, never report ON as
	 * successful on a later HOST event: retry cleanup instead.
	 */
	chg->otg_cleanup_pending = true;
	chg->otg_boost_on = false;

	/* Stop VBUS before restoring charging or releasing the HOST lease. */
	ret = regmap_update_bits(reg, MAX77693_CHG_REG_CHG_CNFG_00,
			CHG_CNFG_00_OTG_MASK | CHG_CNFG_00_BOOST_MASK |
			CHG_CNFG_00_DIS_MUIC_CTRL_MASK | CHG_CNFG_00_CHG_MASK |
			CHG_CNFG_00_BUCK_MASK, CHG_CNFG_00_BUCK_MASK);
	if (ret)
		return ret;
	ret = regmap_read(reg, MAX77693_CHG_REG_CHG_CNFG_00, &mode);
	if (ret)
		return ret;
	if ((mode & (CHG_CNFG_00_OTG_MASK | CHG_CNFG_00_BOOST_MASK |
		     CHG_CNFG_00_DIS_MUIC_CTRL_MASK | CHG_CNFG_00_CHG_MASK)) ||
	    !(mode & CHG_CNFG_00_BUCK_MASK))
		return -EIO;

	/* Restore MUIC detection and verify the hardware register, not ACK. */
	ret = regmap_update_bits(chg->max77693->regmap_muic,
			MAX77693_MUIC_REG_CDETCTRL1,
			CDETCTRL1_CHGDETEN_MASK,
			chg->saved_otg_cdet & CDETCTRL1_CHGDETEN_MASK);
	if (ret)
		return ret;
	ret = regmap_read(chg->max77693->regmap_muic,
			  MAX77693_MUIC_REG_CDETCTRL1, &cdet);
	if (ret)
		return ret;
	if ((cdet & CDETCTRL1_CHGDETEN_MASK) !=
	    (chg->saved_otg_cdet & CDETCTRL1_CHGDETEN_MASK))
		return -EIO;

	/* CHG_INT_MASK remains under regmap-irq ownership through OFF. */
	/* Only after verified OFF+CDET may PHY0 re-enter. */
	chg->otg_cleanup_pending = false;
	if (chg->otg_host_claimed) {
		samsung_usb2_phy_t311_host_release(chg->otg_host_phy);
		chg->otg_host_claimed = false;
	}
	if (chg->otg_wake_held) {
		__pm_relax(chg->otg_wakeup_source);
		chg->otg_wake_held = false;
	}
	return 0;
}

/*
 * The ON acknowledgement must match the physical MAX77693 mode. Both
 * initial enable and subsequent USB_HOST events use this same predicate.
 * In particular, BUCK+BOOST must never be accepted as valid HOST mode.
 */
static bool max77693_t311_otg_mode_is_on(unsigned int mode)
{
	const unsigned int bits = CHG_CNFG_00_CHG_MASK |
				  CHG_CNFG_00_BUCK_MASK |
				  CHG_CNFG_00_OTG_MASK |
				  CHG_CNFG_00_BOOST_MASK |
				  CHG_CNFG_00_DIS_MUIC_CTRL_MASK;

	return (mode & bits) == (CHG_CNFG_00_OTG_MASK |
				  CHG_CNFG_00_BOOST_MASK |
				  CHG_CNFG_00_DIS_MUIC_CTRL_MASK);
}

static int max77693_t311_otg_enable(struct max77693_charger *chg)
{
	unsigned int cdet, conf, status2, current_limit_mode;
	int ret;

	if (chg->otg_cleanup_pending) {
		/*
		 * A prior successful ON is not proof that the PMIC stayed ON.
		 * Recheck after each HOST event. A lost read, BUCK/CHG conflict
		 * or a silently reset BOOST is a fault, never a cached success.
		 * Keep pending and PHY1 lease for verified OFF recovery.
		 */
		if (!chg->otg_boost_on)
			return -EIO;
		ret = regmap_read(chg->max77693->regmap,
				  MAX77693_CHG_REG_CHG_CNFG_00, &conf);
		if (ret || !max77693_t311_otg_mode_is_on(conf)) {
			chg->otg_boost_on = false;
			return ret ? ret : -EIO;
		}
		ret = max77693_t311_byp_irq_ready(chg);
		if (ret)
			chg->otg_boost_on = false;
		return ret;
	}

	/* Never start BOOST while regulator framework still owns charging. */
	if (chg->regulator_enabled || max77693_get_cable_current(chg))
		return -EBUSY;

	/*
	 * This Galaxy Tab 3 8.0 physically reports bit7=1 already, even
	 * while using the PC/MTP gadget path. Upstream MAX77693 OTG work
	 * by Artur Weber (v2/v4, tested with Tab 3 8.0) selects bit7
	 * for a nominal 900mA source-current option. Refusing bit7 made
	 * every OTG session fail with a proven-valid stock register value.
	 *
	 * Bits 6/7 form a current selection field, but values involving
	 * bit6 are not qualified for this board. Do not write or clear
	 * either bit here: preserve the original boot firmware setting.
	 * Reject bit6 and all I2C faults, and rely on BYP overcurrent IRQ,
	 * host PHY and USB upstream-input locks before starting 5V.
	 */
	ret = regmap_read(chg->max77693->regmap,
			 MAX77693_CHG_REG_CHG_CNFG_02,
			 &current_limit_mode);
	if (ret)
		return ret;
	if (current_limit_mode & CHG_CNFG_02_OTG_UNQUALIFIED_MASK)
		return -EOPNOTSUPP;

	/* No ordinary upstream VBUS/SDP cable may be connected. */
	ret = extcon_get_state(chg->extcon, EXTCON_USB);
	if (ret)
		return ret < 0 ? ret : -EBUSY;

	/* Refuse internal BOOST when external powered VBUS is already present. */
	ret = regmap_read(chg->max77693->regmap_muic,
			 MAX77693_MUIC_REG_STATUS2, &status2);
	if (ret)
		return ret;
	if (status2 & MAX77693_STATUS2_VBVOLT_MASK)
		return -EBUSY;

	ret = max77693_t311_byp_irq_ready(chg);
	if (ret)
		return ret;
	ret = regmap_read(chg->max77693->regmap_muic,
			 MAX77693_MUIC_REG_CDETCTRL1, &cdet);
	if (ret)
		return ret;
	ret = regmap_read(chg->max77693->regmap,
			 MAX77693_CHG_REG_CHG_CNFG_00, &conf);
	if (ret)
		return ret;
	/*
	 * Any preexisting BOOST/OTG/MUIC-disable ownership (boot firmware
	 * or another driver) is unknown to this state machine. Never claim
	 * that it belongs to us or allow charging to be paired with it.
	 */
	if (conf & (CHG_CNFG_00_CHG_MASK | CHG_CNFG_00_OTG_MASK |
		    CHG_CNFG_00_BOOST_MASK | CHG_CNFG_00_DIS_MUIC_CTRL_MASK))
		return -EBUSY;

	/*
	 * Do NOT source VBUS unless SYSREG MODE_SWITCH is physically HOST,
	 * HOST PHY1 owned and DEVICE PHY0 has zero owners. Lease, not switch.
	 */
	ret = samsung_usb2_phy_t311_host_claim(chg->otg_host_phy);
	if (ret)
		return ret;
	chg->otg_host_claimed = true;

	ret = extcon_get_state(chg->extcon, EXTCON_USB_HOST);
	if (ret != 1) {
		ret = ret < 0 ? ret : -ENOTCONN;
		goto release_unpowered_host;
	}
	ret = extcon_get_state(chg->extcon, EXTCON_USB);
	if (ret) {
		ret = ret < 0 ? ret : -EBUSY;
		goto release_unpowered_host;
	}
	ret = regmap_read(chg->max77693->regmap_muic,
			  MAX77693_MUIC_REG_STATUS2, &status2);
	if (ret)
		goto release_unpowered_host;
	if (status2 & MAX77693_STATUS2_VBVOLT_MASK) {
		ret = -EBUSY;
		goto release_unpowered_host;
	}
	if (chg->otg_shutdown || chg->otg_user_blocked ||
	    !chg->otg_user_armed || chg->otg_overcurrent_latched ||
	    time_after_eq(jiffies, chg->otg_lease_deadline)) {
		ret = -ECANCELED;
		goto release_unpowered_host;
	}

	chg->saved_otg_cdet = cdet;
	/*
	 * Mark cleanup BEFORE the first I2C write.  Even a reported failure
	 * can mean that PMIC accepted the mode switch but ACK was lost.
	 * On failure do not optimistically restore charging state.
	 */
	chg->otg_cleanup_pending = true;
	if (!chg->otg_wake_held) {
		__pm_stay_awake(chg->otg_wakeup_source);
		chg->otg_wake_held = true;
	}
	/*
	 * BYP_I is unmasked by the registered nested IRQ handler.
	 * Never directly manipulate CHG_INT_MASK; regmap-irq owns its
	 * cache and hardware sync, including the charger IRQ mask bits.
	 */
	ret = regmap_update_bits(chg->max77693->regmap_muic,
			 MAX77693_MUIC_REG_CDETCTRL1,
			 CDETCTRL1_CHGDETEN_MASK, 0);
	if (ret)
		return ret;
	ret = regmap_update_bits(chg->max77693->regmap,
			 MAX77693_CHG_REG_CHG_CNFG_00,
			 CHG_CNFG_00_CHG_MASK | CHG_CNFG_00_BUCK_MASK |
			 CHG_CNFG_00_OTG_MASK | CHG_CNFG_00_BOOST_MASK |
			 CHG_CNFG_00_DIS_MUIC_CTRL_MASK,
			 CHG_CNFG_00_OTG_MASK | CHG_CNFG_00_BOOST_MASK |
			 CHG_CNFG_00_DIS_MUIC_CTRL_MASK);
	if (!ret) {
		/* ACK alone cannot prove we own the requested ON mode. */
		ret = regmap_read(chg->max77693->regmap,
				  MAX77693_CHG_REG_CHG_CNFG_00, &conf);
		if (ret)
			return ret;
		if (!max77693_t311_otg_mode_is_on(conf))
			return -EIO;
		/* Recheck that the BYP interrupt remains physically enabled. */
		ret = max77693_t311_byp_irq_ready(chg);
		if (ret)
			return ret;
		chg->otg_boost_on = true;
		return 0;
	}

	/*
	 * A failed I2C write leaves actual BOOST state uncertain.  Don't
	 * assume best-effort undo succeeded; the next extcon work must
	 * complete the full fail-closed cleanup before enabling CHG.
	 */
	return ret;

release_unpowered_host:
	samsung_usb2_phy_t311_host_release(chg->otg_host_phy);
	chg->otg_host_claimed = false;
	return ret;
}

/* Run BEFORE normal charger current decisions. */
static bool max77693_t311_otg_handle(struct max77693_charger *chg)
{
	int host, ret;

	if (!max77693_t311_otg_allowed(chg))
		return false;

	/* A remove/shutdown or a failed I2C transaction blocks BOOST. */
	if (chg->otg_shutdown)
		return true;

	host = extcon_get_state(chg->extcon, EXTCON_USB_HOST);
	if (host == 1)
		WRITE_ONCE(chg->otg_host_seen, true);
	/*
	 * The extcon notifier records a *real* HOST falling edge even when
	 * the queued charger work later runs after HOST has reattached.
	 * Accept that edge only when the PMIC is verified OFF, no I2C/IRQ
	 * fault exists and there is no competing charger or external VBUS.
	 * This never arms BOOST: root-only lease refresh remains necessary.
	 */
	if (host == 1 && chg->otg_user_blocked &&
	    (unsigned int)atomic_read(&chg->otg_host_detach_edges) !=
					chg->otg_host_detach_ack &&
	    !chg->otg_overcurrent_latched &&
	    !chg->otg_orphan_boost_uncertain &&
	    !chg->otg_cleanup_pending && !chg->otg_boost_on) {
		unsigned int mode, input;
		int usb;

		ret = max77693_t311_otg_disable(chg);
		if (!ret)
			ret = regmap_read(chg->max77693->regmap,
					  MAX77693_CHG_REG_CHG_CNFG_00, &mode);
		usb = extcon_get_state(chg->extcon, EXTCON_USB);
		if (!ret)
			ret = regmap_read(chg->max77693->regmap_muic,
					  MAX77693_MUIC_REG_STATUS2, &input);
		if (!ret && usb == 0 && !max77693_get_cable_current(chg) &&
		    !(input & MAX77693_STATUS2_VBVOLT_MASK) &&
		    !(mode & (CHG_CNFG_00_OTG_MASK |
			       CHG_CNFG_00_BOOST_MASK |
			       CHG_CNFG_00_DIS_MUIC_CTRL_MASK)) &&
		    !max77693_t311_byp_irq_ready(chg)) {
			chg->otg_user_blocked = false;
			chg->otg_block_reason = 0;
			chg->otg_host_detach_ack =
				(unsigned int)atomic_read(&chg->otg_host_detach_edges);
			dev_info(chg->dev,
				 "T311_OTG_HOST_REPLUG_RECOVERED edges=%u: verified OFF, lease still unarmed\n",
				 chg->otg_host_detach_ack);
		} else {
			dev_warn_ratelimited(chg->dev,
				 "T311_OTG_HOST_REPLUG_REJECTED err=%d usb=%d\n",
				 ret, usb);
		}
	}
	/*
	 * A MAX77693 BYP_I overcurrent shuts 5V off and latches. Never
	 * auto-retry a shorted accessory on repeated HOST extcon events.
	 * Only a confirmed HOST detach plus verified OFF clears the fault.
	 */
	if (chg->otg_overcurrent_latched) {
		chg->otg_user_armed = false;
		ret = max77693_t311_otg_disable(chg);
		if (host == 0 && !ret) {
			chg->otg_overcurrent_latched = false;
			/* Continue to the fresh PMIC mode read below before
			 * allowing charger enable on the next non-HOST cable.
			 */
		} else {
			return true;
		}
	}
	if (host < 0) {
		chg->otg_user_armed = false;
		dev_err(chg->dev, "OTG extcon unreadable: %d\n", host);
		(void)max77693_t311_otg_disable(chg);
		return true;
	}
	/* Never source VBUS with conflicting USB input or extcon errors. */
	if (host && (max77693_get_cable_current(chg) ||
		     extcon_get_state(chg->extcon, EXTCON_USB) != 0)) {
		chg->otg_user_armed = false;
		chg->otg_user_blocked = true;
		chg->otg_block_reason = 2;
		dev_warn_ratelimited(chg->dev,
			"T311_OTG_HOST_CONFLICT: charger_input=%u usb_extcon=%d; latch OFF\n",
			max77693_get_cable_current(chg),
			extcon_get_state(chg->extcon, EXTCON_USB));
		(void)max77693_t311_otg_disable(chg);
		return true;
	}

	/*
	 * If a prior I2C operation failed, attempt one safe cleanup
	 * transaction but NEVER start BOOST in the same work invocation:
	 * a fresh cable event is required after a fault.
	 */
	if (chg->otg_cleanup_pending && !chg->otg_boost_on && host) {
		chg->otg_user_armed = false;
		chg->otg_user_blocked = true;
		chg->otg_block_reason = 3;
		ret = max77693_t311_otg_disable(chg);
		if (ret)
			dev_err(chg->dev,
				"OTG cleanup pending after failed I2C: %d\n", ret);
		return true;
	}
	if (!host) {
		unsigned int hw_mode;

		/* Re-arming always requires an entirely new confirmed attach. */
		chg->otg_user_armed = false;

		ret = max77693_t311_otg_disable(chg);
		if (ret) {
			dev_err(chg->dev, "OTG boost disable failed: %d\n", ret);
			return true; /* never run normal charger while boost may be on */
		}
		/*
		 * Verify actual PMIC mode on the safe path, including after
		 * boot firmware / another driver left a BOOST active before
		 * our driver acquired the extcon. A read failure is NOT safe.
		 */
		ret = regmap_read(chg->max77693->regmap,
				  MAX77693_CHG_REG_CHG_CNFG_00, &hw_mode);
		if (ret || (hw_mode & (CHG_CNFG_00_OTG_MASK |
				       CHG_CNFG_00_BOOST_MASK |
				       CHG_CNFG_00_DIS_MUIC_CTRL_MASK))) {
			dev_err(chg->dev,
				"cannot verify non-BOOST charging mode: err=%d mode=%02x\n",
				ret, ret ? 0 : hw_mode);
			return true;
		}
		chg->otg_user_blocked = false;
		chg->otg_block_reason = 0;
		return false; /* proceed only after confirmed BOOST inactive */
	}

	/* A cable alone never powers an unknown USB host or USB gadget. */
	if (chg->otg_user_blocked || !chg->otg_user_armed) {
		ret = max77693_t311_otg_disable(chg);
		if (ret)
			dev_err(chg->dev,
				"OTG coordinator unarmed/blocked; OFF unverified: %d\n", ret);
		return true;
	}

	if (chg->regulator_enabled) {
		ret = regulator_disable(chg->charger_regulator);
		if (ret) {
			dev_err(chg->dev, "cannot disable charger before OTG: %d\n", ret);
			return true;
		}
		chg->regulator_enabled = false;
		chg->active_current_limit = 0;
		power_supply_changed(chg->charger);
	}

	ret = max77693_t311_otg_enable(chg);
	if (ret && chg->otg_cleanup_pending && !chg->otg_boost_on) {
		int cleanup_ret;

		/*
		 * A rejected fresh ON or a lost cached ON is an uncertain
		 * hardware state. Attempt verified OFF immediately rather
		 * than waiting indefinitely for a second HOST extcon event.
		 * NEVER retry BOOST or regular charging in this invocation.
		 * On any I2C error max77693_t311_otg_disable() keeps the
		 * PHY1 lease and cleanup_pending for subsequent recovery.
		 */
		cleanup_ret = max77693_t311_otg_disable(chg);
		if (cleanup_ret)
			dev_err(chg->dev,
				"OTG rejected; immediate PMIC OFF unverified: %d\n",
				cleanup_ret);
	}
	if (ret) {
		/* Failures never silently authorize a subsequent HOST event. */
		chg->otg_user_armed = false;
		chg->otg_user_blocked = true;
		chg->otg_block_reason = 4;
		if (ret != -EBUSY)
			dev_err(chg->dev, "OTG 5V boost rejected: %d\n", ret);
	}
	return true; /* host event NEVER reaches regular charger-enable code */
}

/*
 * The Samsung 3.0.31 board charger handled BYP_DTLS bit0 on BYP_I as
 * a hardware bypass/BOOST overcurrent and cleared OTG+BOOST immediately.
 * max77693-regmap-irq numbers BYP as the FIRST (local index 0) charger
 * interrupt, not the global MAX77693_CHG_IRQ_BYP_I enum value.
 * This callback runs in a threaded interrupt context and may do I2C.
 */
static irqreturn_t max77693_t311_otg_bypass_irq(int irq, void *data)
{
	struct max77693_charger *chg = data;
	unsigned int details;
	int ret;

	mutex_lock(&chg->lock);
	/* The BYP interrupt can also happen during ordinary charging. */
	if (!chg->otg_cleanup_pending && !chg->otg_boost_on)
		goto out;

	ret = regmap_read(chg->max77693->regmap,
			  MAX77693_CHG_REG_CHG_DETAILS_02, &details);
	if (!ret && !(details & CHG_DETAILS_02_BYP_MASK & BIT(0)))
		goto out;

	/* An unreadable fault register is unsafe with an active session. */
	chg->otg_overcurrent_latched = true;
	chg->otg_block_reason = 7;
	dev_err(chg->dev,
		"T311_OTG_BYP_FAULT irq=%d detail=%#x read_errno=%d: latch and verified OFF\n",
		irq, ret ? 0 : details & CHG_DETAILS_02_BYP_MASK, ret);
	ret = max77693_t311_otg_disable(chg);
	if (ret)
		dev_err(chg->dev,
			"T311_OTG_BYP_OFF_UNVERIFIED errno=%d: keep PHY1 lease\n",
			ret);
	mod_delayed_work(system_wq, &chg->otg_watchdog_work,
			 msecs_to_jiffies(1000));
out:
	mutex_unlock(&chg->lock);
	return IRQ_HANDLED;
}
#endif


#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
static void max77693_t311_otg_unregister_wakeup(void *data)
{
	wakeup_source_unregister(data);
}

static void max77693_t311_otg_watchdog_worker(struct work_struct *work)
{
	struct max77693_charger *chg =
		container_of(to_delayed_work(work), struct max77693_charger,
			     otg_watchdog_work);
	bool expired = false, retry = false;
	int ret = 0;

	mutex_lock(&chg->lock);
	if (chg->otg_shutdown)
		goto out;

	if (!chg->otg_user_blocked && chg->otg_user_armed &&
	    time_after_eq(jiffies, chg->otg_lease_deadline)) {
		chg->otg_user_armed = false;
		chg->otg_user_blocked = true;
		chg->otg_block_reason = 5;
		expired = true;
	}

	if (chg->otg_orphan_boost_uncertain ||
	    ((chg->otg_user_blocked || chg->otg_overcurrent_latched ||
	      !chg->otg_user_armed) &&
	     (chg->otg_cleanup_pending || chg->otg_boost_on))) {
		ret = max77693_t311_otg_disable(chg);
		if (ret)
			retry = true;
	}

	if (chg->otg_cleanup_pending || chg->otg_boost_on ||
	    chg->otg_user_armed || chg->otg_orphan_boost_uncertain)
		retry = true;
out:
	mutex_unlock(&chg->lock);

	if (expired)
		dev_err(chg->dev, "OTG power lease expired; BOOST revoked\n");
	if (ret)
		dev_err_ratelimited(chg->dev,
			"OTG emergency PMIC OFF not verified: %d; retrying\n", ret);
	if (retry)
		mod_delayed_work(system_wq, &chg->otg_watchdog_work,
				 msecs_to_jiffies(1000));
}

/*
 * Optional supervised Android role coordinator interface. NO automatic
 * gadget/host transition is performed by the charger. In particular, the
 * charger must NEVER unbind adbd/DWC2 or toggle the HSIC-bearing EHCI.
 *
 * The status is safe to poll read-only; physical BOOST detection is from a
 * fresh MAX77693 register read, not optimistic software bookkeeping.
 */
static ssize_t t311_otg_interlock_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int pmic_mode = 0;
	bool pending, boost, lease, shutting_down, armed, blocked, fault;
	u8 reason;
	unsigned int detaches, acknowledged;
	int ret;

	if (!chg || !max77693_t311_otg_allowed(chg))
		return -EOPNOTSUPP;

	mutex_lock(&chg->lock);
	pending = chg->otg_cleanup_pending;
	boost = chg->otg_boost_on;
	lease = chg->otg_host_claimed;
	shutting_down = chg->otg_shutdown;
	armed = chg->otg_user_armed;
	blocked = chg->otg_user_blocked;
	reason = chg->otg_block_reason;
	fault = chg->otg_overcurrent_latched ||
		chg->otg_orphan_boost_uncertain;
	detaches = (unsigned int)atomic_read(&chg->otg_host_detach_edges);
	acknowledged = chg->otg_host_detach_ack;
	ret = regmap_read(chg->max77693->regmap,
			  MAX77693_CHG_REG_CHG_CNFG_00, &pmic_mode);
	mutex_unlock(&chg->lock);

	return sysfs_emit(buf,
		"boost=%u pending=%u lease=%u shutdown=%u armed=%u blocked=%u pmic_known=%u pmic_boost=%u safe_off=%u detach_edges=%u detach_ack=%u fault_latched=%u block_reason=%u\n",
		boost, pending, lease, shutting_down, armed, blocked, !ret,
		!ret && !!(pmic_mode & (CHG_CNFG_00_OTG_MASK |
				       CHG_CNFG_00_BOOST_MASK |
				       CHG_CNFG_00_DIS_MUIC_CTRL_MASK)),
		!ret && !(pmic_mode & (CHG_CNFG_00_OTG_MASK |
				      CHG_CNFG_00_BOOST_MASK |
				      CHG_CNFG_00_DIS_MUIC_CTRL_MASK)) &&
		!pending && !lease, detaches, acknowledged, fault, reason);
}
static DEVICE_ATTR_RO(t311_otg_interlock);

static ssize_t t311_otg_host_refresh_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	int state;

	/* No external toggles in production; root-only sysfs, opt-in DT. */
	if (!chg || !max77693_t311_otg_allowed(chg))
		return -EOPNOTSUPP;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	if (READ_ONCE(chg->otg_shutdown))
		return -ESHUTDOWN;
	/* Explicit second phase, never just an automatic MUIC notifier. */
	mutex_lock(&chg->lock);
	state = extcon_get_state(chg->extcon, EXTCON_USB_HOST);
	if (state != 1) {
		mutex_unlock(&chg->lock);
		return state < 0 ? state : -ENOTCONN;
	}
	if (chg->otg_shutdown || chg->otg_user_blocked ||
	    chg->otg_overcurrent_latched) {
		mutex_unlock(&chg->lock);
		return -EBUSY;
	}
	chg->otg_user_armed = true;
	chg->otg_lease_deadline = jiffies + msecs_to_jiffies(3000);
	mutex_unlock(&chg->lock);
	mod_delayed_work(system_wq, &chg->otg_watchdog_work,
			 msecs_to_jiffies(1000));
	/*
	 * The worker independently enforces gadget PHY0 released, host PHY1
	 * active, both HCDs ready, and SYSREG mux=HOST before PMIC BOOST.
	 */
	schedule_work(&chg->extcon_work);
	return count;
}
static DEVICE_ATTR_WO(t311_otg_host_refresh);

static ssize_t t311_otg_host_stop_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int mode;
	int ret;

	if (!chg || !max77693_t311_otg_allowed(chg))
		return -EOPNOTSUPP;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	/*
	 * Sticky stop while ID remains grounded: a queued/notified HOST event
	 * must not bring BOOST back up after this emergency off request.
	 * The worker does I2C and verified OFF, never sysfs store context.
	 * Caller MUST poll interlock safe_off; write success is NOT OFF proof.
	 */
	mutex_lock(&chg->lock);
	/*
	 * A cleanup trap may run after a rejected/never-started laboratory
	 * session. If BOOST was never armed, the PMIC is verified OFF, no
	 * lease exists and no earlier fault was latched, STOP is idempotent.
	 * Do not poison all later OTG insertions with block_reason=6 when
	 * there was nothing to stop in the first place.
	 *
	 * On any uncertainty, actual source lease, active BOOST, or prior
	 * block we KEEP the original sticky emergency-stop semantics.
	 */
	ret = regmap_read(chg->max77693->regmap,
			  MAX77693_CHG_REG_CHG_CNFG_00, &mode);
	if (!ret && !chg->otg_shutdown && !chg->otg_user_blocked &&
	    !chg->otg_user_armed && !chg->otg_boost_on &&
	    !chg->otg_host_claimed && !chg->otg_cleanup_pending &&
	    !chg->otg_orphan_boost_uncertain &&
	    !chg->otg_overcurrent_latched &&
	    !(mode & (CHG_CNFG_00_OTG_MASK |
		       CHG_CNFG_00_BOOST_MASK |
		       CHG_CNFG_00_DIS_MUIC_CTRL_MASK))) {
		mutex_unlock(&chg->lock);
		return count;
	}
	/*
	 * Emergency STOP must not overwrite a previously diagnosed
	 * I2C/BYP/charger fault with the generic reason 6. Keep the first
	 * useful reason across repeated cleanup calls.
	 */
	if (!chg->otg_user_blocked || !chg->otg_block_reason)
		chg->otg_block_reason = 6;
	chg->otg_user_blocked = true;
	chg->otg_user_armed = false;
	mutex_unlock(&chg->lock);
	mod_delayed_work(system_wq, &chg->otg_watchdog_work,
			 msecs_to_jiffies(1));
	schedule_work(&chg->extcon_work);
	return count;
}
static DEVICE_ATTR_WO(t311_otg_host_stop);
#endif

#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
/*
 * Called before device_shutdown() and ahead of the T311 XMM notifier
 * (priority 255). Do not assume we can veto a kernel reboot on I2C error.
 * Only the explicit board+DT opt-in registers this notifier.
 */
static int max77693_t311_otg_reboot_notify(struct notifier_block *nb,
					  unsigned long action, void *data)
{
	struct max77693_charger *chg =
		container_of(nb, struct max77693_charger, otg_reboot_nb);
	unsigned int mode;
	int ret;

	if (action != SYS_RESTART && action != SYS_HALT &&
	    action != SYS_POWER_OFF)
		return NOTIFY_DONE;

	WRITE_ONCE(chg->otg_shutdown, true);
	cancel_delayed_work_sync(&chg->otg_watchdog_work);
	cancel_work_sync(&chg->extcon_work);
	mutex_lock(&chg->lock);
	ret = max77693_t311_otg_disable(chg);
	if (!ret) {
		/* Also verify an idle/no-owned-lease PMIC, not just our session. */
		ret = regmap_read(chg->max77693->regmap,
				  MAX77693_CHG_REG_CHG_CNFG_00, &mode);
		if (!ret && (mode & (CHG_CNFG_00_OTG_MASK |
				     CHG_CNFG_00_BOOST_MASK |
				     CHG_CNFG_00_DIS_MUIC_CTRL_MASK)))
			ret = -EIO;
	}
	if (ret)
		dev_emerg(chg->dev,
			  "T311_OTG_REBOOT_UNSAFE: PMIC OFF not verified: %d; reboot cannot be vetoed\n",
			  ret);
	else
		dev_info(chg->dev,
			 "T311_OTG_REBOOT PMIC OFF verified before USB shutdown\n");
	mutex_unlock(&chg->lock);

	return NOTIFY_DONE;
}
#endif

static void max77693_charger_extcon_worker(struct work_struct *work)
{
	struct max77693_charger *chg =
		container_of(work, struct max77693_charger, extcon_work);
	unsigned int current_limit;
	int ret;

	mutex_lock(&chg->lock);
#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	if (max77693_t311_otg_handle(chg))
		goto out;
#endif
	current_limit = max77693_get_cable_current(chg);

	if (!current_limit) {
		if (!chg->regulator_enabled)
			goto out;

		ret = regulator_disable(chg->charger_regulator);
		if (ret) {
			dev_err(chg->dev,
				"failed to disable charger regulator: %d\n",
				ret);
			goto out;
		}

		chg->regulator_enabled = false;
		chg->active_current_limit = 0;
		dev_info(chg->dev, "charging disabled: cable disconnected\n");
		power_supply_changed(chg->charger);
		goto out;
	}

	if (chg->regulator_enabled &&
	    chg->active_current_limit == current_limit)
		goto out;

	ret = regulator_set_current_limit(chg->charger_regulator,
					  current_limit, current_limit);
	if (ret) {
		dev_err(chg->dev, "failed to set %u uA input limit: %d\n",
			current_limit, ret);
		goto out;
	}

	ret = max77693_set_fast_charge_current(chg, current_limit);
	if (ret) {
		dev_err(chg->dev, "failed to set %u uA charge current: %d\n",
			current_limit, ret);
		goto out;
	}

	if (!chg->regulator_enabled) {
		/*
		 * Take a regulator-framework reference even when the bootloader
		 * left charging enabled. This prevents late_initcall from
		 * disabling the apparently unused CHARGER regulator.
		 */
		ret = regulator_enable(chg->charger_regulator);
		if (ret) {
			dev_err(chg->dev,
				"failed to enable charger regulator: %d\n", ret);
			goto out;
		}
		chg->regulator_enabled = true;
	}

	chg->active_current_limit = current_limit;
	dev_info(chg->dev, "charging enabled at %u uA\n", current_limit);
	power_supply_changed(chg->charger);

out:
	mutex_unlock(&chg->lock);
}

/*
 * G466: always-available SM-T311 USB/OTG diagnostic, deliberately READ-ONLY.
 * Do not take ownership of PHY, alter the MUIC mux, toggle gadget functions,
 * or write MAX77693. This is independent from the explicit OTG-BOOST opt-in.
 * On I2C/PHY failure, report unknown rather than a misleading safe state.
 */
static ssize_t t311_usb_role_diag_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	struct tab3_xmm6262_phy_diag phy = {};
	unsigned int charger_mode = 0, muic_status2 = 0, otg_ilim = 0;
	int usb, host, pmic_ret, vbus_ret, ilim_ret, phy_ret;
	bool power_control = false;

	if (!chg || !of_machine_is_compatible("samsung,t311"))
		return -EOPNOTSUPP;

#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	power_control = max77693_t311_otg_allowed(chg);
#endif
	mutex_lock(&chg->lock);
	usb = extcon_get_state(chg->extcon, EXTCON_USB);
	host = extcon_get_state(chg->extcon, EXTCON_USB_HOST);
	pmic_ret = regmap_read(chg->max77693->regmap,
			       MAX77693_CHG_REG_CHG_CNFG_00, &charger_mode);
	vbus_ret = regmap_read(chg->max77693->regmap_muic,
			       MAX77693_MUIC_REG_STATUS2, &muic_status2);
	ilim_ret = regmap_read(chg->max77693->regmap,
			       MAX77693_CHG_REG_CHG_CNFG_02, &otg_ilim);
	mutex_unlock(&chg->lock);

	phy_ret = tab3_t311_phy_diag_snapshot(&phy);
	return sysfs_emit(buf,
		"extcon_usb=%d extcon_host=%d pmic_errno=%d boost_bits_known=%u boost_bits=%u muic_errno=%d vbus_present_known=%u vbus_present=%u phy_errno=%d phy0_device_owners=%d phy1_host_owners=%d phy_hsic0_owners=%d phy_hsic1_owners=%d host_runtime_flags=0x%x ohci_runtime_suspended=%u host_hcd_ready_flags=0x%x mode_switch_known=%u mode_switch_host=%u no_power_control=%u otg_ilim_known=%u otg_ilim_900ma=%u otg_ilim_unqualified=%u\n",
		usb, host, pmic_ret, !pmic_ret,
		!pmic_ret && !!(charger_mode &
			(CHG_CNFG_00_OTG_MASK | CHG_CNFG_00_BOOST_MASK |
			 CHG_CNFG_00_DIS_MUIC_CTRL_MASK)),
		vbus_ret, !vbus_ret,
		!vbus_ret && !!(muic_status2 & MAX77693_STATUS2_VBVOLT_MASK),
		phy_ret, phy_ret ? -1 : phy.ext_cnt[0],
		phy_ret ? -1 : phy.ext_cnt[1],
		phy_ret ? -1 : phy.ext_cnt[2],
		phy_ret ? -1 : phy.ext_cnt[3],
		phy_ret ? 0 : phy.host_runtime_flags,
		phy_ret ? 0 : phy.ohci_runtime_suspended,
		phy_ret ? 0 : phy.host_hcd_ready_flags,
		!phy_ret && !phy.mode_switch_errno,
		!phy_ret && !phy.mode_switch_errno && (phy.mode_switch & 1),
		!power_control, !ilim_ret,
		!ilim_ret && !!(otg_ilim & CHG_CNFG_02_OTG_ILIM_MASK),
		!ilim_ret && !!(otg_ilim & CHG_CNFG_02_OTG_UNQUALIFIED_MASK));
}
static DEVICE_ATTR_RO(t311_usb_role_diag);

static int max77693_charger_extcon_event(struct notifier_block *nb,
					 unsigned long event, void *ptr)
{
	struct max77693_charger *chg =
		container_of(nb, struct max77693_charger, extcon_nb);

#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	if (max77693_t311_otg_allowed(chg)) {
		int host = extcon_get_state(chg->extcon, EXTCON_USB_HOST);

		/*
		 * The notifier sees each HOST edge immediately; schedule_work()
		 * coalesces them. Remember a falling edge only after a verified
		 * earlier HOST=1. Do not touch charger I2C or clear any latch in
		 * notifier context.
		 */
		if (host == 1) {
			WRITE_ONCE(chg->otg_host_seen, true);
		} else if (host == 0 && READ_ONCE(chg->otg_host_seen)) {
			WRITE_ONCE(chg->otg_host_seen, false);
			atomic_inc(&chg->otg_host_detach_edges);
		}
	}
	if (!READ_ONCE(chg->otg_shutdown))
#endif
		schedule_work(&chg->extcon_work);

	return NOTIFY_OK;
}

static enum power_supply_property max77693_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static int max77693_charger_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max77693_charger *chg = power_supply_get_drvdata(psy);
	struct regmap *regmap = chg->max77693->regmap;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = max77693_get_charger_state(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = max77693_get_charge_type(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = max77693_get_battery_health(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = max77693_get_present(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = max77693_get_online(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = max77693_get_usb_type(chg);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = READ_ONCE(chg->active_current_limit);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = READ_ONCE(chg->active_current_limit) ? 5000000 : 0;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = max77693_get_input_current_limit(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = max77693_get_fast_charge_current(regmap, &val->intval);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = max77693_charger_model;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = max77693_charger_manufacturer;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct power_supply_desc max77693_charger_desc = {
	.name		= MAX77693_CHARGER_NAME,
	.type		= POWER_SUPPLY_TYPE_USB,
	.usb_types	= BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
			  BIT(POWER_SUPPLY_USB_TYPE_SDP) |
			  BIT(POWER_SUPPLY_USB_TYPE_DCP) |
			  BIT(POWER_SUPPLY_USB_TYPE_CDP),
	.properties	= max77693_charger_props,
	.num_properties	= ARRAY_SIZE(max77693_charger_props),
	.get_property	= max77693_charger_get_property,
};

static ssize_t device_attr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count,
		int (*fn)(struct max77693_charger *, unsigned long))
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	ret = fn(chg, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t fast_charge_timer_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int data, val;
	int ret;

	ret = regmap_read(chg->max77693->regmap, MAX77693_CHG_REG_CHG_CNFG_01,
			&data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_01_FCHGTIME_MASK;
	data >>= CHG_CNFG_01_FCHGTIME_SHIFT;
	switch (data) {
	case 0x1 ... 0x7:
		/* Starting from 4 hours, step by 2 hours */
		val = 4 + (data - 1) * 2;
		break;
	case 0x0:
	default:
		val = 0;
		break;
	}

	return sysfs_emit(buf, "%u\n", val);
}

static int max77693_set_fast_charge_timer(struct max77693_charger *chg,
		unsigned long hours)
{
	unsigned int data;

	/*
	 * 0x00 - disable
	 * 0x01 - 4h
	 * 0x02 - 6h
	 * ...
	 * 0x07 - 16h
	 * Round down odd values.
	 */
	switch (hours) {
	case 4 ... 16:
		data = (hours - 4) / 2 + 1;
		break;
	case 0:
		/* Disable */
		data = 0;
		break;
	default:
		return -EINVAL;
	}
	data <<= CHG_CNFG_01_FCHGTIME_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_01,
			CHG_CNFG_01_FCHGTIME_MASK, data);
}

static ssize_t fast_charge_timer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return device_attr_store(dev, attr, buf, count,
			max77693_set_fast_charge_timer);
}

static ssize_t top_off_threshold_current_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int data, val;
	int ret;

	ret = regmap_read(chg->max77693->regmap, MAX77693_CHG_REG_CHG_CNFG_03,
			&data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_03_TOITH_MASK;
	data >>= CHG_CNFG_03_TOITH_SHIFT;

	if (data <= 0x04)
		val = 100000 + data * 25000;
	else
		val = data * 50000;

	return sysfs_emit(buf, "%u\n", val);
}

static int max77693_set_top_off_threshold_current(struct max77693_charger *chg,
		unsigned long uamp)
{
	unsigned int data;

	if (uamp < 100000 || uamp > 350000)
		return -EINVAL;

	if (uamp <= 200000)
		data = (uamp - 100000) / 25000;
	else
		/* (200000, 350000> */
		data = uamp / 50000;

	data <<= CHG_CNFG_03_TOITH_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_03,
			CHG_CNFG_03_TOITH_MASK, data);
}

static ssize_t top_off_threshold_current_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return device_attr_store(dev, attr, buf, count,
			max77693_set_top_off_threshold_current);
}

static ssize_t top_off_timer_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77693_charger *chg = dev_get_drvdata(dev);
	unsigned int data, val;
	int ret;

	ret = regmap_read(chg->max77693->regmap, MAX77693_CHG_REG_CHG_CNFG_03,
			&data);
	if (ret < 0)
		return ret;

	data &= CHG_CNFG_03_TOTIME_MASK;
	data >>= CHG_CNFG_03_TOTIME_SHIFT;

	val = data * 10;

	return sysfs_emit(buf, "%u\n", val);
}

static int max77693_set_top_off_timer(struct max77693_charger *chg,
		unsigned long minutes)
{
	unsigned int data;

	if (minutes > 70)
		return -EINVAL;

	data = minutes / 10;
	data <<= CHG_CNFG_03_TOTIME_SHIFT;

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_03,
			CHG_CNFG_03_TOTIME_MASK, data);
}

static ssize_t top_off_timer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return device_attr_store(dev, attr, buf, count,
			max77693_set_top_off_timer);
}

static DEVICE_ATTR_RW(fast_charge_timer);
static DEVICE_ATTR_RW(top_off_threshold_current);
static DEVICE_ATTR_RW(top_off_timer);

static int max77693_set_constant_volt(struct max77693_charger *chg,
		unsigned int uvolt)
{
	unsigned int data;

	/*
	 * 0x00 - 3.650 V
	 * 0x01 - 3.675 V
	 * ...
	 * 0x1b - 4.325 V
	 * 0x1c - 4.340 V
	 * 0x1d - 4.350 V
	 * 0x1e - 4.375 V
	 * 0x1f - 4.400 V
	 */
	if (uvolt >= 3650000 && uvolt < 4340000)
		data = (uvolt - 3650000) / 25000;
	else if (uvolt >= 4340000 && uvolt < 4350000)
		data = 0x1c;
	else if (uvolt >= 4350000 && uvolt <= 4400000)
		data = 0x1d + (uvolt - 4350000) / 25000;
	else {
		dev_err(chg->dev, "Wrong value for charging constant voltage\n");
		return -EINVAL;
	}

	data <<= CHG_CNFG_04_CHGCVPRM_SHIFT;

	dev_dbg(chg->dev, "Charging constant voltage: %u (0x%x)\n", uvolt,
			data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_04,
			CHG_CNFG_04_CHGCVPRM_MASK, data);
}

static int max77693_set_min_system_volt(struct max77693_charger *chg,
		unsigned int uvolt)
{
	unsigned int data;

	if (uvolt < 3000000 || uvolt > 3700000) {
		dev_err(chg->dev, "Wrong value for minimum system regulation voltage\n");
		return -EINVAL;
	}

	data = (uvolt - 3000000) / 100000;

	data <<= CHG_CNFG_04_MINVSYS_SHIFT;

	dev_dbg(chg->dev, "Minimum system regulation voltage: %u (0x%x)\n",
			uvolt, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_04,
			CHG_CNFG_04_MINVSYS_MASK, data);
}

static int max77693_set_thermal_regulation_temp(struct max77693_charger *chg,
		unsigned int cels)
{
	unsigned int data;

	switch (cels) {
	case 70:
	case 85:
	case 100:
	case 115:
		data = (cels - 70) / 15;
		break;
	default:
		dev_err(chg->dev, "Wrong value for thermal regulation loop temperature\n");
		return -EINVAL;
	}

	data <<= CHG_CNFG_07_REGTEMP_SHIFT;

	dev_dbg(chg->dev, "Thermal regulation loop temperature: %u (0x%x)\n",
			cels, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_07,
			CHG_CNFG_07_REGTEMP_MASK, data);
}

static int max77693_set_batttery_overcurrent(struct max77693_charger *chg,
		unsigned int uamp)
{
	unsigned int data;

	if (uamp && (uamp < 2000000 || uamp > 3500000)) {
		dev_err(chg->dev, "Wrong value for battery overcurrent\n");
		return -EINVAL;
	}

	if (uamp)
		data = ((uamp - 2000000) / 250000) + 1;
	else
		data = 0; /* disable */

	data <<= CHG_CNFG_12_B2SOVRC_SHIFT;

	dev_dbg(chg->dev, "Battery overcurrent: %u (0x%x)\n", uamp, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_12,
			CHG_CNFG_12_B2SOVRC_MASK, data);
}

static int max77693_set_charge_input_threshold_volt(struct max77693_charger *chg,
		unsigned int uvolt)
{
	unsigned int data;

	switch (uvolt) {
	case 4300000:
		data = 0x0;
		break;
	case 4700000:
	case 4800000:
	case 4900000:
		data = ((uvolt - 4700000) / 100000) + 1;
		break;
	default:
		dev_err(chg->dev, "Wrong value for charge input voltage regulation threshold\n");
		return -EINVAL;
	}

	data <<= CHG_CNFG_12_VCHGINREG_SHIFT;

	dev_dbg(chg->dev, "Charge input voltage regulation threshold: %u (0x%x)\n",
			uvolt, data);

	return regmap_update_bits(chg->max77693->regmap,
			MAX77693_CHG_REG_CHG_CNFG_12,
			CHG_CNFG_12_VCHGINREG_MASK, data);
}

/*
 * Sets charger registers to proper and safe default values.
 */
static int max77693_reg_init(struct max77693_charger *chg)
{
	int ret;
	unsigned int data;

	/* Unlock charger register protection */
	data = (0x3 << CHG_CNFG_06_CHGPROT_SHIFT);
	ret = regmap_update_bits(chg->max77693->regmap,
				MAX77693_CHG_REG_CHG_CNFG_06,
				CHG_CNFG_06_CHGPROT_MASK, data);
	if (ret) {
		dev_err(chg->dev, "Error unlocking registers: %d\n", ret);
		return ret;
	}

	ret = max77693_set_fast_charge_timer(chg, chg->fast_charge_timer);
	if (ret)
		return ret;

	ret = max77693_set_top_off_threshold_current(chg,
			chg->top_off_current);
	if (ret)
		return ret;

	ret = max77693_set_top_off_timer(chg, chg->top_off_timer);
	if (ret)
		return ret;

	ret = max77693_set_constant_volt(chg, chg->constant_volt);
	if (ret)
		return ret;

	ret = max77693_set_min_system_volt(chg, chg->min_system_volt);
	if (ret)
		return ret;

	ret = max77693_set_thermal_regulation_temp(chg,
			chg->thermal_regulation_temp);
	if (ret)
		return ret;

	ret = max77693_set_batttery_overcurrent(chg, chg->batttery_overcurrent);
	if (ret)
		return ret;

	return max77693_set_charge_input_threshold_volt(chg,
			chg->charge_input_threshold_volt);
}

#ifdef CONFIG_OF
static int max77693_dt_init(struct device *dev, struct max77693_charger *chg)
{
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "no charger OF node\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "maxim,constant-microvolt",
			&chg->constant_volt))
		chg->constant_volt = DEFAULT_CONSTANT_VOLT;

	if (of_property_read_u32(np, "maxim,min-system-microvolt",
			&chg->min_system_volt))
		chg->min_system_volt = DEFAULT_MIN_SYSTEM_VOLT;

	if (of_property_read_u32(np, "maxim,thermal-regulation-celsius",
			&chg->thermal_regulation_temp))
		chg->thermal_regulation_temp = DEFAULT_THERMAL_REGULATION_TEMP;

	if (of_property_read_u32(np, "maxim,battery-overcurrent-microamp",
			&chg->batttery_overcurrent))
		chg->batttery_overcurrent = DEFAULT_BATTERY_OVERCURRENT;

	if (of_property_read_u32(np, "maxim,charge-input-threshold-microvolt",
			&chg->charge_input_threshold_volt))
		chg->charge_input_threshold_volt =
			DEFAULT_CHARGER_INPUT_THRESHOLD_VOLT;

	if (of_property_read_u32(np, "maxim,usb-current-microamp",
				 &chg->usb_current_limit))
		chg->usb_current_limit = MAX77693_DEFAULT_USB_CURRENT_UA;

	if (of_property_read_u32(np, "maxim,ac-current-microamp",
				 &chg->ac_current_limit))
		chg->ac_current_limit = MAX77693_DEFAULT_AC_CURRENT_UA;

	if (of_property_read_u32(np, "maxim,fast-charge-timer-hours",
				 &chg->fast_charge_timer))
		chg->fast_charge_timer = DEFAULT_FAST_CHARGE_TIMER;

	if (of_property_read_u32(np, "maxim,top-off-current-microamp",
				 &chg->top_off_current))
		chg->top_off_current = DEFAULT_TOP_OFF_THRESHOLD_CURRENT;

	if (of_property_read_u32(np, "maxim,top-off-timer-minutes",
				 &chg->top_off_timer))
		chg->top_off_timer = DEFAULT_TOP_OFF_TIMER;

	return 0;
}
#else /* CONFIG_OF */
static int max77693_dt_init(struct device *dev, struct max77693_charger *chg)
{
	return 0;
}
#endif /* CONFIG_OF */

static int max77693_charger_probe(struct platform_device *pdev)
{
	struct max77693_charger *chg;
	struct power_supply_config psy_cfg = {};
	struct max77693_dev *max77693 = dev_get_drvdata(pdev->dev.parent);
	int ret;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	platform_set_drvdata(pdev, chg);
	chg->dev = &pdev->dev;
	chg->max77693 = max77693;
	mutex_init(&chg->lock);
#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	atomic_set(&chg->otg_host_detach_edges, 0);
	INIT_DELAYED_WORK(&chg->otg_watchdog_work,
			  max77693_t311_otg_watchdog_worker);
#endif

	ret = max77693_dt_init(&pdev->dev, chg);
	if (ret)
		return ret;

	ret = max77693_reg_init(chg);
	if (ret)
		return ret;

	chg->charger_regulator = devm_regulator_get(&pdev->dev, "charger");
	if (IS_ERR(chg->charger_regulator))
		return dev_err_probe(&pdev->dev, PTR_ERR(chg->charger_regulator),
				     "failed to get CHARGER regulator\n");

	chg->extcon = extcon_get_edev_by_phandle(&pdev->dev, 0);
	if (IS_ERR(chg->extcon))
		return dev_err_probe(&pdev->dev, PTR_ERR(chg->extcon),
				     "failed to get MAX77693 MUIC extcon\n");

#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	/*
	 * Another explicit staging requirement: HOST PHY1 in the charger
	 * node phys property. This is ABSENT in production DTS. The PHY
	 * callback will reject accidental DEVICE PHY0 references.
	 */
	if (max77693_t311_otg_allowed(chg)) {
		chg->otg_wakeup_source = wakeup_source_register(&pdev->dev,
					"t311-max77693-otg");
		if (!chg->otg_wakeup_source)
			return -ENOMEM;
		ret = devm_add_action_or_reset(&pdev->dev,
				max77693_t311_otg_unregister_wakeup,
				chg->otg_wakeup_source);
		if (ret)
			return ret;
		chg->otg_host_phy = devm_of_phy_get_by_index(&pdev->dev,
							 pdev->dev.of_node, 0);
		if (IS_ERR(chg->otg_host_phy))
			return dev_err_probe(&pdev->dev,
					     PTR_ERR(chg->otg_host_phy),
					     "OTG requires explicit host PHY phandle\n");
	}
#endif

	psy_cfg.drv_data = chg;
	psy_cfg.fwnode = dev_fwnode(&pdev->dev);
	psy_cfg.supplied_to = max77693_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(max77693_charger_supplied_to);

	ret = device_create_file(&pdev->dev, &dev_attr_fast_charge_timer);
	if (ret) {
		dev_err(&pdev->dev, "failed: create fast charge timer sysfs entry\n");
		goto err;
	}

	ret = device_create_file(&pdev->dev,
			&dev_attr_top_off_threshold_current);
	if (ret) {
		dev_err(&pdev->dev, "failed: create top off current sysfs entry\n");
		goto err;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_top_off_timer);
	if (ret) {
		dev_err(&pdev->dev, "failed: create top off timer sysfs entry\n");
		goto err;
	}

	chg->charger = devm_power_supply_register(&pdev->dev,
						  &max77693_charger_desc,
						  &psy_cfg);
	if (IS_ERR(chg->charger)) {
		dev_err(&pdev->dev, "failed: power supply register\n");
		ret = PTR_ERR(chg->charger);
		goto err;
	}

	ret = devm_work_autocancel(&pdev->dev, &chg->extcon_work,
				   max77693_charger_extcon_worker);
	if (ret)
		goto err;

	/* Read-only role snapshot for the SM-T311, without enabling BOOST. */
	if (of_machine_is_compatible("samsung,t311")) {
		ret = device_create_file(&pdev->dev, &dev_attr_t311_usb_role_diag);
		if (ret)
			goto err;
	}

	chg->extcon_nb.notifier_call = max77693_charger_extcon_event;
	ret = devm_extcon_register_notifier_all(&pdev->dev, chg->extcon,
						&chg->extcon_nb);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register MUIC extcon notifier: %d\n", ret);
		goto err;
	}

#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	if (max77693_t311_otg_allowed(chg)) {
		int byp_irq;

		/*
		 * BYP_I is charger irq_data_chg local index ZERO (first item
		 * in max77693_charger_irqs[]), NOT the global enum index.
		 * Missing IRQ is fatal for experimental OTG: do not operate
		 * 5V without the stock Samsung overcurrent reaction.
		 */
		byp_irq = regmap_irq_get_virq(chg->max77693->irq_data_chg, 0);
		if (byp_irq < 0) {
			ret = byp_irq;
			goto err;
		}
		ret = devm_request_threaded_irq(&pdev->dev, byp_irq, NULL,
				max77693_t311_otg_bypass_irq,
				IRQF_ONESHOT, "t311-max77693-byp", chg);
		if (ret)
			goto err;

		/*
		 * This callback precedes XMM6262 (priority 255) and the core's
		 * device_shutdown walk, without changing the modem notifier.
		 * Use devres so probe rollback/unbind unregisters it safely.
		 */
		chg->otg_reboot_nb.notifier_call =
			max77693_t311_otg_reboot_notify;
		chg->otg_reboot_nb.priority = 256;
		ret = devm_register_reboot_notifier(&pdev->dev,
						   &chg->otg_reboot_nb);
		if (ret)
			goto err;
		ret = device_create_file(&pdev->dev, &dev_attr_t311_otg_interlock);
		if (ret)
			goto err;
		ret = device_create_file(&pdev->dev, &dev_attr_t311_otg_host_refresh);
		if (ret) {
			device_remove_file(&pdev->dev, &dev_attr_t311_otg_interlock);
			goto err;
		}
		ret = device_create_file(&pdev->dev, &dev_attr_t311_otg_host_stop);
		if (ret) {
			device_remove_file(&pdev->dev, &dev_attr_t311_otg_host_refresh);
			device_remove_file(&pdev->dev, &dev_attr_t311_otg_interlock);
			goto err;
		}
	}
	/* Extcon work may already be queued; never source 5V until ALL
	 * opt-in registration and sysfs setup have succeeded.
	 */
	if (max77693_t311_otg_allowed(chg))
		WRITE_ONCE(chg->otg_byp_irq_registered, true);
#endif
	dev_info(&pdev->dev,
		 "V150 Android power-supply ABI: USB=%u uA AC=%u uA\n",
		 chg->usb_current_limit, chg->ac_current_limit);
	schedule_work(&chg->extcon_work);

	return 0;

err:
#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	WRITE_ONCE(chg->otg_shutdown, true);
	cancel_delayed_work_sync(&chg->otg_watchdog_work);
#endif
	if (of_machine_is_compatible("samsung,t311"))
		device_remove_file(&pdev->dev, &dev_attr_t311_usb_role_diag);
	device_remove_file(&pdev->dev, &dev_attr_top_off_timer);
	device_remove_file(&pdev->dev, &dev_attr_top_off_threshold_current);
	device_remove_file(&pdev->dev, &dev_attr_fast_charge_timer);

	return ret;
}

static void max77693_charger_remove(struct platform_device *pdev)
{
#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	struct max77693_charger *chg = platform_get_drvdata(pdev);
	int ret;

	/*
	 * Stop future extcon work and drain any in flight worker before
	 * attempting to release the battery BOOST.  Never leave VBUS
	 * deliberately sourced by a driver being removed.
	 */
	WRITE_ONCE(chg->otg_shutdown, true);
	cancel_delayed_work_sync(&chg->otg_watchdog_work);
	if (max77693_t311_otg_allowed(chg)) {
		device_remove_file(&pdev->dev, &dev_attr_t311_otg_host_stop);
		device_remove_file(&pdev->dev, &dev_attr_t311_otg_host_refresh);
		device_remove_file(&pdev->dev, &dev_attr_t311_otg_interlock);
	}
	cancel_work_sync(&chg->extcon_work);
	mutex_lock(&chg->lock);
	if (max77693_t311_otg_allowed(chg)) {
		ret = max77693_t311_otg_disable(chg);
		if (ret)
			dev_err(chg->dev,
				"OTG PMIC unsafe on removal, cannot confirm off: %d\n",
				ret);
	}
	mutex_unlock(&chg->lock);
#endif
	if (of_machine_is_compatible("samsung,t311"))
		device_remove_file(&pdev->dev, &dev_attr_t311_usb_role_diag);
	device_remove_file(&pdev->dev, &dev_attr_top_off_timer);
	device_remove_file(&pdev->dev, &dev_attr_top_off_threshold_current);
	device_remove_file(&pdev->dev, &dev_attr_fast_charge_timer);
}

static const struct platform_device_id max77693_charger_id[] = {
	{ .name = "max77693-charger" },
	{ }
};
MODULE_DEVICE_TABLE(platform, max77693_charger_id);

static struct platform_driver max77693_charger_driver = {
	.driver = {
		.name	= "max77693-charger",
	},
	.probe		= max77693_charger_probe,
	.remove		= max77693_charger_remove,
	.id_table	= max77693_charger_id,
};
/*
 * The platform driver's sysfs "unbind" cannot veto its void remove_new()
 * callback. If an opt-in T311 BOOST session has an I2C failure, manual
 * unbind would discard the charger state and devm PHY handle while VBUS
 * may still be sourced. Disallow this unsupported user-triggered path.
 * Kernel-internal shutdown/removal is NOT blocked; it still needs a
 * hardware-level rollback mechanism before physical OTG can be enabled.
 */
static int __init max77693_charger_init(void)
{
#ifdef CONFIG_CHARGER_MAX77693_T311_OTG_EXPERIMENTAL
	if (of_machine_is_compatible("samsung,t311"))
		max77693_charger_driver.driver.suppress_bind_attrs = true;
#endif
	return platform_driver_register(&max77693_charger_driver);
}
module_init(max77693_charger_init);

static void __exit max77693_charger_exit(void)
{
	platform_driver_unregister(&max77693_charger_driver);
}
module_exit(max77693_charger_exit);

MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_AUTHOR("Paweł Jarczak");
MODULE_DESCRIPTION("Maxim 77693 charger driver");
MODULE_LICENSE("GPL");
