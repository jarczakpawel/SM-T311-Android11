/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_PHY_SAMSUNG_USB2_H
#define _LINUX_PHY_SAMSUNG_USB2_H

#include <linux/errno.h>
#include <linux/kconfig.h>

struct phy;

enum samsung_usb2_phy_host_owner {
	SAMSUNG_USB2_PHY_HOST_EHCI = 1,
	SAMSUNG_USB2_PHY_HOST_OHCI = 2,
};

#if IS_REACHABLE(CONFIG_PHY_SAMSUNG_USB2)
int samsung_usb2_phy_host_init(struct phy *phy,
			       enum samsung_usb2_phy_host_owner owner);
void samsung_usb2_phy_host_exit(struct phy *phy,
				enum samsung_usb2_phy_host_owner owner);
int samsung_usb2_phy_runtime_suspend(struct phy *phy,
			     enum samsung_usb2_phy_host_owner owner);
int samsung_usb2_phy_runtime_resume(struct phy *phy,
			    enum samsung_usb2_phy_host_owner owner);
/*
 * Returns 0 ONLY if the Samsung SM-T311 gadget PHY0 is released,
 * host PHY1 already owned and SYSREG mux is physically HOST, while
 * a lease prevents DEVICE re-entry. Does NOT switch the mux itself.
 */
int samsung_usb2_phy_t311_host_claim(struct phy *phy);
void samsung_usb2_phy_t311_host_release(struct phy *phy);
int samsung_usb2_phy_t311_host_teardown_begin(struct phy *phy,
				 enum samsung_usb2_phy_host_owner owner);
void samsung_usb2_phy_t311_host_teardown_end(struct phy *phy,
				 enum samsung_usb2_phy_host_owner owner);
void samsung_usb2_phy_t311_hcd_ready(struct phy *phy,
				 enum samsung_usb2_phy_host_owner owner,
				 bool ready);
#else
static inline int samsung_usb2_phy_host_init(struct phy *phy,
			       enum samsung_usb2_phy_host_owner owner)
{
	return -EOPNOTSUPP;
}
static inline void samsung_usb2_phy_host_exit(struct phy *phy,
				enum samsung_usb2_phy_host_owner owner)
{
}
static inline int samsung_usb2_phy_t311_host_claim(struct phy *phy)
{
	return -EOPNOTSUPP;
}
static inline void samsung_usb2_phy_t311_host_release(struct phy *phy)
{
}
static inline int samsung_usb2_phy_t311_host_teardown_begin(struct phy *phy,
				 enum samsung_usb2_phy_host_owner owner)
{
	return 0;
}
static inline void samsung_usb2_phy_t311_host_teardown_end(struct phy *phy,
				 enum samsung_usb2_phy_host_owner owner)
{
}
static inline void samsung_usb2_phy_t311_hcd_ready(struct phy *phy,
				 enum samsung_usb2_phy_host_owner owner,
				 bool ready)
{
}
static inline int samsung_usb2_phy_runtime_suspend(struct phy *phy,
			     enum samsung_usb2_phy_host_owner owner)
{
	return 0;
}
static inline int samsung_usb2_phy_runtime_resume(struct phy *phy,
			    enum samsung_usb2_phy_host_owner owner)
{
	return 0;
}
#endif

#endif
