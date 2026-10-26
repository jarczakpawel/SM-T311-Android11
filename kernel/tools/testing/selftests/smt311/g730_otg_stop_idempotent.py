#!/usr/bin/env python3
"""G730 safe STOP idempotence model, source anchors; NEVER touches device.

The kernel only ignores manual STOP when PMIC is read back OFF and no
BOOST lease, source activity or sticky fault exists. All other states
must invoke existing latched STOP worker, never transparently clear it.
"""
from pathlib import Path

src=(Path(__file__).resolve().parents[4]/
     "drivers/power/supply/max77693_charger.c").read_text()
begin=src.index("static ssize_t t311_otg_host_stop_store(")
end=src.index("static DEVICE_ATTR_WO(t311_otg_host_stop);",begin)
fn=src[begin:end]

source_checks={
    "PMIC_ON_READBACK": "MAX77693_CHG_REG_CHG_CNFG_00, &mode" in fn,
    "I2C_ERROR_NEVER_BYPASS": "if (!ret &&" in fn,
    "BOOST_BITS_ALL_CHECKED": all(x in fn for x in (
        "CHG_CNFG_00_OTG_MASK","CHG_CNFG_00_BOOST_MASK",
        "CHG_CNFG_00_DIS_MUIC_CTRL_MASK")),
    "NEVER_BYPASS_SHUTDOWN": "!chg->otg_shutdown" in fn,
    "NEVER_CLEAR_OLD_BLOCK": "!chg->otg_user_blocked" in fn,
    "NEVER_IGNORE_ARMED_LEASE": "!chg->otg_user_armed" in fn,
    "NEVER_IGNORE_ACTIVE_BOOST": "!chg->otg_boost_on" in fn,
    "NEVER_IGNORE_PHY_LEASE": "!chg->otg_host_claimed" in fn,
    "NEVER_IGNORE_PENDING_CLEANUP": "!chg->otg_cleanup_pending" in fn,
    "NEVER_IGNORE_UNCERTAIN_BOOST": "!chg->otg_orphan_boost_uncertain" in fn,
    "NEVER_IGNORE_OVERCURRENT": "!chg->otg_overcurrent_latched" in fn,
    "STICKY_EMERGENCY_STOP_PRESERVED": "chg->otg_user_blocked = true;" in fn and
        "chg->otg_block_reason = 6;" in fn,
    "FIRST_FAULT_REASON_PRESERVED": "if (!chg->otg_user_blocked || !chg->otg_block_reason)" in fn,
    "WATCHDOG_STOP_WORK_PRESERVED": "mod_delayed_work(system_wq" in fn and
        "schedule_work(&chg->extcon_work)" in fn,
}
for name,passed in source_checks.items():
    assert passed,name
    print("PASS source",name)

def safe_ignore(*,i2c_error=False,pmic_boost=False,shutdown=False,
               blocked=False,armed=False,active=False,phy_lease=False,
               cleanup=False,uncertain=False,oc=False):
    return not any((i2c_error,pmic_boost,shutdown,blocked,armed,
                    active,phy_lease,cleanup,uncertain,oc))

assert safe_ignore()
print("PASS verified_OFF_unarmed_idle_is_idempotent")
for flag in (
    "i2c_error","pmic_boost","shutdown","blocked","armed",
    "active","phy_lease","cleanup","uncertain","oc"):
    assert not safe_ignore(**{flag:True})
    print("PASS sticky_STOP",flag)
print("G730_OTG_STOP_CHECKS_PASS",len(source_checks)+11)
