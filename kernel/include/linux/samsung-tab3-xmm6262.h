/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SAMSUNG_TAB3_XMM6262_H
#define _LINUX_SAMSUNG_TAB3_XMM6262_H

#include <linux/poll.h>
#include <linux/types.h>

struct device;
struct usb_hcd;

/* Exact Samsung modem_if numeric ABI used by stock cbd/RIL. */
enum tab3_xmm6262_state {
	TAB3_XMM_OFFLINE = 0,
	TAB3_XMM_CRASH_RESET,
	TAB3_XMM_CRASH_EXIT,
	TAB3_XMM_BOOTING,
	TAB3_XMM_ONLINE,
	TAB3_XMM_NV_REBUILDING,
	TAB3_XMM_LOADER_DONE,
	TAB3_XMM_SIM_ATTACH,
	TAB3_XMM_SIM_DETACH,
};

typedef void (*tab3_xmm6262_hostwake_cb_t)(bool active, void *data);

/*
 * SM-T311 XMM6262 board/HSIC coordination hooks.
 * Implemented by the modem-control driver and consumed by EHCI/runtime.
 */
int tab3_xmm6262_hsic_phy_init_notify(void);
int tab3_xmm6262_hsic_host_state(bool on);
int tab3_xmm6262_hsic_link_reconnect(void);
long tab3_xmm6262_modem_compat_ioctl(unsigned int cmd);
int tab3_xmm6262_get_state(void);
bool tab3_xmm6262_phone_active(void);
void tab3_xmm6262_boot_wake_hold(void);
void tab3_xmm6262_boot_wake_release(void);
void tab3_xmm6262_fmt_open(bool open);
bool tab3_xmm6262_sim_changed(void);
void tab3_xmm6262_poll_state(struct file *file, poll_table *wait);
void tab3_xmm6262_set_state(enum tab3_xmm6262_state state);
void tab3_xmm6262_usb_connected_set(bool connected);
bool tab3_xmm6262_usb_connected_get(void);
int tab3_xmm6262_register_hostwake_cb(tab3_xmm6262_hostwake_cb_t cb,
				      void *data);
void tab3_xmm6262_unregister_hostwake_cb(tab3_xmm6262_hostwake_cb_t cb,
					void *data);

void tab3_xmm6262_handoff_quiesce_arm(void);
bool tab3_xmm6262_claim_host_rearm(void);
bool tab3_xmm6262_handoff_quiesce_active(void);
void tab3_xmm6262_handoff_quiesce_end(void);
int tab3_t311_ohci_platform_driver_detach(void);
int tab3_t311_ohci_runtime_prepare_active(void);
int tab3_t311_usb_platform_drivers_detach(void);
int tab3_t311_usb_platform_drivers_prepare_active(void);

/*
 * v171/v173/v174/v175/v176 trace-only XMM6262 second-stage handoff recorder.
 * Hot-path events write numeric data into a preallocated ring only.
 */
enum tab3_xmm6262_handoff_event {
	TAB3_XMM_HANDOFF_ARM = 1,
	TAB3_XMM_HANDOFF_USBBOOT_ATTACH,
	TAB3_XMM_HANDOFF_USBBOOT_DETACH,
	TAB3_XMM_HANDOFF_EHCI_OFF_ENTER,
	TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_REMOVE,
	TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_PHY,
	TAB3_XMM_HANDOFF_EHCI_ON_ENTER,
	TAB3_XMM_HANDOFF_EHCI_ON_AFTER_PHY,
	TAB3_XMM_HANDOFF_EHCI_ADD_BEGIN,
	TAB3_XMM_HANDOFF_EHCI_ADD_END,
	TAB3_XMM_HANDOFF_PHY_OWNER_BEFORE,
	TAB3_XMM_HANDOFF_PHY_OWNER_AFTER,
	TAB3_XMM_HANDOFF_PHY_HOST_RUNTIME,
	TAB3_XMM_HANDOFF_PORT2_RESET_SET,
	TAB3_XMM_HANDOFF_PORT2_RESET_DONE,
	TAB3_XMM_HANDOFF_PORT2_RESET_ATTEMPT,
	TAB3_XMM_HANDOFF_HUB_INIT_BEGIN,
	TAB3_XMM_HANDOFF_GET64_BEGIN,
	TAB3_XMM_HANDOFF_GET64_DONE,
	TAB3_XMM_HANDOFF_GET64_TRY_BEGIN,
	TAB3_XMM_HANDOFF_GET64_TRY_DONE,
	TAB3_XMM_HANDOFF_EP0_QTD_STATUS,
	TAB3_XMM_HANDOFF_EP0_SETUP_PACKET,
	TAB3_XMM_HANDOFF_EP0_QH_INFO,
	TAB3_XMM_HANDOFF_EP0_QH_OVERLAY,
	TAB3_XMM_HANDOFF_EP0_QTD_LINKS,
	TAB3_XMM_HANDOFF_EP0_ASYNC_STATE,
	TAB3_XMM_HANDOFF_EP0_EHCI_REGS,
	TAB3_XMM_HANDOFF_EP0_EHCI_AUX,
	TAB3_XMM_HANDOFF_EP0_PHY_REGS,
	TAB3_XMM_HANDOFF_EP0_PHY_TUNE,
	TAB3_XMM_HANDOFF_EP0_CLOCKS,
	TAB3_XMM_HANDOFF_TRANSIENT_HW,
	TAB3_XMM_HANDOFF_PORT2_PCD_IRQ,
	TAB3_XMM_HANDOFF_PORT2_PE_DROP_QH,
	TAB3_XMM_HANDOFF_PORT2_PE_DROP_QTD,
	TAB3_XMM_HANDOFF_PORT2_PE_DROP_URB,
	TAB3_XMM_HANDOFF_BOOT_XACT_META,
	TAB3_XMM_HANDOFF_BOOT_XACT_QH,
	TAB3_XMM_HANDOFF_BOOT_XACT_QTD,
	TAB3_XMM_HANDOFF_BOOT_XACT_URB,
	TAB3_XMM_HANDOFF_BOOT_XACT_CLOCKS,
	TAB3_XMM_HANDOFF_SECOND_RESET_BEGIN,
	TAB3_XMM_HANDOFF_SECOND_RESET_DONE,
	TAB3_XMM_HANDOFF_SECOND_RESET_SKIPPED,
	TAB3_XMM_HANDOFF_SET_ADDRESS_BEGIN,
	TAB3_XMM_HANDOFF_SET_ADDRESS_DONE,
	TAB3_XMM_HANDOFF_FULL_DESCRIPTOR,
	TAB3_XMM_HANDOFF_HUB_INIT_SUCCESS,
	TAB3_XMM_HANDOFF_HUB_INIT_FAIL,
	TAB3_XMM_HANDOFF_RUNTIME_CMD_ONLINE,
	/* Added after existing enum IDs: first IRQ-time BOOTROM PE drop PHY. */
	TAB3_XMM_HANDOFF_PORT2_PE_DROP_PHY_PWR,
	TAB3_XMM_HANDOFF_PORT2_PE_DROP_PHY_TUNE,
	TAB3_XMM_HANDOFF_PORT2_PE_DROP_PHY_OWNERS,
	TAB3_XMM_HANDOFF_PORT2_PRPE_READBACK,
};

enum tab3_xmm6262_transient_stage {
	TAB3_XMM_TRANSIENT_HWRESET_DONE = 1,
	TAB3_XMM_TRANSIENT_EHCI_OFF_ENTER,
	TAB3_XMM_TRANSIENT_EHCI_OFF_AFTER_REMOVE,
	TAB3_XMM_TRANSIENT_EHCI_OFF_AFTER_PHY,
	TAB3_XMM_TRANSIENT_EHCI_ON_ENTER,
	TAB3_XMM_TRANSIENT_EHCI_ON_AFTER_PHY,
	TAB3_XMM_TRANSIENT_EHCI_ADD_END,
	TAB3_XMM_TRANSIENT_PORT2_RESET_DONE,
};

enum tab3_xmm6262_phy_runtime_op {
	TAB3_XMM_PHY_RUNTIME_HOST_INIT = 1,
	TAB3_XMM_PHY_RUNTIME_HOST_EXIT,
	TAB3_XMM_PHY_RUNTIME_SUSPEND,
	TAB3_XMM_PHY_RUNTIME_RESUME,
};

struct tab3_xmm6262_phy_diag {
	u32 uphypwr;
	u32 uphyclk;
	u32 uphyrst;
	u32 phy0_phytune;
	u32 phy1_phytune;
	s32 ext_cnt[4];
	u32 host_runtime_flags;
	u32 ohci_runtime_suspended;
	u32 host_hcd_ready_flags;
	u32 mode_switch;
	s32 mode_switch_errno;
};

void tab3_xmm6262_handoff_trace_arm(u32 cookie);
bool tab3_xmm6262_handoff_trace_active(void);
bool tab3_xmm6262_boot_xact_should_capture(void);
bool tab3_xmm6262_handoff_trace_claim_ep0_snapshot(void);
void tab3_xmm6262_handoff_trace_event(enum tab3_xmm6262_handoff_event event,
				      u32 a, u32 b, u32 c, u32 d);
void tab3_xmm6262_trace_port2_pcd(u32 status, u32 portsc,
				  u32 reset_done, u32 command);
void tab3_xmm6262_trace_hw_snapshot(enum tab3_xmm6262_transient_stage stage);
void tab3_xmm6262_handoff_trace_port2_reset_set(u32 pre, u32 written,
						unsigned long deadline,
						unsigned long now);
void tab3_xmm6262_handoff_trace_port2_reset_done(int ret, u32 pre, u32 post,
						 unsigned long deadline);
int tab3_t311_phy_diag_snapshot(struct tab3_xmm6262_phy_diag *diag);
int tab3_t311_ehci_clock_snapshot(struct usb_hcd *hcd, u32 *usbhost_rate,
				  u32 *aclk133_rate, u32 *mout_rate,
				  u32 *aclk133_div_minus_one);

#endif /* _LINUX_SAMSUNG_TAB3_XMM6262_H */
