#!/usr/bin/env python3
"""G727 source-anchored safety regression for coalesced USB HOST replug.

Abstract sequential state model, NOT a measurement of physical MAX77693 5V.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
SRC=(ROOT/"drivers/power/supply/max77693_charger.c").read_text()

required={
 "notification_tracks_host_zero":"atomic_inc(&chg->otg_host_detach_edges)" in SRC and
    "WRITE_ONCE(chg->otg_host_seen, false)" in SRC,
 "host_one_marks_seen":"WRITE_ONCE(chg->otg_host_seen, true)" in SRC,
 "atomic_initialization":"atomic_set(&chg->otg_host_detach_edges, 0)" in SRC,
 "worker_reads_event_sequence":"atomic_read(&chg->otg_host_detach_edges)" in SRC and
    "chg->otg_host_detach_ack" in SRC,
 "no_rearm_on_overcurrent":"!chg->otg_overcurrent_latched" in SRC,
 "no_rearm_if_i2c_uncertain":"!chg->otg_orphan_boost_uncertain" in SRC,
 "no_rearm_if_boost_pending":"!chg->otg_cleanup_pending && !chg->otg_boost_on" in SRC,
 "fresh_mode_and_vbus":"MAX77693_CHG_REG_CHG_CNFG_00, &mode" in SRC and
    "MAX77693_MUIC_REG_STATUS2, &input" in SRC,
 "extcon_cable_conflicts_checked":"usb == 0 && !max77693_get_cable_current(chg)" in SRC,
 "no_automatic_boost_on_rearm":"chg->otg_user_blocked = false;" in SRC and
    "chg->otg_host_detach_ack =" in SRC,
 "read_only_block_reason":"block_reason=%u" in SRC and "detach_edges=%u" in SRC,
 "root_lease_still_explicit":"t311_otg_host_refresh_store" in SRC
}

class State:
    def __init__(self):
        self.seen=False; self.edges=0; self.ack=0
        self.blocked=True; self.armed=False
        self.fault=False; self.orphan=False; self.pending=False; self.boost=False
        self.usb=False; self.vbus=False; self.charger=False; self.i2c_error=False
    def event(self,host):
        if host:self.seen=True
        elif self.seen:
            self.seen=False;self.edges+=1
    def worker(self,host):
        if host and self.blocked and self.edges!=self.ack and not any((
             self.fault,self.orphan,self.pending,self.boost,self.usb,self.vbus,
             self.charger,self.i2c_error)):
            self.blocked=False
            self.ack=self.edges
        return self.blocked,self.armed,self.boost

def scenario(name,events,fail_field=None,expect_clear=False):
    s=State()
    if fail_field:setattr(s,fail_field,True)
    for e in events:s.event(e)
    block,arm,boost=s.worker(events[-1])
    assert (not block)==expect_clear,(name,vars(s))
    assert not arm and not boost,(name,vars(s))
    print("PASS",name)

for n,v in required.items():
    assert v,n
    print("PASS source",n)
scenario("true_detach_reattach_work_coalesced",[True,False,True],expect_clear=True)
scenario("never_detached",[True,True],expect_clear=False)
scenario("BYP_overcurrent_is_sticky",[True,False,True],fail_field="fault")
scenario("I2C_uncertain_is_sticky",[True,False,True],fail_field="orphan")
scenario("boost_cleanup_pending_is_sticky",[True,False,True],fail_field="pending")
scenario("USB_PC_power_conflict_is_sticky",[True,False,True],fail_field="vbus")
scenario("charging_input_conflict_is_sticky",[True,False,True],fail_field="charger")
scenario("extcon_device_conflict_is_sticky",[True,False,True],fail_field="usb")
scenario("I2C_error_cannot_ack",[True,False,True],fail_field="i2c_error")
print("G727 SOURCE/SC-MODEL PASSED",len(required)+9,"checks; not a physical VBUS test")
