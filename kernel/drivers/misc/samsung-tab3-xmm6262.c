// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Galaxy Tab 3 8.0 3G (SM-T311) XMM6262 modem control
 *
 * Hardware/power sequencing is derived from Samsung's exact SM-T311
 * downstream board data and modem_modemctl_device_xmm6262.c.
 *
 * This driver intentionally implements only the physical modem-control and
 * HSIC wake GPIO layer. SIPC4 framing and Android RIL integration are a
 * separate follow-up layer.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/samsung-tab3-xmm6262.h>
#include <linux/pm_wakeirq.h>
#include <linux/pm_wakeup.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/wait.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#define TAB3_XMM6262_DRV_VERSION "6.12-samsung-bsp"

#define TAB3_EXYNOS_GPIO1_PHYS          0x11000000
#define TAB3_EXYNOS_GPIO1_SIZE          0x1000
#define TAB3_EXYNOS_USBPHY_PHYS         0x125b0000
#define TAB3_EXYNOS_USBPHY_SIZE         0x100
#define TAB3_EXYNOS_SYSREG_PHYS         0x10010000
#define TAB3_EXYNOS_SYSREG_SIZE         0x1000
#define TAB3_EXYNOS_PMU_PHYS            0x10020000
#define TAB3_EXYNOS_PMU_SIZE            0x1000
#define TAB3_EXYNOS_CMU_PHYS            0x10030000
#define TAB3_EXYNOS_CMU_SIZE            0x10000
#define TAB3_SYSREG_USB20PHY_CFG         0x021c
#define TAB3_PMU_USBPHY_CONTROL         0x0704
#define TAB3_PMU_USBHSIC0_PHY_CONTROL   0x0708
#define TAB3_PMU_USBHSIC1_PHY_CONTROL   0x070c
#define TAB3_CMU_SRC_TOP0                0xc210
#define TAB3_CMU_DIV_TOP                 0xc510
#define TAB3_CMU_GATE_IP_FSYS            0xc940
#define TAB3_USBPHY_PHY1_PHYTUNE        0x20
#define TAB3_USBPHY_PHY0_PHYTUNE        0x24
#define TAB3_GPL0_CON                   0x00c0
#define TAB3_GPL0_DAT                   0x00c4
#define TAB3_GPL1_DAT                   0x00e4
#define TAB3_GPL2_DAT                   0x0104
#define TAB3_GPM2_CON                   0x02a0
#define TAB3_GPM2_DAT                   0x02a4
#define TAB3_GPM2_PUD                   0x02a8
#define TAB3_GPX1_CON                   0x0c20
#define TAB3_GPX1_DAT                   0x0c24

#define IOCTL_MODEM_ON                  _IO('o', 0x19)
#define IOCTL_MODEM_OFF                 _IO('o', 0x20)
#define IOCTL_MODEM_RESET               _IO('o', 0x21)
#define IOCTL_MODEM_BOOT_ON             _IO('o', 0x22)
#define IOCTL_MODEM_BOOT_OFF            _IO('o', 0x23)
#define IOCTL_MODEM_BOOT_DONE           _IO('o', 0x24)
#define IOCTL_MODEM_STATUS              _IO('o', 0x27)
#define IOCTL_MODEM_FORCE_CRASH_EXIT    _IO('o', 0x34)

#define IOCTL_LINK_CONTROL_ENABLE       _IO('o', 0x30)
#define IOCTL_LINK_CONTROL_ACTIVE       _IO('o', 0x31)
#define IOCTL_LINK_GET_HOSTWAKE         _IO('o', 0x32)
#define IOCTL_LINK_CONNECTED            _IO('o', 0x33)
#define IOCTL_LINK_SET_BIAS_CLEAR       _IO('o', 0x34)
#define IOCTL_LINK_GET_PHONEACTIVE      _IO('o', 0x35)

#define XMM_BOOT_VENDOR                 0x058b
#define XMM_BOOT_PRODUCT                0x0041
#define XMM_RUNTIME_VENDOR              0x1519
#define XMM_RUNTIME_PRODUCT             0x0020
#define TAB3_XMM_LINK_TRACE_MAX         32

enum tab3_xmm6262_link_trace_kind {
	TAB3_XMM_TRACE_CONTROL_ENABLE = 0,
	TAB3_XMM_TRACE_CONTROL_ACTIVE,
	TAB3_XMM_TRACE_GET_HOSTWAKE,
	TAB3_XMM_TRACE_CONNECTED,
	TAB3_XMM_TRACE_SET_BIAS_CLEAR,
	TAB3_XMM_TRACE_GET_PHONEACTIVE,
	TAB3_XMM_TRACE_HOSTWAKE_IRQ,
	TAB3_XMM_TRACE_SLAVEWAKE_LOW,
	TAB3_XMM_TRACE_SLAVEWAKE_HIGH,
};

struct tab3_xmm6262_link_trace {
	u64 first_ns;
	u64 last_ns;
	u32 seq;
	u16 repeats;
	u8 kind;
	s8 value;
	s32 ret;
};

enum tab3_xmm6262_diag_stage {
	TAB3_XMM_DIAG_PROBE = 0,
	TAB3_XMM_DIAG_MODEM_ON_ENTRY,
	TAB3_XMM_DIAG_BIAS_CLEAR_DONE,
	TAB3_XMM_DIAG_CP_RESET_HIGH,
	TAB3_XMM_DIAG_CP_ON_PULSE_DONE,
	TAB3_XMM_DIAG_BIAS_RESTORE_DONE,
	TAB3_XMM_DIAG_MODEM_ON_DONE,
	TAB3_XMM_DIAG_PHY_INIT,
	TAB3_XMM_DIAG_BOOT_HOST_ON,
	TAB3_XMM_DIAG_HANDOFF_HOST_OFF,
	TAB3_XMM_DIAG_CONTROL_ACTIVE_LOW,
	TAB3_XMM_DIAG_HANDOFF_HOST_ON,
	TAB3_XMM_DIAG_STAGE_COUNT,
};

enum tab3_xmm6262_cp_on_wave_stage {
	TAB3_XMM_CPON_BEFORE_HIGH = 0,
	TAB3_XMM_CPON_AFTER_HIGH,
	TAB3_XMM_CPON_AFTER_60US,
	TAB3_XMM_CPON_AFTER_LOW,
	TAB3_XMM_CPON_AFTER_20MS,
	TAB3_XMM_CPON_WAVE_COUNT,
};

struct tab3_xmm6262_cp_on_wave_sample {
	bool valid;
	u32 gpl2_dat;
	u32 gpm2_dat;
};

struct tab3_xmm6262_diag_snapshot {
	bool valid;
	u8 state;
	u8 powered;
	u8 host_link;
	u32 gpm2_con;
	u32 gpm2_dat;
	u32 gpm2_pud;
	u32 gpl0_dat;
	u32 gpl1_dat;
	u32 gpx1_con;
	u32 gpx1_dat;
	u32 phy0_phytune;
	u32 phy1_phytune;
};

struct tab3_xmm6262 {
	struct device *dev;
	struct mutex lock;
	struct notifier_block reboot_nb;
	void __iomem *gpio1_base;
	void __iomem *usbphy_base;
	void __iomem *sysreg_base;
	void __iomem *pmu_base;
	void __iomem *cmu_base;

	struct gpio_desc *reset_req_n;
	struct gpio_desc *cp_on;
	struct gpio_desc *cp_reset;
	struct gpio_desc *pda_active;
	struct gpio_desc *phone_active;
	struct gpio_desc *cp_dump_int;
	struct gpio_desc *ap_dump_int;
	struct gpio_desc *link_active;
	struct gpio_desc *host_wake;
	struct gpio_desc *slave_wake;
	struct gpio_desc *suspend_request;

	int phone_active_irq;
	int host_wake_irq;
	wait_queue_head_t state_wait;
	unsigned int fmt_opened;
	bool powered;
	bool phone_wake_enabled;
	bool host_wake_enabled;
	bool host_link_on;
	bool boot_wake_held;
	tab3_xmm6262_hostwake_cb_t runtime_hostwake_cb;
	void *runtime_hostwake_data;
	struct miscdevice link_pm_miscdev;
	struct proc_dir_entry *handoff_trace_proc;
	enum tab3_xmm6262_state state;
	bool diag_first_attempt_started;
	struct tab3_xmm6262_diag_snapshot diag_snap[TAB3_XMM_DIAG_STAGE_COUNT];
	struct tab3_xmm6262_cp_on_wave_sample diag_cp_on_wave[TAB3_XMM_CPON_WAVE_COUNT];
	spinlock_t link_trace_lock;
	struct tab3_xmm6262_link_trace link_trace[TAB3_XMM_LINK_TRACE_MAX];
	u32 link_trace_count;
	u32 link_trace_seq;
};

static struct tab3_xmm6262 *tab3_xmm_transient_xmm;

static const char * const tab3_xmm6262_diag_stage_name[TAB3_XMM_DIAG_STAGE_COUNT] = {
	[TAB3_XMM_DIAG_PROBE] = "probe",
	[TAB3_XMM_DIAG_MODEM_ON_ENTRY] = "modem_on_entry",
	[TAB3_XMM_DIAG_BIAS_CLEAR_DONE] = "bias_clear_done",
	[TAB3_XMM_DIAG_CP_RESET_HIGH] = "cp_reset_high",
	[TAB3_XMM_DIAG_CP_ON_PULSE_DONE] = "cp_on_pulse_done",
	[TAB3_XMM_DIAG_BIAS_RESTORE_DONE] = "bias_restore_done",
	[TAB3_XMM_DIAG_MODEM_ON_DONE] = "modem_on_done",
	[TAB3_XMM_DIAG_PHY_INIT] = "phy_init",
	[TAB3_XMM_DIAG_BOOT_HOST_ON] = "boot_host_on",
	[TAB3_XMM_DIAG_HANDOFF_HOST_OFF] = "handoff_host_off",
	[TAB3_XMM_DIAG_CONTROL_ACTIVE_LOW] = "control_active_low",
	[TAB3_XMM_DIAG_HANDOFF_HOST_ON] = "handoff_host_on",
};

static const char * const tab3_xmm6262_cp_on_wave_name[TAB3_XMM_CPON_WAVE_COUNT] = {
	[TAB3_XMM_CPON_BEFORE_HIGH] = "before_high",
	[TAB3_XMM_CPON_AFTER_HIGH] = "after_high",
	[TAB3_XMM_CPON_AFTER_60US] = "after_60us",
	[TAB3_XMM_CPON_AFTER_LOW] = "after_low",
	[TAB3_XMM_CPON_AFTER_20MS] = "after_20ms",
};


static const char *tab3_xmm6262_link_trace_name(u8 kind)
{
	switch (kind) {
	case TAB3_XMM_TRACE_CONTROL_ENABLE: return "CONTROL_ENABLE";
	case TAB3_XMM_TRACE_CONTROL_ACTIVE: return "CONTROL_ACTIVE";
	case TAB3_XMM_TRACE_GET_HOSTWAKE: return "GET_HOSTWAKE";
	case TAB3_XMM_TRACE_CONNECTED: return "CONNECTED";
	case TAB3_XMM_TRACE_SET_BIAS_CLEAR: return "SET_BIAS_CLEAR";
	case TAB3_XMM_TRACE_GET_PHONEACTIVE: return "GET_PHONEACTIVE";
	case TAB3_XMM_TRACE_HOSTWAKE_IRQ: return "HOSTWAKE_IRQ";
	case TAB3_XMM_TRACE_SLAVEWAKE_LOW: return "SLAVEWAKE_LOW";
	case TAB3_XMM_TRACE_SLAVEWAKE_HIGH: return "SLAVEWAKE_HIGH";
	default: return "UNKNOWN";
	}
}

static void tab3_xmm6262_link_trace_add(struct tab3_xmm6262 *xmm, u8 kind,
					s8 value, s32 ret, bool compress)
{
	unsigned long flags;
	struct tab3_xmm6262_link_trace *e;
	u64 now;
	u32 n;

	if (!READ_ONCE(xmm->diag_first_attempt_started))
		return;
	now = ktime_get_ns();
	spin_lock_irqsave(&xmm->link_trace_lock, flags);
	n = xmm->link_trace_count;
	if (compress && n) {
		e = &xmm->link_trace[n - 1];
		if (e->kind == kind && e->value == value && e->ret == ret) {
			e->last_ns = now;
			if (e->repeats != 0xffff)
				e->repeats++;
			spin_unlock_irqrestore(&xmm->link_trace_lock, flags);
			return;
		}
	}
	if (n < TAB3_XMM_LINK_TRACE_MAX) {
		e = &xmm->link_trace[n];
		e->first_ns = now;
		e->last_ns = now;
		e->seq = ++xmm->link_trace_seq;
		e->repeats = 1;
		e->kind = kind;
		e->value = value;
		e->ret = ret;
		xmm->link_trace_count = n + 1;
	}
	spin_unlock_irqrestore(&xmm->link_trace_lock, flags);
}
static void tab3_xmm6262_diag_capture_cp_on_wave(
	struct tab3_xmm6262 *xmm, enum tab3_xmm6262_cp_on_wave_stage stage)
{
	struct tab3_xmm6262_cp_on_wave_sample *sample;

	if (!xmm->diag_first_attempt_started || stage >= TAB3_XMM_CPON_WAVE_COUNT)
		return;
	sample = &xmm->diag_cp_on_wave[stage];
	if (READ_ONCE(sample->valid))
		return;

	/*
	 * Two read-only MMIO loads only. No printk, no GPIO write, no delay.
	 * This keeps the stock 60 us CP_ON pulse effectively unchanged while
	 * telling us exactly when GPM2(4) first reads high.
	 */
	sample->gpl2_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPL2_DAT);
	sample->gpm2_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_DAT);
	WRITE_ONCE(sample->valid, true);
}

static void tab3_xmm6262_diag_capture(struct tab3_xmm6262 *xmm,
				      enum tab3_xmm6262_diag_stage stage)
{
	struct tab3_xmm6262_diag_snapshot *snap;

	if (stage >= TAB3_XMM_DIAG_STAGE_COUNT)
		return;
	if (stage != TAB3_XMM_DIAG_PROBE && !xmm->diag_first_attempt_started)
		return;

	snap = &xmm->diag_snap[stage];
	if (READ_ONCE(snap->valid))
		return;

	/* Read-only MMIO snapshots: no printk/WARN and no GPIO reprogramming. */
	snap->state = xmm->state;
	snap->powered = xmm->powered;
	snap->host_link = xmm->host_link_on;
	snap->gpm2_con = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_CON);
	snap->gpm2_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_DAT);
	snap->gpm2_pud = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_PUD);
	snap->gpl0_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPL0_DAT);
	snap->gpl1_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPL1_DAT);
	snap->gpx1_con = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_CON);
	snap->gpx1_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_DAT);
	snap->phy0_phytune = readl_relaxed(xmm->usbphy_base + TAB3_USBPHY_PHY0_PHYTUNE);
	snap->phy1_phytune = readl_relaxed(xmm->usbphy_base + TAB3_USBPHY_PHY1_PHYTUNE);
	WRITE_ONCE(snap->valid, true);
}


#define TAB3_XMM_HANDOFF_TRACE_MAX 512

enum tab3_xmm6262_handoff_phase {
	TAB3_XMM_HANDOFF_PHASE_IDLE = 0,
	TAB3_XMM_HANDOFF_PHASE_ARMED,
	TAB3_XMM_HANDOFF_PHASE_EHCI_OFF,
	TAB3_XMM_HANDOFF_PHASE_EHCI_ON,
	TAB3_XMM_HANDOFF_PHASE_RESET_PENDING,
	TAB3_XMM_HANDOFF_PHASE_FROZEN,
};

struct tab3_xmm6262_handoff_record {
	u64 ns;
	u32 seq;
	u16 event;
	u16 cpu;
	u32 a;
	u32 b;
	u32 c;
	u32 d;
	u32 valid;
};

static struct tab3_xmm6262_handoff_record
	tab3_xmm_handoff_trace[TAB3_XMM_HANDOFF_TRACE_MAX];
static atomic_t tab3_xmm_handoff_trace_armed = ATOMIC_INIT(0);
static atomic_t tab3_xmm_handoff_trace_frozen = ATOMIC_INIT(0);
static atomic_t tab3_xmm_handoff_trace_next = ATOMIC_INIT(0);
static atomic_t tab3_xmm_handoff_trace_phase =
	ATOMIC_INIT(TAB3_XMM_HANDOFF_PHASE_IDLE);
static atomic_t tab3_xmm_handoff_trace_reset_attempt = ATOMIC_INIT(0);
static atomic_t tab3_xmm_handoff_ep0_snapshot_claimed = ATOMIC_INIT(0);
/* After the first failed GET64 retain event summaries but subsample the
 * high-volume, five-record per qTD BOOT_XACT details. This leaves capacity
 * for later retry/reset/runtime outcomes in a 512-slot diagnostic recorder.
 */
static atomic_t tab3_xmm_handoff_get64_failed = ATOMIC_INIT(0);
static atomic_t tab3_xmm_handoff_late_boot_xacts = ATOMIC_INIT(0);
static atomic_t tab3_xmm_handoff_capture_this_reset = ATOMIC_INIT(1);
static atomic_t tab3_xmm_handoff_first_reset_failure = ATOMIC_INIT(0);
static u64 tab3_xmm_handoff_trace_t0_ns;

static const char *tab3_xmm6262_handoff_event_name(u16 event)
{
	switch (event) {
	case TAB3_XMM_HANDOFF_ARM: return "ARM";
	case TAB3_XMM_HANDOFF_USBBOOT_ATTACH: return "USBBOOT_ATTACH";
	case TAB3_XMM_HANDOFF_USBBOOT_DETACH: return "USBBOOT_DETACH";
	case TAB3_XMM_HANDOFF_EHCI_OFF_ENTER: return "EHCI_OFF_ENTER";
	case TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_REMOVE: return "EHCI_OFF_AFTER_REMOVE";
	case TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_PHY: return "EHCI_OFF_AFTER_PHY";
	case TAB3_XMM_HANDOFF_EHCI_ON_ENTER: return "EHCI_ON_ENTER";
	case TAB3_XMM_HANDOFF_EHCI_ON_AFTER_PHY: return "EHCI_ON_AFTER_PHY";
	case TAB3_XMM_HANDOFF_EHCI_ADD_BEGIN: return "EHCI_ADD_BEGIN";
	case TAB3_XMM_HANDOFF_EHCI_ADD_END: return "EHCI_ADD_END";
	case TAB3_XMM_HANDOFF_PHY_OWNER_BEFORE: return "PHY_OWNER_BEFORE";
	case TAB3_XMM_HANDOFF_PHY_OWNER_AFTER: return "PHY_OWNER_AFTER";
	case TAB3_XMM_HANDOFF_PHY_HOST_RUNTIME: return "PHY_HOST_RUNTIME";
	case TAB3_XMM_HANDOFF_PORT2_RESET_SET: return "PORT2_RESET_SET";
	case TAB3_XMM_HANDOFF_PORT2_RESET_DONE: return "PORT2_RESET_DONE";
	case TAB3_XMM_HANDOFF_PORT2_RESET_ATTEMPT: return "PORT2_RESET_ATTEMPT";
	case TAB3_XMM_HANDOFF_HUB_INIT_BEGIN: return "HUB_INIT_BEGIN";
	case TAB3_XMM_HANDOFF_GET64_BEGIN: return "GET64_BEGIN";
	case TAB3_XMM_HANDOFF_GET64_DONE: return "GET64_DONE";
	case TAB3_XMM_HANDOFF_GET64_TRY_BEGIN: return "GET64_TRY_BEGIN";
	case TAB3_XMM_HANDOFF_GET64_TRY_DONE: return "GET64_TRY_DONE";
	case TAB3_XMM_HANDOFF_EP0_QTD_STATUS: return "EP0_QTD_STATUS";
	case TAB3_XMM_HANDOFF_EP0_SETUP_PACKET: return "EP0_SETUP_PACKET";
	case TAB3_XMM_HANDOFF_EP0_QH_INFO: return "EP0_QH_INFO";
	case TAB3_XMM_HANDOFF_EP0_QH_OVERLAY: return "EP0_QH_OVERLAY";
	case TAB3_XMM_HANDOFF_EP0_QTD_LINKS: return "EP0_QTD_LINKS";
	case TAB3_XMM_HANDOFF_EP0_ASYNC_STATE: return "EP0_ASYNC_STATE";
	case TAB3_XMM_HANDOFF_EP0_EHCI_REGS: return "EP0_EHCI_REGS";
	case TAB3_XMM_HANDOFF_EP0_EHCI_AUX: return "EP0_EHCI_AUX";
	case TAB3_XMM_HANDOFF_EP0_PHY_REGS: return "EP0_PHY_REGS";
	case TAB3_XMM_HANDOFF_EP0_PHY_TUNE: return "EP0_PHY_TUNE";
	case TAB3_XMM_HANDOFF_EP0_CLOCKS: return "EP0_CLOCKS";
	case TAB3_XMM_HANDOFF_TRANSIENT_HW: return "TRANSIENT_HW";
	case TAB3_XMM_HANDOFF_PORT2_PCD_IRQ: return "PORT2_PCD_IRQ";
	case TAB3_XMM_HANDOFF_PORT2_PE_DROP_QH: return "PORT2_PE_DROP_QH";
	case TAB3_XMM_HANDOFF_PORT2_PE_DROP_QTD: return "PORT2_PE_DROP_QTD";
	case TAB3_XMM_HANDOFF_PORT2_PE_DROP_URB: return "PORT2_PE_DROP_URB";
	case TAB3_XMM_HANDOFF_PORT2_PE_DROP_PHY_PWR: return "PORT2_PE_DROP_PHY_PWR";
	case TAB3_XMM_HANDOFF_PORT2_PE_DROP_PHY_TUNE: return "PORT2_PE_DROP_PHY_TUNE";
	case TAB3_XMM_HANDOFF_PORT2_PE_DROP_PHY_OWNERS: return "PORT2_PE_DROP_PHY_OWNERS";
	case TAB3_XMM_HANDOFF_PORT2_PRPE_READBACK: return "PORT2_PRPE_READBACK";
	case TAB3_XMM_HANDOFF_BOOT_XACT_META: return "BOOT_XACT_META";
	case TAB3_XMM_HANDOFF_BOOT_XACT_QH: return "BOOT_XACT_QH";
	case TAB3_XMM_HANDOFF_BOOT_XACT_QTD: return "BOOT_XACT_QTD";
	case TAB3_XMM_HANDOFF_BOOT_XACT_URB: return "BOOT_XACT_URB";
	case TAB3_XMM_HANDOFF_BOOT_XACT_CLOCKS: return "BOOT_XACT_CLOCKS";
	case TAB3_XMM_HANDOFF_SECOND_RESET_BEGIN: return "SECOND_RESET_BEGIN";
	case TAB3_XMM_HANDOFF_SECOND_RESET_DONE: return "SECOND_RESET_DONE";
	case TAB3_XMM_HANDOFF_SECOND_RESET_SKIPPED: return "SECOND_RESET_SKIPPED";
	case TAB3_XMM_HANDOFF_SET_ADDRESS_BEGIN: return "SET_ADDRESS_BEGIN";
	case TAB3_XMM_HANDOFF_SET_ADDRESS_DONE: return "SET_ADDRESS_DONE";
	case TAB3_XMM_HANDOFF_FULL_DESCRIPTOR: return "FULL_DESCRIPTOR";
	case TAB3_XMM_HANDOFF_HUB_INIT_SUCCESS: return "HUB_INIT_SUCCESS";
	case TAB3_XMM_HANDOFF_HUB_INIT_FAIL: return "HUB_INIT_FAIL";
	case TAB3_XMM_HANDOFF_RUNTIME_CMD_ONLINE: return "RUNTIME_CMD_ONLINE";
	default: return "UNKNOWN";
	}
}

static void tab3_xmm6262_handoff_trace_store(
	enum tab3_xmm6262_handoff_event event,
	u32 a, u32 b, u32 c, u32 d)
{
	struct tab3_xmm6262_handoff_record *rec;
	int idx;

	if (!atomic_read(&tab3_xmm_handoff_trace_armed) ||
	    atomic_read(&tab3_xmm_handoff_trace_frozen))
		return;

	idx = atomic_inc_return(&tab3_xmm_handoff_trace_next) - 1;
	if (idx < 0 || idx >= TAB3_XMM_HANDOFF_TRACE_MAX) {
		atomic_set(&tab3_xmm_handoff_trace_frozen, 1);
		atomic_set(&tab3_xmm_handoff_trace_phase,
			   TAB3_XMM_HANDOFF_PHASE_FROZEN);
		return;
	}

	rec = &tab3_xmm_handoff_trace[idx];
	WRITE_ONCE(rec->valid, 0);
	rec->ns = ktime_get_mono_fast_ns();
	rec->seq = idx + 1;
	rec->event = event;
	rec->cpu = raw_smp_processor_id();
	rec->a = a;
	rec->b = b;
	rec->c = c;
	rec->d = d;
	/* Publish all record fields before readers can observe valid=1. */
	smp_wmb();
	WRITE_ONCE(rec->valid, 1);
}

void tab3_xmm6262_handoff_trace_arm(u32 cookie)
{
	u64 now;

	/*
	 * -1 is a short initialization sentinel: concurrent callbacks see the
	 * recorder as inactive until the ring and phase are fully initialized.
	 */
	if (atomic_cmpxchg(&tab3_xmm_handoff_trace_armed, 0, -1) != 0)
		return;

	now = ktime_get_mono_fast_ns();
	atomic_set(&tab3_xmm_handoff_trace_next, 0);
	atomic_set(&tab3_xmm_handoff_trace_frozen, 0);
	atomic_set(&tab3_xmm_handoff_trace_reset_attempt, 0);
	atomic_set(&tab3_xmm_handoff_ep0_snapshot_claimed, 0);
	atomic_set(&tab3_xmm_handoff_get64_failed, 0);
	atomic_set(&tab3_xmm_handoff_late_boot_xacts, 0);
	atomic_set(&tab3_xmm_handoff_capture_this_reset, 1);
	atomic_set(&tab3_xmm_handoff_first_reset_failure, 0);
	atomic_set(&tab3_xmm_handoff_trace_phase,
		   TAB3_XMM_HANDOFF_PHASE_ARMED);
	WRITE_ONCE(tab3_xmm_handoff_trace_t0_ns, now);
	/* Do not expose armed=1 until the new ring state is initialized. */
	smp_wmb();
	atomic_set(&tab3_xmm_handoff_trace_armed, 1);
	tab3_xmm6262_handoff_trace_store(TAB3_XMM_HANDOFF_ARM,
					 cookie, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_trace_arm);

bool tab3_xmm6262_handoff_trace_active(void)
{
	return atomic_read(&tab3_xmm_handoff_trace_armed) == 1 &&
	       !atomic_read(&tab3_xmm_handoff_trace_frozen);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_trace_active);

/* Exactly one sampling decision per five-record transaction group. The
 * initial failed transaction remains fully recorded. Once GET64 reports an
 * error, keep the next eight complete BOOT_XACT groups, then one group in
 * 32. All control, port reset, PHY and runtime events remain unsampled.
 * Safe in EHCI IRQ context: only atomics, no locks, sleeps or allocations.
 */
bool tab3_xmm6262_boot_xact_should_capture(void)
{
	int n;

	if (!tab3_xmm6262_handoff_trace_active())
		return false;
	if (!atomic_read(&tab3_xmm_handoff_get64_failed))
		return true;

	n = atomic_inc_return(&tab3_xmm_handoff_late_boot_xacts);
	return n <= 8 || (n % 32) == 0;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_boot_xact_should_capture);

bool tab3_xmm6262_handoff_trace_claim_ep0_snapshot(void)
{
	if (!tab3_xmm6262_handoff_trace_active())
		return false;

	return atomic_cmpxchg(&tab3_xmm_handoff_ep0_snapshot_claimed, 0, 1) == 0;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_trace_claim_ep0_snapshot);

void tab3_xmm6262_trace_hw_snapshot(enum tab3_xmm6262_transient_stage stage)
{
	struct tab3_xmm6262 *xmm;
	u32 uphypwr, uphyclk, uphyrst;
	u32 pmu704, pmu708, pmu70c, sys_usb;
	u32 src_top0, div_top, gate_fsys, infra;

	if (!tab3_xmm6262_handoff_trace_active())
		return;
	xmm = READ_ONCE(tab3_xmm_transient_xmm);
	if (!xmm || !xmm->usbphy_base || !xmm->sysreg_base ||
	    !xmm->pmu_base || !xmm->cmu_base)
		return;

	/* v181: direct read-only MMIO snapshot; no regmap/clock locks. */
	uphypwr = readl_relaxed(xmm->usbphy_base + 0x00);
	uphyclk = readl_relaxed(xmm->usbphy_base + 0x04);
	uphyrst = readl_relaxed(xmm->usbphy_base + 0x08);
	pmu704 = readl_relaxed(xmm->pmu_base + TAB3_PMU_USBPHY_CONTROL);
	pmu708 = readl_relaxed(xmm->pmu_base + TAB3_PMU_USBHSIC0_PHY_CONTROL);
	pmu70c = readl_relaxed(xmm->pmu_base + TAB3_PMU_USBHSIC1_PHY_CONTROL);
	sys_usb = readl_relaxed(xmm->sysreg_base + TAB3_SYSREG_USB20PHY_CFG);
	src_top0 = readl_relaxed(xmm->cmu_base + TAB3_CMU_SRC_TOP0);
	div_top = readl_relaxed(xmm->cmu_base + TAB3_CMU_DIV_TOP);
	gate_fsys = readl_relaxed(xmm->cmu_base + TAB3_CMU_GATE_IP_FSYS);

	/*
	 * d packs only the hardware bits relevant to T311 USB PHY20:
	 * [10:0] UPHYRST; raw PMU isolation bits: [16] shared USB PHY,
	 * [17] HSIC0, [18] HSIC1 (0=de-isolated/on, 1=isolated/off);
	 * [19] USB20PHY mode, [20] USB_HOST gate,
	 * [21] mout_aclk133 select, [24:22] aclk133 divider-minus-one.
	 */
	infra = uphyrst & 0x7ff;
	infra |= (pmu704 & 1) << 16;
	infra |= (pmu708 & 1) << 17;
	infra |= (pmu70c & 1) << 18;
	infra |= (sys_usb & 1) << 19;
	infra |= ((gate_fsys >> 12) & 1) << 20;
	infra |= ((src_top0 >> 24) & 1) << 21;
	infra |= ((div_top >> 12) & 0x7) << 22;

	tab3_xmm6262_handoff_trace_store(TAB3_XMM_HANDOFF_TRANSIENT_HW,
					 stage, uphypwr, uphyclk, infra);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_trace_hw_snapshot);

void tab3_xmm6262_handoff_trace_event(enum tab3_xmm6262_handoff_event event,
				      u32 a, u32 b, u32 c, u32 d)
{
	int phase;

	if (!tab3_xmm6262_handoff_trace_active())
		return;

	tab3_xmm6262_handoff_trace_store(event, a, b, c, d);

	if (event == TAB3_XMM_HANDOFF_GET64_DONE && (s32)a < 0)
		atomic_set(&tab3_xmm_handoff_get64_failed, 1);

	phase = atomic_read(&tab3_xmm_handoff_trace_phase);
	if (event == TAB3_XMM_HANDOFF_EHCI_OFF_AFTER_PHY &&
	    phase == TAB3_XMM_HANDOFF_PHASE_ARMED)
		atomic_cmpxchg(&tab3_xmm_handoff_trace_phase,
			       TAB3_XMM_HANDOFF_PHASE_ARMED,
			       TAB3_XMM_HANDOFF_PHASE_EHCI_OFF);
	else if (event == TAB3_XMM_HANDOFF_EHCI_ADD_BEGIN &&
		 phase == TAB3_XMM_HANDOFF_PHASE_EHCI_OFF)
		atomic_cmpxchg(&tab3_xmm_handoff_trace_phase,
			       TAB3_XMM_HANDOFF_PHASE_EHCI_OFF,
			       TAB3_XMM_HANDOFF_PHASE_EHCI_ON);

	if (event == TAB3_XMM_HANDOFF_RUNTIME_CMD_ONLINE) {
		/* The initial GET64_DONE can be an error (e.g. -EPROTO/ -71)
		 * even in a boot that eventually reaches an operational modem.
		 * Do not freeze at the first descriptor failure: keep the
		 * subsequent recovery and enumeration attempts. A failed
		 * reset is also nonterminal and later attempts stay observable.
		 * Freeze successful handoff only once the runtime command
		 * channel is connected. With 512 slots, a full seq_file trace
		 * still fits within the watchdog's 64KiB /proc capture budget.
		 */
		smp_wmb();
		atomic_set(&tab3_xmm_handoff_trace_phase,
			   TAB3_XMM_HANDOFF_PHASE_FROZEN);
		atomic_set(&tab3_xmm_handoff_trace_frozen, 1);
	}
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_trace_event);

void tab3_xmm6262_trace_port2_pcd(u32 status, u32 portsc,
				  u32 reset_done, u32 command)
{
	tab3_xmm6262_handoff_trace_event(TAB3_XMM_HANDOFF_PORT2_PCD_IRQ,
					 status, portsc, reset_done, command);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_trace_port2_pcd);

void tab3_xmm6262_handoff_trace_port2_reset_set(u32 pre, u32 written,
						unsigned long deadline,
						unsigned long now)
{
	int attempt;

	if (!tab3_xmm6262_handoff_trace_active())
		return;
	if (atomic_cmpxchg(&tab3_xmm_handoff_trace_phase,
			   TAB3_XMM_HANDOFF_PHASE_EHCI_ON,
			   TAB3_XMM_HANDOFF_PHASE_RESET_PENDING) !=
	    TAB3_XMM_HANDOFF_PHASE_EHCI_ON)
		return;

	attempt = atomic_inc_return(&tab3_xmm_handoff_trace_reset_attempt);
	/* Repeated -ETIMEDOUT resets are not necessarily terminal. Keep
	 * all of the first eight attempts, then every sixteenth pair.
	 * The first error's RESET_DONE is always captured regardless.
	 */
	atomic_set(&tab3_xmm_handoff_capture_this_reset,
		   attempt <= 8 || (attempt % 16) == 0);
	if (atomic_read(&tab3_xmm_handoff_capture_this_reset)) {
		tab3_xmm6262_handoff_trace_store(
			TAB3_XMM_HANDOFF_PORT2_RESET_ATTEMPT,
			attempt, pre, written, (u32)deadline);
		tab3_xmm6262_handoff_trace_store(
			TAB3_XMM_HANDOFF_PORT2_RESET_SET,
			pre, written, (u32)deadline, (u32)now);
	}
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_trace_port2_reset_set);

void tab3_xmm6262_handoff_trace_port2_reset_done(int ret, u32 pre, u32 post,
						 unsigned long deadline)
{
	bool first_failed;

	if (atomic_read(&tab3_xmm_handoff_trace_phase) !=
	    TAB3_XMM_HANDOFF_PHASE_RESET_PENDING)
		return;

	/* A failed single reset is not terminal: XMM6262 sometimes comes
	 * ONLINE after later retries. Preserve the FIRST failed completion,
	 * sample remaining repeated resets, and keep the recorder live.
	 * All of this is trace bookkeeping only, not actual reset control.
	 */
	first_failed = ret &&
		atomic_cmpxchg(&tab3_xmm_handoff_first_reset_failure,
			       0, 1) == 0;
	if (atomic_read(&tab3_xmm_handoff_capture_this_reset) ||
	    first_failed) {
		tab3_xmm6262_handoff_trace_store(
			TAB3_XMM_HANDOFF_PORT2_RESET_DONE,
			(u32)ret, pre, post, (u32)deadline);
		tab3_xmm6262_trace_hw_snapshot(
			TAB3_XMM_TRANSIENT_PORT2_RESET_DONE);
	}
	atomic_cmpxchg(&tab3_xmm_handoff_trace_phase,
			       TAB3_XMM_HANDOFF_PHASE_RESET_PENDING,
			       TAB3_XMM_HANDOFF_PHASE_EHCI_ON);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_trace_port2_reset_done);

static struct platform_device *tab3_xmm6262_find_device(void)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_find_compatible_node(NULL, NULL, "samsung,tab3-xmm6262");
	if (!np)
		return NULL;
	pdev = of_find_device_by_node(np);
	of_node_put(np);
	return pdev;
}

static int tab3_xmm6262_hostwake_stock_pull_none(struct tab3_xmm6262 *xmm,
						 const char *tag)
{
	unsigned long cfg;
	int ret;

	cfg = pinconf_to_config_packed(PIN_CONFIG_BIAS_DISABLE, 0);
	ret = gpiod_set_config(xmm->host_wake, cfg);

	/* Samsung does not gate modem operation on pull-programming readback. */
	if (ret)
		dev_warn(xmm->dev,
			 "HOST_WAKE pull-none set failed tag=%s ret=%d; continuing\n",
			 tag, ret);

	return 0;
}

static void tab3_xmm6262_slave_wake_stock(struct tab3_xmm6262 *xmm)
{
	int host_raw;
	int slave_before;
	int ret_low = 0;
	int ret_high;

	host_raw = gpiod_get_raw_value_cansleep(xmm->host_wake);
	if (host_raw <= 0) {
		dev_dbg(xmm->dev,
			 "STOCK_SLAVE_WAKE action=NONE host_wake_raw=%d reason=HOST_ACTIVE_LOW\n",
			 host_raw);
		return;
	}

	slave_before = gpiod_get_raw_value_cansleep(xmm->slave_wake);
	if (slave_before > 0) {
		ret_low = gpiod_direction_output_raw(xmm->slave_wake, 0);
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_SLAVEWAKE_LOW, 0, ret_low, false);
		mdelay(10);
	}

	ret_high = gpiod_direction_output_raw(xmm->slave_wake, 1);
	tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_SLAVEWAKE_HIGH, 1, ret_high, false);
	dev_dbg(xmm->dev,
		 "STOCK_SLAVE_WAKE action=PULSE host_wake_raw=%d slave_before=%d ret_low=%d ret_high=%d delay=mdelay10\n",
		 host_raw, slave_before, ret_low, ret_high);
}

/*
 * Exact SM-T311 stock invariant: GPIO_SUSPEND_REQUEST is GPM2(4),
 * configured once during probe as output LOW with its pull disabled.
 * Runtime HSIC paths observe the signal only; they never repair or rewrite it.
 */
static int tab3_xmm6262_susreq_force_low(struct tab3_xmm6262 *xmm,
					 const char *tag)
{
	unsigned long cfg;
	int cfg_ret;
	int ret;

	/*
	 * Exact Samsung T311 board setup: request SUS_REQ, drive it LOW once,
	 * then disable the pull.  Stock does not read the pad back and does not
	 * turn a transient electrical level into a modem-probe failure.
	 */
	ret = gpiod_direction_output_raw(xmm->suspend_request, 0);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "SUS_REQ output-low failed tag=%s\n", tag);

	cfg = pinconf_to_config_packed(PIN_CONFIG_BIAS_DISABLE, 0);
	cfg_ret = gpiod_set_config(xmm->suspend_request, cfg);
	if (cfg_ret)
		dev_warn(xmm->dev,
			 "SUS_REQ pull-disable failed tag=%s ret=%d; continuing with Samsung non-gating semantics\n",
			 tag, cfg_ret);


	dev_dbg(xmm->dev,
		 "SAMSUNG_SUSREQ_SETUP tag=%s output=LOW pull=NONE readback_gate=OFF\n",
		 tag);
	return 0;
}

int tab3_xmm6262_hsic_phy_init_notify(void)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	int cp_reset;
	int ret = 0;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return -ENODEV;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		ret = -EPROBE_DEFER;
		goto out_put;
	}

	mutex_lock(&xmm->lock);
	tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_PHY_INIT);
	/* Stock touches SUS_REQ only during board GPIO setup. */
	cp_reset = gpiod_get_value_cansleep(xmm->cp_reset);
	if (cp_reset > 0) {
		gpiod_set_value_cansleep(xmm->pda_active, 1);
		tab3_xmm6262_slave_wake_stock(xmm);
	}
	dev_dbg(xmm->dev, "HSIC_PHY_INIT_NOTIFY cp_reset=%d\n", cp_reset);
	mutex_unlock(&xmm->lock);
out_put:
	put_device(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_hsic_phy_init_notify);

static int tab3_xmm6262_link_active_stock_write(struct tab3_xmm6262 *xmm,
					       bool on, const char *tag)
{
	u32 dat, con;

	if (!xmm->gpio1_base)
		return -ENODEV;

	/* Exact Samsung Exynos4 gpio_direction_output(): DAT -> CON -> DAT. */
	dat = readl_relaxed(xmm->gpio1_base + TAB3_GPL0_DAT);
	if (on)
		dat |= BIT(0);
	else
		dat &= ~BIT(0);
	writel_relaxed(dat, xmm->gpio1_base + TAB3_GPL0_DAT);

	con = readl_relaxed(xmm->gpio1_base + TAB3_GPL0_CON);
	con &= ~0xfU;
	con |= 0x1U;
	writel_relaxed(con, xmm->gpio1_base + TAB3_GPL0_CON);
	writel_relaxed(dat, xmm->gpio1_base + TAB3_GPL0_DAT);

	xmm->host_link_on = on;
	dev_dbg(xmm->dev, "LINK_ACTIVE_PAD tag=%s requested=%u\n", tag, on);
	return 0;
}

int tab3_xmm6262_hsic_host_state(bool on)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	int cp_reset;
	int ret = 0;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return -ENODEV;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		ret = -EPROBE_DEFER;
		goto out_put;
	}

	mutex_lock(&xmm->lock);
	cp_reset = gpiod_get_value_cansleep(xmm->cp_reset);
	if (cp_reset > 0) {
		int dir_ret;

		dir_ret = tab3_xmm6262_link_active_stock_write(xmm, on,
						      "hsic-host-state");
		if (xmm->diag_first_attempt_started) {
			if (on && !READ_ONCE(xmm->diag_snap[TAB3_XMM_DIAG_BOOT_HOST_ON].valid))
				tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_BOOT_HOST_ON);
			else if (!on && READ_ONCE(xmm->diag_snap[TAB3_XMM_DIAG_BOOT_HOST_ON].valid))
				tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_HANDOFF_HOST_OFF);
			else if (on && READ_ONCE(xmm->diag_snap[TAB3_XMM_DIAG_HANDOFF_HOST_OFF].valid))
				tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_HANDOFF_HOST_ON);
		}
		dev_dbg(xmm->dev,
			 "STOCK_HOST_STATE_DIRECTION requested=%u ret=%d\n",
			 on, dir_ret);
	}
	mutex_unlock(&xmm->lock);
out_put:
	put_device(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_hsic_host_state);

static int __tab3_xmm6262_hsic_link_reconnect(struct tab3_xmm6262 *xmm)
{
	if (!gpiod_get_value_cansleep(xmm->phone_active) ||
	    !gpiod_get_value_cansleep(xmm->cp_reset))
		return -ENODEV;

	/* Exact board-m0-modems.c umts_link_reconnect(). */
	gpiod_set_value_cansleep(xmm->link_active, 0);
	xmm->host_link_on = false;
	mdelay(10);
	tab3_xmm6262_slave_wake_stock(xmm);
	gpiod_set_value_cansleep(xmm->link_active, 1);
	xmm->host_link_on = true;
	dev_dbg(xmm->dev, "HSIC_LINK_RECONNECT link_active=1\n");
	return 0;
}

static const char *tab3_xmm6262_state_name(enum tab3_xmm6262_state state);

int tab3_xmm6262_hsic_link_reconnect(void)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	int ret;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return -ENODEV;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return -EPROBE_DEFER;
	}

	mutex_lock(&xmm->lock);
	ret = __tab3_xmm6262_hsic_link_reconnect(xmm);
	mutex_unlock(&xmm->lock);
	put_device(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_hsic_link_reconnect);

int tab3_xmm6262_get_state(void)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	int state;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return -ENODEV;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return -EPROBE_DEFER;
	}

	mutex_lock(&xmm->lock);
	state = xmm->state;
	mutex_unlock(&xmm->lock);
	put_device(&pdev->dev);
	return state;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_get_state);

bool tab3_xmm6262_phone_active(void)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	bool active = false;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return false;
	xmm = platform_get_drvdata(pdev);
	if (xmm)
		active = gpiod_get_value_cansleep(xmm->phone_active) > 0;
	put_device(&pdev->dev);
	return active;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_phone_active);

void tab3_xmm6262_boot_wake_hold(void)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return;
	xmm = platform_get_drvdata(pdev);
	if (!xmm)
		goto out_put;

	mutex_lock(&xmm->lock);
	if (!xmm->boot_wake_held) {
		pm_stay_awake(xmm->dev);
		xmm->boot_wake_held = true;
		dev_dbg(xmm->dev,
			 "SAMSUNG_BSP_BOOT_WAKE state=HELD source=BOOT_USB_DISCONNECT semantics=STOCK_boot_wake\n");
	}
	mutex_unlock(&xmm->lock);
out_put:
	put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_boot_wake_hold);

void tab3_xmm6262_boot_wake_release(void)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return;
	xmm = platform_get_drvdata(pdev);
	if (!xmm)
		goto out_put;

	mutex_lock(&xmm->lock);
	if (xmm->boot_wake_held) {
		pm_relax(xmm->dev);
		xmm->boot_wake_held = false;
		dev_dbg(xmm->dev,
			 "SAMSUNG_BSP_BOOT_WAKE state=RELEASED source=RUNTIME_CMD_OR_CLEANUP semantics=STOCK_boot_wake\n");
	}
	mutex_unlock(&xmm->lock);
out_put:
	put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_boot_wake_release);

void tab3_xmm6262_fmt_open(bool open)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return;
	}

	mutex_lock(&xmm->lock);
	if (open)
		xmm->fmt_opened++;
	else if (xmm->fmt_opened)
		xmm->fmt_opened--;
	mutex_unlock(&xmm->lock);
	put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_fmt_open);

bool tab3_xmm6262_sim_changed(void)
{
	/* SM-T311 stock has CONFIG_SIM_DETECT=n; no modem SIM GPIO exists. */
	return false;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_sim_changed);

void tab3_xmm6262_poll_state(struct file *file, poll_table *wait)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return;
	}

	poll_wait(file, &xmm->state_wait, wait);
	put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_poll_state);

void tab3_xmm6262_set_state(enum tab3_xmm6262_state state)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return;
	}

	mutex_lock(&xmm->lock);
	if (xmm->state != state) {
		enum tab3_xmm6262_state old_state = xmm->state;

		dev_dbg(xmm->dev, "MODEM_STATE %s->%s stock_numeric=%u\n",
			 tab3_xmm6262_state_name(old_state),
			 tab3_xmm6262_state_name(state), (unsigned int)state);
		xmm->state = state;
	}
	if (state == TAB3_XMM_CRASH_RESET || state == TAB3_XMM_CRASH_EXIT ||
	    state == TAB3_XMM_NV_REBUILDING)
		wake_up_interruptible(&xmm->state_wait);
	mutex_unlock(&xmm->lock);
	put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_set_state);

int tab3_xmm6262_register_hostwake_cb(tab3_xmm6262_hostwake_cb_t cb,
				      void *data)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	int ret = 0;

	if (!cb)
		return -EINVAL;
	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return -ENODEV;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return -EPROBE_DEFER;
	}

	mutex_lock(&xmm->lock);
	if (xmm->runtime_hostwake_cb &&
	    (xmm->runtime_hostwake_cb != cb || xmm->runtime_hostwake_data != data))
		ret = -EBUSY;
	else {
		xmm->runtime_hostwake_cb = cb;
		xmm->runtime_hostwake_data = data;
	}
	mutex_unlock(&xmm->lock);
	put_device(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_register_hostwake_cb);

void tab3_xmm6262_unregister_hostwake_cb(tab3_xmm6262_hostwake_cb_t cb,
					void *data)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return;
	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		put_device(&pdev->dev);
		return;
	}

	mutex_lock(&xmm->lock);
	if (xmm->runtime_hostwake_cb == cb && xmm->runtime_hostwake_data == data) {
		xmm->runtime_hostwake_cb = NULL;
		xmm->runtime_hostwake_data = NULL;
	}
	mutex_unlock(&xmm->lock);
	put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_unregister_hostwake_cb);

static const char *tab3_xmm6262_state_name(enum tab3_xmm6262_state state)
{
	switch (state) {
	case TAB3_XMM_OFFLINE:
		return "OFFLINE";
	case TAB3_XMM_BOOTING:
		return "BOOTING";
	case TAB3_XMM_ONLINE:
		return "ONLINE";
	case TAB3_XMM_NV_REBUILDING:
		return "NV_REBUILDING";
	case TAB3_XMM_LOADER_DONE:
		return "LOADER_DONE";
	case TAB3_XMM_SIM_ATTACH:
		return "SIM_ATTACH";
	case TAB3_XMM_SIM_DETACH:
		return "SIM_DETACH";
	case TAB3_XMM_CRASH_RESET:
		return "CRASH_RESET";
	case TAB3_XMM_CRASH_EXIT:
		return "CRASH_EXIT";
	default:
		return "UNKNOWN";
	}
}

/*
 * Samsung's board-m0-modems.c temporarily drove AP<->CP inputs low around
 * XMM6262 reset/power transitions to avoid reverse-biasing the CP.
 */
static int tab3_xmm6262_irq_lines_force_low(struct tab3_xmm6262 *xmm)
{
	u32 con, dat;

	if (!xmm->gpio1_base)
		return -ENODEV;

	/* PHONE_ACTIVE and HOST_WAKE: exact output-low reverse-bias window. */
	dat = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_DAT);
	dat &= ~(BIT(1) | BIT(6));
	writel_relaxed(dat, xmm->gpio1_base + TAB3_GPX1_DAT);

	con = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_CON);
	con &= ~((0xfU << 4) | (0xfU << 24));
	con |= (0x1U << 4) | (0x1U << 24);
	writel_relaxed(con, xmm->gpio1_base + TAB3_GPX1_CON);
	writel_relaxed(dat, xmm->gpio1_base + TAB3_GPX1_DAT);

	return 0;
}

static void tab3_xmm6262_irq_lines_restore_eint(struct tab3_xmm6262 *xmm)
{
	u32 con;

	con = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_CON);
	con &= ~((0xfU << 4) | (0xfU << 24));
	con |= (0xfU << 4) | (0xfU << 24);
	writel_relaxed(con, xmm->gpio1_base + TAB3_GPX1_CON);
}

/* Mainline-safe translation of stock xmm_gpio_revers_bias_clear(). */
static int tab3_xmm6262_reverse_bias_clear(struct tab3_xmm6262 *xmm)
{
	int ret;

	ret = gpiod_direction_output(xmm->pda_active, 0);
	if (ret)
		return ret;
	ret = gpiod_direction_output(xmm->link_active, 0);
	if (ret)
		return ret;
	ret = gpiod_direction_output(xmm->slave_wake, 0);
	if (ret)
		return ret;

	ret = gpiod_direction_output(xmm->cp_dump_int, 0);
	if (ret)
		return ret;

	ret = tab3_xmm6262_irq_lines_force_low(xmm);
	if (ret)
		return ret;

	xmm->host_link_on = false;
	dev_dbg(xmm->dev,
		 "REVERSE_BIAS_CLEAR stock=PHONE_ACTIVE_LOW,HOST_WAKE_LOW cp-dump=out0 rf_touch=UNTOUCHED delay_ms=20\n");
	msleep(20);
	return 0;
}


static int tab3_xmm6262_reverse_bias_restore(struct tab3_xmm6262 *xmm)
{
	int ret;

	ret = gpiod_direction_input(xmm->cp_dump_int);
	if (ret)
		return ret;

	tab3_xmm6262_irq_lines_restore_eint(xmm);

	ret = tab3_xmm6262_hostwake_stock_pull_none(xmm,
						      "reverse-bias-restore");
	if (ret)
		return ret;


	dev_dbg(xmm->dev,
		 "REVERSE_BIAS_RESTORE stock=PHONE_ACTIVE_EINT,HOST_WAKE_EINT cp-dump=input rf_touch=UNTOUCHED\n");
	return 0;
}


static int tab3_xmm6262_power_on(struct tab3_xmm6262 *xmm)
{
	int ret;

	/*
	 * Samsung CBD directly owns OHCI/EHCI around MODEM_ON.
	 * MODEM_ON itself remains GPIO/reset only, matching xmm6262_on().
	 */
	dev_dbg(xmm->dev,
		 "MODEM_ON_HSIC action=UNCHANGED owner=CBD_DIRECT policy=STOCK_GPIO_ONLY\n");

	ret = tab3_xmm6262_reverse_bias_clear(xmm);
	if (ret)
		return ret;
	tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_BIAS_CLEAR_DONE);

	/* Exact Samsung XMM6262 power-on timing. */
	gpiod_set_value_cansleep(xmm->reset_req_n, 0);
	gpiod_set_value_cansleep(xmm->cp_on, 0);
	/* T311: complete CP_ON posted write before advancing the stock pulse. */
	(void)readl_relaxed(xmm->gpio1_base + TAB3_GPL2_DAT);
	gpiod_set_value_cansleep(xmm->cp_reset, 0);
	msleep(100);

	gpiod_set_value_cansleep(xmm->cp_reset, 1);
	msleep(50);
	gpiod_set_value_cansleep(xmm->reset_req_n, 1);
	tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_CP_RESET_HIGH);
	tab3_xmm6262_diag_capture_cp_on_wave(xmm, TAB3_XMM_CPON_BEFORE_HIGH);

	gpiod_set_value_cansleep(xmm->cp_on, 1);
	(void)readl_relaxed(xmm->gpio1_base + TAB3_GPL2_DAT);
	tab3_xmm6262_diag_capture_cp_on_wave(xmm, TAB3_XMM_CPON_AFTER_HIGH);
	udelay(60);
	tab3_xmm6262_diag_capture_cp_on_wave(xmm, TAB3_XMM_CPON_AFTER_60US);
	gpiod_set_value_cansleep(xmm->cp_on, 0);
	(void)readl_relaxed(xmm->gpio1_base + TAB3_GPL2_DAT);
	tab3_xmm6262_diag_capture_cp_on_wave(xmm, TAB3_XMM_CPON_AFTER_LOW);
	msleep(20);
	tab3_xmm6262_diag_capture_cp_on_wave(xmm, TAB3_XMM_CPON_AFTER_20MS);
	tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_CP_ON_PULSE_DONE);

	/* Stock publishes BOOTING before restoring the IRQ GPIO directions. */
	xmm->state = TAB3_XMM_BOOTING;

	ret = tab3_xmm6262_reverse_bias_restore(xmm);
	if (ret)
		return ret;
	tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_BIAS_RESTORE_DONE);

	xmm->powered = true;

	/* Stock modemctl owns PDA_ACTIVE here; LINK_ACTIVE belongs to link-PM. */
	gpiod_set_value_cansleep(xmm->pda_active, 1);
	xmm->host_link_on = false;

	dev_dbg(xmm->dev,
		 "CP_CORE_POWER_ON exact-stock-sequence complete link_active=0\n");
	return 0;
}

static void tab3_xmm6262_power_off(struct tab3_xmm6262 *xmm)
{
	/*
	 * Exact Samsung XMM6262 modem-off hardware contract: only CP_ON and
	 * CP_RESET are driven low.  LINK_ACTIVE/PDA_ACTIVE/SLAVE_WAKE belong
	 * to the link-PM and board-state machines and are deliberately not
	 * rewritten here.
	 */
	gpiod_set_value_cansleep(xmm->cp_on, 0);
	gpiod_set_value_cansleep(xmm->cp_reset, 0);


	xmm->powered = false;
	dev_dbg(xmm->dev,
		 "SAMSUNG_BSP_MODEM_OFF cp_on=0 cp_reset=0 link_pda_slave=UNCHANGED state=IRQ_OWNED\n");
}

static int tab3_xmm6262_reboot_notify(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct tab3_xmm6262 *xmm =
		container_of(nb, struct tab3_xmm6262, reboot_nb);

	if (action != SYS_RESTART && action != SYS_HALT &&
	    action != SYS_POWER_OFF)
		return NOTIFY_DONE;

	/*
	 * Samsung 3.0.31 reached modem_off() at the very front of the
	 * platform-device shutdown sequence, before OHCI/EHCI shutdown.
	 * Modern device_shutdown() takes parent/device locks before invoking
	 * ->shutdown().  Force the stock CP-off hardware state from the reboot
	 * notifier chain, which runs before device_shutdown(), so those modern
	 * locks cannot leave an active XMM6262 blocking global reboot.
	 *
	 * Do not take xmm->lock here.  v162 ->shutdown() remains as an
	 * idempotent second stock-parity CP-off when device_shutdown() reaches
	 * this driver.
	 */
	dev_emerg(xmm->dev,
		  "TAB3_V164_XMM_EARLY_CP_OFF start action=%lu powered=%u state=%s\n",
		  action, xmm->powered, tab3_xmm6262_state_name(xmm->state));
	tab3_xmm6262_power_off(xmm);
	WRITE_ONCE(xmm->state, TAB3_XMM_OFFLINE);
	dev_emerg(xmm->dev,
		  "TAB3_V164_XMM_EARLY_CP_OFF done cp_on=0 cp_reset=0 state=OFFLINE\n");

	return NOTIFY_DONE;
}

static int tab3_xmm6262_reset(struct tab3_xmm6262 *xmm)
{
	int ret;

	ret = tab3_xmm6262_reverse_bias_clear(xmm);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(xmm->cp_reset, 0);
	gpiod_set_value_cansleep(xmm->reset_req_n, 0);
	xmm->state = TAB3_XMM_OFFLINE;
	msleep(20);

	gpiod_set_value_cansleep(xmm->cp_reset, 1);
	udelay(160);
	gpiod_set_value_cansleep(xmm->reset_req_n, 1);
	udelay(100);

	gpiod_set_value_cansleep(xmm->cp_on, 1);
	udelay(60);
	gpiod_set_value_cansleep(xmm->cp_on, 0);
	msleep(20);

	ret = tab3_xmm6262_reverse_bias_restore(xmm);
	if (ret)
		return ret;

	xmm->state = TAB3_XMM_BOOTING;
	xmm->powered = true;
	xmm->host_link_on = false;
	dev_dbg(xmm->dev,
		 "CP_CORE_RESET stock-parity pda_active=0 link_active=0\n");
	return 0;
}


static bool tab3_xmm6262_if_usb_connected;

void tab3_xmm6262_usb_connected_set(bool connected)
{
	WRITE_ONCE(tab3_xmm6262_if_usb_connected, connected);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_usb_connected_set);

bool tab3_xmm6262_usb_connected_get(void)
{
	return READ_ONCE(tab3_xmm6262_if_usb_connected);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_usb_connected_get);

struct tab3_xmm_runtime_match {
	bool connected;
};

static int tab3_xmm6262_runtime_usb_match_cb(struct usb_device *udev, void *data)
{
	struct tab3_xmm_runtime_match *match = data;

	if (le16_to_cpu(udev->descriptor.idVendor) == XMM_RUNTIME_VENDOR &&
	    le16_to_cpu(udev->descriptor.idProduct) == XMM_RUNTIME_PRODUCT) {
		match->connected = true;
		return 1;
	}

	return 0;
}

static bool tab3_xmm6262_runtime_usb_connected(void)
{
	struct tab3_xmm_runtime_match match = { };

	usb_for_each_dev(&match, tab3_xmm6262_runtime_usb_match_cb);
	return match.connected;
}

long tab3_xmm6262_modem_compat_ioctl(unsigned int cmd)
{
	struct platform_device *pdev;
	struct tab3_xmm6262 *xmm;
	long ret = 0;

	pdev = tab3_xmm6262_find_device();
	if (!pdev)
		return -ENODEV;

	xmm = platform_get_drvdata(pdev);
	if (!xmm) {
		ret = -EPROBE_DEFER;
		goto out_put;
	}

	mutex_lock(&xmm->lock);

	switch (cmd) {
	case IOCTL_MODEM_ON:
		if (!xmm->diag_first_attempt_started) {
			xmm->diag_first_attempt_started = true;
			tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_MODEM_ON_ENTRY);
		}
		/*
		 * Stock sipc4_io_device.c dispatches every MODEM_ON directly
		 * to xmm6262_on(), and xmm6262_on() always performs the full
		 * reset/power pulse sequence.  Do not turn CBD retries into
		 * no-ops merely because the previous attempt left powered=1.
		 */
		dev_dbg(xmm->dev,
			 "MODEM_ON_FULL_RETRY was_powered=%u state=%s policy=STOCK_ALWAYS_FULL_SEQUENCE\n",
			 xmm->powered, tab3_xmm6262_state_name(xmm->state));
		ret = tab3_xmm6262_power_on(xmm);
		if (!ret)
			tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_MODEM_ON_DONE);
		break;

	case IOCTL_MODEM_OFF:
		/* Stock dispatches every MODEM_OFF directly to xmm6262_off(). */
		tab3_xmm6262_power_off(xmm);
		break;

	case IOCTL_MODEM_RESET:
		ret = tab3_xmm6262_reset(xmm);
		break;

	case IOCTL_MODEM_BOOT_ON:
	case IOCTL_MODEM_BOOT_OFF:
		ret = 0;
		break;

	case IOCTL_MODEM_BOOT_DONE:
		/* Exact stock XMM6262 sipc4 behavior: BOOT_DONE is a no-op. */
		ret = 0;
		break;

	case IOCTL_MODEM_STATUS:
		ret = xmm->state;
		if (ret == TAB3_XMM_CRASH_RESET || ret == TAB3_XMM_CRASH_EXIT)
			break;
		if (ret == TAB3_XMM_NV_REBUILDING)
			xmm->state = TAB3_XMM_ONLINE;
		break;

	case IOCTL_MODEM_FORCE_CRASH_EXIT:
		gpiod_set_value_cansleep(xmm->ap_dump_int, 1);
		ret = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&xmm->lock);

	dev_dbg(xmm->dev,
		 "MODEM_IOCTL cmd=0x%x ret=%ld state=%s stock_numeric=%u\n",
		 cmd, ret, tab3_xmm6262_state_name(xmm->state),
		 (unsigned int)xmm->state);

out_put:
	put_device(&pdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_modem_compat_ioctl);

static int tab3_xmm6262_link_pm_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct tab3_xmm6262 *xmm;

	xmm = container_of(misc, struct tab3_xmm6262, link_pm_miscdev);
	file->private_data = xmm;

	return nonseekable_open(inode, file);
}

static int tab3_xmm6262_link_pm_set_bias_clear(struct tab3_xmm6262 *xmm,
						 bool clear)
{
	u32 con, dat;
	int ret;

	if (!xmm->gpio1_base)
		return -ENODEV;
	ret = gpiod_direction_output(xmm->slave_wake, 0);
	if (ret)
		return ret;

	if (clear) {
		dat = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_DAT) & ~BIT(1);
		writel_relaxed(dat, xmm->gpio1_base + TAB3_GPX1_DAT);
		con = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_CON);
		con &= ~(0xfU << 4);
		con |= 0x1U << 4;
		writel_relaxed(con, xmm->gpio1_base + TAB3_GPX1_CON);
		writel_relaxed(dat, xmm->gpio1_base + TAB3_GPX1_DAT);
	} else {
		con = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_CON);
		con &= ~(0xfU << 4);
		con |= 0xfU << 4;
		writel_relaxed(con, xmm->gpio1_base + TAB3_GPX1_CON);
		irq_set_irq_type(xmm->host_wake_irq, IRQ_TYPE_EDGE_BOTH);
	}

	return 0;
}

static long tab3_xmm6262_link_pm_ioctl(struct file *file, unsigned int cmd,
				      unsigned long arg)
{
	struct tab3_xmm6262 *xmm = file->private_data;
	int value;
	long ret = 0;

	switch (cmd) {
	case IOCTL_LINK_CONTROL_ENABLE:
		if (copy_from_user(&value, (const void __user *)arg, sizeof(value)))
			return -EFAULT;
		/* Samsung's T311 link_ldo_enable() is a hardware no-op. */
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_CONTROL_ENABLE, !!value, 0, false);
		dev_dbg(xmm->dev, "link control enable=%d (board no-op)\n",
			!!value);
		return 0;

	case IOCTL_LINK_CONTROL_ACTIVE:
		if (copy_from_user(&value, (const void __user *)arg, sizeof(value)))
			return -EFAULT;

		/*
		 * Stock modem_link_device_hsic.c uses gpio_set_value() here.
		 * Reprogramming direction belongs only to the EHCI host-state
		 * callback (set_host_states()).
		 */
		mutex_lock(&xmm->lock);
		gpiod_set_value_cansleep(xmm->link_active, !!value);
		xmm->host_link_on = !!value;
		if (!value)
			tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_CONTROL_ACTIVE_LOW);
		mutex_unlock(&xmm->lock);
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_CONTROL_ACTIVE, !!value, 0, false);
		dev_dbg(xmm->dev,
			 "LINK_PM CONTROL_ACTIVE=%d\n", !!value);
		return 0;

	case IOCTL_LINK_GET_HOSTWAKE:
		value = gpiod_get_raw_value_cansleep(xmm->host_wake);
		ret = value < 0 ? value : !value;
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_GET_HOSTWAKE, value, ret, true);
		dev_dbg(xmm->dev,
			 "LINK_PM GET_HOSTWAKE raw=%d active=%ld\n",
			 value, ret);
		return ret;

	case IOCTL_LINK_CONNECTED:
		ret = tab3_xmm6262_usb_connected_get();
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_CONNECTED, -1, ret, true);
		dev_dbg(xmm->dev,
			 "LINK_PM CONNECTED boot_or_main=%ld\n", ret);
		return ret;

	case IOCTL_LINK_SET_BIAS_CLEAR:
		if (copy_from_user(&value, (const void __user *)arg, sizeof(value)))
			return -EFAULT;

		mutex_lock(&xmm->lock);

		ret = tab3_xmm6262_link_pm_set_bias_clear(xmm, !!value);
		if (!ret)
			ret = gpiod_get_raw_value_cansleep(xmm->phone_active);

		mutex_unlock(&xmm->lock);
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_SET_BIAS_CLEAR, !!value, ret, false);

		dev_dbg(xmm->dev,
			 "LINK_PM SET_BIAS_CLEAR=%d phone_active=%ld stock_direction_semantics=1\n",
			 !!value, ret);
		return ret;

	case IOCTL_LINK_GET_PHONEACTIVE:
		ret = gpiod_get_raw_value_cansleep(xmm->phone_active);
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_GET_PHONEACTIVE, ret, ret, true);
		dev_dbg(xmm->dev,
			 "LINK_PM GET_PHONEACTIVE=%ld\n", ret);
		return ret;

	default:
		break;
	}

	/* Samsung modem_link_device_hsic.c treats unknown legacy link-PM ioctls as no-op. */
	return 0;
}

static const struct file_operations tab3_xmm6262_link_pm_fops = {
	.owner = THIS_MODULE,
	.open = tab3_xmm6262_link_pm_open,
	.unlocked_ioctl = tab3_xmm6262_link_pm_ioctl,
};

static irqreturn_t tab3_xmm6262_phone_active_thread(int irq, void *data)
{
	struct tab3_xmm6262 *xmm = data;
	int cp_reset = gpiod_get_value_cansleep(xmm->cp_reset);
	int phone_active = gpiod_get_value_cansleep(xmm->phone_active);
	int cp_dump = gpiod_get_value_cansleep(xmm->cp_dump_int);
	unsigned int next_type;

	mutex_lock(&xmm->lock);
	if (cp_reset && phone_active) {
		xmm->state = TAB3_XMM_BOOTING;
	} else if (xmm->dev->power.is_suspended && !phone_active) {
		/* Exact stock DPM-resume failure path. */
		tab3_xmm6262_power_off(xmm);
		xmm->state = TAB3_XMM_CRASH_RESET;
		dev_dbg(xmm->dev,
			 "SAMSUNG_BSP_PHONE_IRQ suspended=1 phone_active=0 action=MODEM_OFF_CRASH_RESET\n");
	} else if (cp_reset && !phone_active) {
		xmm->state = cp_dump ? TAB3_XMM_CRASH_EXIT : TAB3_XMM_CRASH_RESET;
	} else {
		xmm->state = TAB3_XMM_OFFLINE;
	}
	if (xmm->state == TAB3_XMM_CRASH_RESET ||
	    xmm->state == TAB3_XMM_CRASH_EXIT)
		wake_up_interruptible(&xmm->state_wait);
	mutex_unlock(&xmm->lock);

	/* Match Samsung's dynamic level polarity handling. */
	next_type = phone_active ? IRQ_TYPE_LEVEL_LOW : IRQ_TYPE_LEVEL_HIGH;
	irq_set_irq_type(irq, next_type);
	dev_dbg(xmm->dev,
		 "PHONE_ACTIVE reset=%d active=%d dump=%d state=%s\n",
		 cp_reset, phone_active, cp_dump,
		 tab3_xmm6262_state_name(xmm->state));
	return IRQ_HANDLED;
}

static irqreturn_t tab3_xmm6262_host_wake_thread(int irq, void *data)
{
	struct tab3_xmm6262 *xmm = data;
	int value;

	value = gpiod_get_raw_value_cansleep(xmm->host_wake);
	tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_HOSTWAKE_IRQ, value, 0, true);
	/*
	 * Samsung stock modem_link_device_hsic.c:link_pm_irq_handler()
	 * deasserts GPIO_LINK_SLAVEWAKE when HOSTWAKE goes HIGH.  The
	 * original modem expects this full handshake, not a SLAVEWAKE
	 * line stuck HIGH after the AP's initial wake pulse.  Only the
	 * idle-level IRQ clears it; never modify the GPIO on the active
	 * low (CP requests AP wake) IRQ, and never reset the baseband.
	 */
	if (value > 0 && gpiod_get_raw_value_cansleep(xmm->slave_wake) > 0) {
		gpiod_set_raw_value_cansleep(xmm->slave_wake, 0);
		tab3_xmm6262_link_trace_add(xmm, TAB3_XMM_TRACE_SLAVEWAKE_LOW,
					  0, 0, false);
	}
	if (!tab3_xmm6262_runtime_usb_connected())
		return IRQ_HANDLED;

	value = gpiod_get_value_cansleep(xmm->host_wake);
	mutex_lock(&xmm->lock);
	if (xmm->runtime_hostwake_cb) {
		tab3_xmm6262_hostwake_cb_t cb = xmm->runtime_hostwake_cb;
		void *cb_data = xmm->runtime_hostwake_data;

		mutex_unlock(&xmm->lock);
		cb(value == 0, cb_data);
	} else {
		mutex_unlock(&xmm->lock);
	}
	dev_dbg(xmm->dev,
		 "HOST_WAKE_RUNTIME_IRQ raw=%d active=%u powered=%u host_link=%u\n",
		 value, value == 0, xmm->powered, xmm->host_link_on);
	return IRQ_HANDLED;
}

static ssize_t power_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", xmm->powered);
}

static ssize_t power_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	bool on;
	int ret = 0;

	ret = kstrtobool(buf, &on);
	if (ret)
		return ret;

	mutex_lock(&xmm->lock);
	if (on && !xmm->powered)
		ret = tab3_xmm6262_power_on(xmm);
	else if (!on && xmm->powered)
		tab3_xmm6262_power_off(xmm);
	mutex_unlock(&xmm->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR(cp_power, 0644, power_show, power_store);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	bool reset;
	int ret;

	ret = kstrtobool(buf, &reset);
	if (ret)
		return ret;
	if (!reset)
		return count;

	mutex_lock(&xmm->lock);
	ret = tab3_xmm6262_reset(xmm);
	mutex_unlock(&xmm->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(reset);

static ssize_t host_link_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", xmm->host_link_on);
}

static ssize_t host_link_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	bool on;
	int ret;

	ret = kstrtobool(buf, &on);
	if (ret)
		return ret;

	mutex_lock(&xmm->lock);
	if (on) {
		if (!xmm->powered) {
			ret = -EPERM;
			goto out;
		}
		gpiod_set_value_cansleep(xmm->link_active, 1);
		xmm->host_link_on = true;
		dev_dbg(xmm->dev, "HOST_LINK_ON link_active=1\n");
	} else {
		gpiod_set_value_cansleep(xmm->slave_wake, 0);
		gpiod_set_value_cansleep(xmm->link_active, 0);
		xmm->host_link_on = false;
		dev_dbg(xmm->dev, "HOST_LINK_OFF link_active=0 slave_wake=0\n");
	}
	ret = 0;
out:
	mutex_unlock(&xmm->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(host_link);

static ssize_t reconnect_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	bool run;
	int ret;

	ret = kstrtobool(buf, &run);
	if (ret)
		return ret;
	if (!run)
		return count;
	mutex_lock(&xmm->lock);
	ret = __tab3_xmm6262_hsic_link_reconnect(xmm);
	mutex_unlock(&xmm->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(reconnect);

static ssize_t diag_snapshots_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	ssize_t len = 0;
	unsigned int i;

	for (i = 0; i < TAB3_XMM_DIAG_STAGE_COUNT && len < PAGE_SIZE; i++) {
		const struct tab3_xmm6262_diag_snapshot *snap = &xmm->diag_snap[i];
		u32 gpm2_dat, gpl0_dat, gpl1_dat, gpx1_dat;

		if (!READ_ONCE(snap->valid))
			continue;
		gpm2_dat = READ_ONCE(snap->gpm2_dat);
		gpl0_dat = READ_ONCE(snap->gpl0_dat);
		gpl1_dat = READ_ONCE(snap->gpl1_dat);
		gpx1_dat = READ_ONCE(snap->gpx1_dat);
		len += sysfs_emit_at(buf, len,
			"%s state=%u powered=%u host_link=%u gpm2_con=%#010x gpm2_dat=%#010x gpm2_pud=%#010x susreq=%u link=%u pda=%u hostwake=%u slavewake=%u phone=%u gpx1_con=%#010x phy0_phytune=%#010x phy1_phytune=%#010x\n",
			tab3_xmm6262_diag_stage_name[i], snap->state,
			snap->powered, snap->host_link, snap->gpm2_con,
			gpm2_dat, snap->gpm2_pud, !!(gpm2_dat & BIT(4)),
			!!(gpl0_dat & BIT(0)), !!(gpl1_dat & BIT(0)),
			!!(gpx1_dat & BIT(1)), !!(gpx1_dat & BIT(0)),
			!!(gpx1_dat & BIT(6)), snap->gpx1_con,
			snap->phy0_phytune, snap->phy1_phytune);
	}

	for (i = 0; i < TAB3_XMM_CPON_WAVE_COUNT && len < PAGE_SIZE; i++) {
		const struct tab3_xmm6262_cp_on_wave_sample *sample =
			&xmm->diag_cp_on_wave[i];
		u32 gpl2_dat, gpm2_dat;

		if (!READ_ONCE(sample->valid))
			continue;
		gpl2_dat = READ_ONCE(sample->gpl2_dat);
		gpm2_dat = READ_ONCE(sample->gpm2_dat);
		len += sysfs_emit_at(buf, len,
			"cp_on_wave %s gpl2_dat=%#010x cp_on=%u gpm2_dat=%#010x susreq=%u\n",
			tab3_xmm6262_cp_on_wave_name[i], gpl2_dat,
			!!(gpl2_dat & BIT(5)), gpm2_dat, !!(gpm2_dat & BIT(4)));
	}

	/* Live read happens only when userspace reads sysfs, after the test window. */
	if (len < PAGE_SIZE) {
		u32 gpm2_con = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_CON);
		u32 gpm2_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_DAT);
		u32 gpm2_pud = readl_relaxed(xmm->gpio1_base + TAB3_GPM2_PUD);
		u32 gpl0_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPL0_DAT);
		u32 gpl1_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPL1_DAT);
		u32 gpx1_dat = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_DAT);
		u32 gpx1_con = readl_relaxed(xmm->gpio1_base + TAB3_GPX1_CON);
		u32 phy0_phytune = readl_relaxed(xmm->usbphy_base + TAB3_USBPHY_PHY0_PHYTUNE);
		u32 phy1_phytune = readl_relaxed(xmm->usbphy_base + TAB3_USBPHY_PHY1_PHYTUNE);

		len += sysfs_emit_at(buf, len,
			"live gpm2_con=%#010x gpm2_dat=%#010x gpm2_pud=%#010x susreq=%u link=%u pda=%u hostwake=%u slavewake=%u phone=%u gpx1_con=%#010x phy0_phytune=%#010x phy1_phytune=%#010x\n",
			gpm2_con, gpm2_dat, gpm2_pud, !!(gpm2_dat & BIT(4)),
			!!(gpl0_dat & BIT(0)), !!(gpl1_dat & BIT(0)),
			!!(gpx1_dat & BIT(1)), !!(gpx1_dat & BIT(0)),
			!!(gpx1_dat & BIT(6)), gpx1_con,
			phy0_phytune, phy1_phytune);
	}

	return len;
}
static DEVICE_ATTR_RO(diag_snapshots);


static ssize_t handoff_trace_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tab3_xmm6262_phy_diag phy = {};
	ssize_t len = 0;
	u64 t0 = READ_ONCE(tab3_xmm_handoff_trace_t0_ns);
	int count = min_t(int, atomic_read(&tab3_xmm_handoff_trace_next),
			  TAB3_XMM_HANDOFF_TRACE_MAX);
	int phase = atomic_read(&tab3_xmm_handoff_trace_phase);
	int frozen = atomic_read(&tab3_xmm_handoff_trace_frozen);
	int armed = atomic_read(&tab3_xmm_handoff_trace_armed);
	int i, ret;

	len += sysfs_emit_at(buf, len,
		"# v184 recorder armed=%d frozen=%d phase=%d count=%d max=%d reset_attempt=%d\n",
		armed, frozen, phase, count, TAB3_XMM_HANDOFF_TRACE_MAX,
		atomic_read(&tab3_xmm_handoff_trace_reset_attempt));
	len += sysfs_emit_at(buf, len, "# dt_us seq cpu event a b c d\n");
	len += sysfs_emit_at(buf, len,
		"# reset_attempt:a=n b=pre c=write d=deadline reset_done:a=ret b=pre c=post d=deadline\n");
	len += sysfs_emit_at(buf, len,
		"# get64_done:a=maxp/ret b=vidpid c=len/type/maxp/speed d=retry\n");
	len += sysfs_emit_at(buf, len,
		"# get64_try_begin:a=try b=size c=first_time d=devnum get64_try_done:a=try b=rc c=len/type/maxp/speed d=devnum\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_qtd_status:a=token b=status c=qtd_len<<16|xacterrs d=urb_actual_length\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_setup_packet:a=setup0 b=setup1 c=setup_dma d=qtd_dma\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_qh_info:a=info1 b=info2 c=current d=qh_dma ep0_qh_overlay:a=next b=alt c=token d=buf0\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_qtd_links:a=next b=alt c=buf0 d=buf0_hi ep0_async_state:a=head_next b=qh_next c=qh_state<<16|xacterrs d=pipe\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_ehci_regs:a=cmd b=sts c=configflag d=portsc2 ep0_ehci_aux:a=intr b=async c=txfill d=insnreg00\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_phy_regs:a=uphypwr b=uphyclk c=uphyrst d=host_runtime_flags ep0_phy_tune:a=phy0 b=phy1 c=ext0123 d=ohci_runtime_suspended_or_error\n");
	len += sysfs_emit_at(buf, len,
		"# ep0_clocks:a=usbhost_hz b=aclk133_hz c=mout_aclk133_hz d=aclk133_div_minus_one_or_error\n"
		"# transient_hw:a=stage b=uphypwr c=uphyclk d=infra\n"
		"# boot_xact_meta:a=token b=meta c=qtd_len d=urb_actual\n"
		"# boot_xact_qh:a=info1 b=info2 c=current d=qh_dma\n"
		"# boot_xact_qtd:a=overlay b=qtd c=qtd_dma d=buf0\n"
		"# boot_xact_urb:a=pipe b=status c=actual d=requested\n"
		"# boot_xact_clocks:a=usbhost b=aclk133 c=mout d=div_or_error\n");

	for (i = 0; i < count && len < PAGE_SIZE; i++) {
		const struct tab3_xmm6262_handoff_record *rec =
			&tab3_xmm_handoff_trace[i];
		u64 dt_us;

		if (!READ_ONCE(rec->valid))
			continue;
		/* valid=1 is the writer's publish point for all preceding fields. */
		smp_rmb();
		dt_us = t0 && rec->ns >= t0 ?
			div_u64(rec->ns - t0, 1000) : 0;
		len += sysfs_emit_at(buf, len,
			"%llu %u %u %s %x %x %x %x\n",
			(unsigned long long)dt_us, rec->seq, rec->cpu,
			tab3_xmm6262_handoff_event_name(rec->event),
			rec->a, rec->b, rec->c, rec->d);
	}

	ret = tab3_t311_phy_diag_snapshot(&phy);
	if (!ret && len < PAGE_SIZE)
		len += sysfs_emit_at(buf, len,
			"# live_phy uphypwr=%#010x uphyclk=%#010x uphyrst=%#010x phy0_phytune=%#010x phy1_phytune=%#010x ext=[%d,%d,%d,%d] host_runtime_flags=%#010x ohci_runtime_suspended=%u\n",
			phy.uphypwr, phy.uphyclk, phy.uphyrst,
			phy.phy0_phytune, phy.phy1_phytune,
			phy.ext_cnt[0], phy.ext_cnt[1],
			phy.ext_cnt[2], phy.ext_cnt[3],
			phy.host_runtime_flags, phy.ohci_runtime_suspended);
	else if (ret && len < PAGE_SIZE)
		len += sysfs_emit_at(buf, len,
			"# live_phy unavailable ret=%d\n", ret);

	return len;
}
static DEVICE_ATTR_RO(handoff_trace);

static int tab3_xmm6262_handoff_trace_proc_show(struct seq_file *m, void *v)
{
	struct tab3_xmm6262_phy_diag phy = {};
	u64 t0 = READ_ONCE(tab3_xmm_handoff_trace_t0_ns);
	int count = min_t(int, atomic_read(&tab3_xmm_handoff_trace_next),
			  TAB3_XMM_HANDOFF_TRACE_MAX);
	int phase = atomic_read(&tab3_xmm_handoff_trace_phase);
	int frozen = atomic_read(&tab3_xmm_handoff_trace_frozen);
	int armed = atomic_read(&tab3_xmm_handoff_trace_armed);
	int i, ret;

	seq_printf(m,
		   "# v184 recorder armed=%d frozen=%d phase=%d count=%d max=%d reset_attempt=%d\n",
		   armed, frozen, phase, count, TAB3_XMM_HANDOFF_TRACE_MAX,
		   atomic_read(&tab3_xmm_handoff_trace_reset_attempt));
	seq_puts(m, "# dt_us seq cpu event a b c d\n");
	seq_puts(m,
		 "# port2_pcd_irq:a=irq_status b=portsc2 c=reset_done_jiffies d=ehci_command\n");
	seq_puts(m,
		 "# reset_attempt:a=n b=pre c=write d=deadline reset_done:a=ret b=pre c=post d=deadline\n");
	seq_puts(m,
		 "# get64_done:a=maxp/ret b=vidpid c=len/type/maxp/speed d=retry\n");
	seq_puts(m,
		 "# get64_try_begin:a=try b=size c=first_time d=devnum get64_try_done:a=try b=rc c=len/type/maxp/speed d=devnum\n");
	seq_puts(m,
		 "# ep0_qtd_status:a=token b=status c=qtd_len<<16|xacterrs d=urb_actual_length\n");
	seq_puts(m,
		 "# ep0_setup_packet:a=setup0 b=setup1 c=setup_dma d=qtd_dma\n");
	seq_puts(m,
		 "# ep0_qh_info:a=info1 b=info2 c=current d=qh_dma ep0_qh_overlay:a=next b=alt c=token d=buf0\n");
	seq_puts(m,
		 "# ep0_qtd_links:a=next b=alt c=buf0 d=buf0_hi ep0_async_state:a=head_next b=qh_next c=qh_state<<16|xacterrs d=pipe\n");
	seq_puts(m,
		 "# ep0_ehci_regs:a=cmd b=sts c=configflag d=portsc2 ep0_ehci_aux:a=intr b=async c=txfill d=insnreg00\n");
	seq_puts(m,
		 "# ep0_phy_regs:a=uphypwr b=uphyclk c=uphyrst d=host_runtime_flags ep0_phy_tune:a=phy0 b=phy1 c=ext0123 d=ohci_runtime_suspended_or_error\n");
	seq_puts(m,
		 "# ep0_clocks:a=usbhost_hz b=aclk133_hz c=mout_aclk133_hz d=aclk133_div_minus_one_or_error\n");
	seq_puts(m,
		 "# transient_hw:a=stage b=uphypwr c=uphyclk d=rst[10:0]|pmu_usb16|pmu_hsic0_17|pmu_hsic1_18|mode19|gate20|mux21|div[24:22]\n");
	seq_puts(m, "# boot_xact_meta:a=token b=meta c=qtd_len d=urb_actual\n");
	seq_puts(m, "# boot_xact_qh:a=info1 b=info2 c=current d=qh_dma\n");
	seq_puts(m, "# boot_xact_qtd:a=overlay b=qtd c=qtd_dma d=buf0\n");
	seq_puts(m, "# boot_xact_urb:a=pipe b=status c=actual d=requested\n");
	seq_puts(m,
		 "# boot_xact_clocks:a=usbhost b=aclk133 c=mout d=div_or_error\n");

	for (i = 0; i < count; i++) {
		const struct tab3_xmm6262_handoff_record *rec =
			&tab3_xmm_handoff_trace[i];
		u64 dt_us;

		if (!READ_ONCE(rec->valid))
			continue;
		/* valid=1 is the writer's publish point for all preceding fields. */
		smp_rmb();
		dt_us = t0 && rec->ns >= t0 ?
			div_u64(rec->ns - t0, 1000) : 0;
		seq_printf(m, "%llu %u %u %s %x %x %x %x\n",
			   (unsigned long long)dt_us, rec->seq, rec->cpu,
			   tab3_xmm6262_handoff_event_name(rec->event),
			   rec->a, rec->b, rec->c, rec->d);
	}

	ret = tab3_t311_phy_diag_snapshot(&phy);
	if (!ret)
		seq_printf(m,
			   "# live_phy uphypwr=%#010x uphyclk=%#010x uphyrst=%#010x phy0_phytune=%#010x phy1_phytune=%#010x ext=[%d,%d,%d,%d] host_runtime_flags=%#010x ohci_runtime_suspended=%u\n",
			   phy.uphypwr, phy.uphyclk, phy.uphyrst,
			   phy.phy0_phytune, phy.phy1_phytune,
			   phy.ext_cnt[0], phy.ext_cnt[1],
			   phy.ext_cnt[2], phy.ext_cnt[3],
			   phy.host_runtime_flags, phy.ohci_runtime_suspended);
	else
		seq_printf(m, "# live_phy unavailable ret=%d\n", ret);

	return 0;
}

static ssize_t link_trace_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	struct tab3_xmm6262_link_trace *snap;
	unsigned long flags;
	ssize_t len = 0;
	u32 n, i;

	/* Sysfs show() may sleep.  Keep the one-kilobyte trace snapshot off
	 * the small ARM kernel stack; the spinlock protects only the copy.
	 */
	snap = kmalloc(sizeof(xmm->link_trace), GFP_KERNEL);
	if (!snap)
		return -ENOMEM;

	spin_lock_irqsave(&xmm->link_trace_lock, flags);
	n = min_t(u32, xmm->link_trace_count, TAB3_XMM_LINK_TRACE_MAX);
	memcpy(snap, xmm->link_trace, sizeof(xmm->link_trace));
	spin_unlock_irqrestore(&xmm->link_trace_lock, flags);

	for (i = 0; i < n && len < PAGE_SIZE; i++)
		len += sysfs_emit_at(buf, len,
			"seq=%u first_ns=%llu last_ns=%llu kind=%s value=%d ret=%d repeats=%u\n",
			snap[i].seq, (unsigned long long)snap[i].first_ns,
			(unsigned long long)snap[i].last_ns,
			tab3_xmm6262_link_trace_name(snap[i].kind), snap[i].value,
			snap[i].ret, snap[i].repeats);
	kfree(snap);
	return len;
}
static DEVICE_ATTR_RO(link_trace);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tab3_xmm6262 *xmm = dev_get_drvdata(dev);
	int reset_req = gpiod_get_value_cansleep(xmm->reset_req_n);
	int cp_on = gpiod_get_value_cansleep(xmm->cp_on);
	int cp_reset = gpiod_get_value_cansleep(xmm->cp_reset);
	int pda = gpiod_get_value_cansleep(xmm->pda_active);
	int phone = gpiod_get_value_cansleep(xmm->phone_active);
	int link = gpiod_get_value_cansleep(xmm->link_active);
	int hostwake = gpiod_get_value_cansleep(xmm->host_wake);
	int slavewake = gpiod_get_value_cansleep(xmm->slave_wake);
	int suspend_req = gpiod_get_value_cansleep(xmm->suspend_request);

	return sysfs_emit(buf,
		"state=%s powered=%u host_link=%u reset_req=%d cp_on=%d "
		"cp_reset=%d pda_active=%d phone_active=%d link_active=%d "
		"host_wake=%d slave_wake=%d suspend_request=%d\n",
		tab3_xmm6262_state_name(xmm->state), xmm->powered,
		xmm->host_link_on, reset_req, cp_on, cp_reset, pda, phone,
		link, hostwake, slavewake, suspend_req);
}
static DEVICE_ATTR_RO(state);

static struct attribute *tab3_xmm6262_attrs[] = {
	&dev_attr_cp_power.attr,
	&dev_attr_reset.attr,
	&dev_attr_host_link.attr,
	&dev_attr_reconnect.attr,
	&dev_attr_state.attr,
	&dev_attr_diag_snapshots.attr,
	&dev_attr_handoff_trace.attr,
	&dev_attr_link_trace.attr,
	NULL,
};
static const struct attribute_group tab3_xmm6262_group = {
	.attrs = tab3_xmm6262_attrs,
};

static int tab3_xmm6262_get_gpio_asis(struct tab3_xmm6262 *xmm,
				       struct gpio_desc **gpio,
				       const char *name)
{
	*gpio = devm_gpiod_get(xmm->dev, name, GPIOD_ASIS);
	if (IS_ERR(*gpio))
		return dev_err_probe(xmm->dev, PTR_ERR(*gpio),
				     "D2A failed to acquire %s GPIO\n", name);
	dev_dbg(xmm->dev, "D2A_GPIO_ACQUIRED name=%s\n", name);
	return 0;
}

static int tab3_xmm6262_get_gpios(struct tab3_xmm6262 *xmm)
{
	int ret;

	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->reset_req_n, "reset-request");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->cp_on, "cp-on");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->cp_reset, "cp-reset");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->pda_active, "pda-active");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->phone_active, "phone-active");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->cp_dump_int, "cp-dump");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->ap_dump_int, "ap-dump");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->link_active, "link-active");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->host_wake, "host-wake");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->slave_wake, "slave-wake");
	if (ret) return ret;
	ret = tab3_xmm6262_get_gpio_asis(xmm, &xmm->suspend_request, "suspend-request");
	if (ret) return ret;

	dev_dbg(xmm->dev, "D2A_GPIO_ACQUIRED_COUNT=11\n");
	return 0;
}

static int tab3_xmm6262_d2a_output_low(struct tab3_xmm6262 *xmm,
				       struct gpio_desc *gpio,
				       const char *name)
{
	int ret;

	ret = gpiod_direction_output(gpio, 0);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "D2A failed OUT_LOW for %s\n", name);
	dev_dbg(xmm->dev,
		 "D2A_CORE_GPIO_CONFIG name=%s dir=out value=0 readback_gate=OFF\n",
		 name);
	return 0;
}

static int tab3_xmm6262_d2a_input(struct tab3_xmm6262 *xmm,
				  struct gpio_desc *gpio,
				  const char *name)
{
	int ret;

	ret = gpiod_direction_input(gpio);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "D2A failed INPUT for %s\n", name);
	dev_dbg(xmm->dev, "D2A_CORE_GPIO_CONFIG name=%s dir=in\n", name);
	return 0;
}

static int tab3_xmm6262_config_core_gpios_d2a(struct tab3_xmm6262 *xmm)
{
	int ret;

	/* Exact Samsung umts_modem_cfg_gpio() core ordering, minus IRQ/pull/sleep. */
	ret = tab3_xmm6262_d2a_output_low(xmm, xmm->reset_req_n, "reset-request");
	if (ret) return ret;
	ret = tab3_xmm6262_d2a_output_low(xmm, xmm->cp_on, "cp-on");
	if (ret) return ret;
	ret = tab3_xmm6262_d2a_output_low(xmm, xmm->cp_reset, "cp-reset");
	if (ret) return ret;
	ret = tab3_xmm6262_d2a_output_low(xmm, xmm->pda_active, "pda-active");
	if (ret) return ret;
	ret = tab3_xmm6262_d2a_input(xmm, xmm->phone_active, "phone-active");
	if (ret) return ret;
	ret = tab3_xmm6262_d2a_input(xmm, xmm->cp_dump_int, "cp-dump");
	if (ret) return ret;
	ret = tab3_xmm6262_d2a_output_low(xmm, xmm->ap_dump_int, "ap-dump");
	if (ret) return ret;
	ret = tab3_xmm6262_susreq_force_low(xmm, "probe-core-gpio");
	if (ret) return ret;

	dev_dbg(xmm->dev, "D2A_CORE_GPIO_CONFIG_COUNT=8\n");
	return 0;
}

static int tab3_xmm6262_d2b_output_low(struct tab3_xmm6262 *xmm,
                                       struct gpio_desc *gpio,
                                       const char *name)
{
	int ret;

	ret = gpiod_direction_output(gpio, 0);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "D2B failed OUT_LOW for %s\n", name);
	dev_dbg(xmm->dev,
		 "D2B_LINK_PM_GPIO_CONFIG name=%s dir=out value=0 readback_gate=OFF\n",
		 name);
	return 0;
}

static int tab3_xmm6262_d2b_input(struct tab3_xmm6262 *xmm,
                                  struct gpio_desc *gpio,
                                  const char *name)
{
	int ret;

	ret = gpiod_direction_input(gpio);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "D2B failed INPUT for %s\n", name);
	dev_dbg(xmm->dev, "D2B_LINK_PM_GPIO_CONFIG name=%s dir=in\n", name);
	return 0;
}

static int tab3_xmm6262_config_link_pm_gpios_d2b(struct tab3_xmm6262 *xmm)
{
	int ret;

	/* Samsung modem_link_pm_config_gpio() directions only. IRQ type is D3. */
	ret = tab3_xmm6262_d2b_output_low(xmm, xmm->link_active, "link-active");
	if (ret)
		return ret;
	ret = tab3_xmm6262_d2b_input(xmm, xmm->host_wake, "host-wake");
	if (ret)
		return ret;
	ret = tab3_xmm6262_hostwake_stock_pull_none(xmm, "probe-link-pm");
	if (ret)
		return ret;
	ret = tab3_xmm6262_d2b_output_low(xmm, xmm->slave_wake, "slave-wake");
	if (ret)
		return ret;

	dev_dbg(xmm->dev, "D2B_LINK_PM_GPIO_CONFIG_COUNT=3\n");
	return 0;
}

static int tab3_xmm6262_map_irqs_d3a(struct tab3_xmm6262 *xmm)
{
	xmm->phone_active_irq = gpiod_to_irq(xmm->phone_active);
	if (xmm->phone_active_irq < 0)
		return dev_err_probe(xmm->dev, xmm->phone_active_irq,
				     "D3A failed to map PHONE_ACTIVE IRQ\n");
	dev_dbg(xmm->dev, "D3A_IRQ_MAPPED name=phone-active irq=%d\n",
		 xmm->phone_active_irq);

	xmm->host_wake_irq = gpiod_to_irq(xmm->host_wake);
	if (xmm->host_wake_irq < 0)
		return dev_err_probe(xmm->dev, xmm->host_wake_irq,
				     "D3A failed to map HOST_WAKE IRQ\n");
	dev_dbg(xmm->dev, "D3A_IRQ_MAPPED name=host-wake irq=%d\n",
		 xmm->host_wake_irq);

	if (xmm->phone_active_irq == xmm->host_wake_irq)
		return dev_err_probe(xmm->dev, -EINVAL,
				     "D3A modem IRQ collision phone=%d host=%d\n",
				     xmm->phone_active_irq, xmm->host_wake_irq);

	dev_dbg(xmm->dev, "D3A_IRQ_MAPPED_COUNT=2 sim_detect=STOCK_DISABLED\n");
	return 0;
}

static int tab3_xmm6262_request_irqs_d3b(struct tab3_xmm6262 *xmm)
{
	int ret;

	/*
	 * Stock modemctl final PHONE_ACTIVE request semantics:
	 * IRQF_NO_SUSPEND | IRQF_TRIGGER_HIGH.
	 * We use a threaded handler because descriptor GPIO reads may sleep.
	 * IRQF_ONESHOT keeps the level IRQ masked while the thread runs.
	 */
	ret = devm_request_threaded_irq(xmm->dev, xmm->phone_active_irq,
					NULL, tab3_xmm6262_phone_active_thread,
					IRQF_NO_SUSPEND | IRQF_TRIGGER_HIGH |
					IRQF_ONESHOT,
					"xmm6262-phone-active", xmm);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "D3B failed to request PHONE_ACTIVE IRQ %d\n",
				     xmm->phone_active_irq);
	dev_dbg(xmm->dev,
		 "D3B_IRQ_REQUESTED name=phone-active irq=%d trigger=high no_suspend=1 oneshot=1 wake=0\n",
		 xmm->phone_active_irq);

	ret = devm_request_threaded_irq(xmm->dev, xmm->host_wake_irq,
					NULL, tab3_xmm6262_host_wake_thread,
					IRQF_NO_SUSPEND | IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"xmm6262-host-wake", xmm);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "D3B failed to request HOST_WAKE IRQ %d\n",
				     xmm->host_wake_irq);
	dev_dbg(xmm->dev,
		 "D3B_IRQ_REQUESTED name=host-wake irq=%d trigger=edge-both no_suspend=1 oneshot=1 wake=0 active_low=1\n",
		 xmm->host_wake_irq);

	dev_dbg(xmm->dev, "D3B_IRQ_REQUESTED_COUNT=2 wake=0 power=0 sim_detect=STOCK_DISABLED\n");
	return 0;
}

static void tab3_xmm6262_disable_wake(void *data)
{
	struct tab3_xmm6262 *xmm = data;

	if (xmm->host_wake_enabled) {
		disable_irq_wake(xmm->host_wake_irq);
		xmm->host_wake_enabled = false;
	}
	if (xmm->phone_wake_enabled) {
		disable_irq_wake(xmm->phone_active_irq);
		xmm->phone_wake_enabled = false;
	}
	device_init_wakeup(xmm->dev, false);
}

static int tab3_xmm6262_enable_wake(struct tab3_xmm6262 *xmm)
{
	int ret;

	ret = device_init_wakeup(xmm->dev, true);
	if (ret)
		return dev_err_probe(xmm->dev, ret,
				     "device_init_wakeup failed\n");

	ret = enable_irq_wake(xmm->phone_active_irq);
	if (ret) {
		device_init_wakeup(xmm->dev, false);
		return dev_err_probe(xmm->dev, ret,
				     "PHONE_ACTIVE enable_irq_wake failed\n");
	}
	xmm->phone_wake_enabled = true;
	dev_dbg(xmm->dev, "WAKE_ENABLED name=phone-active irq=%d\n",
		 xmm->phone_active_irq);

	ret = enable_irq_wake(xmm->host_wake_irq);
	if (ret) {
		disable_irq_wake(xmm->phone_active_irq);
		xmm->phone_wake_enabled = false;
		device_init_wakeup(xmm->dev, false);
		return dev_err_probe(xmm->dev, ret,
				     "HOST_WAKE enable_irq_wake failed\n");
	}
	xmm->host_wake_enabled = true;
	dev_dbg(xmm->dev, "WAKE_ENABLED name=host-wake irq=%d\n",
		 xmm->host_wake_irq);

	ret = devm_add_action_or_reset(xmm->dev,
				       tab3_xmm6262_disable_wake, xmm);
	if (ret)
		return ret;

	dev_dbg(xmm->dev, "WAKE_ENABLED_COUNT=2\n");
	return 0;
}

static int tab3_xmm6262_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tab3_xmm6262 *xmm;
	int ret;

	xmm = devm_kzalloc(dev, sizeof(*xmm), GFP_KERNEL);
	if (!xmm)
		return -ENOMEM;

	xmm->dev = dev;
	xmm->state = TAB3_XMM_OFFLINE;
	xmm->gpio1_base = devm_ioremap(dev, TAB3_EXYNOS_GPIO1_PHYS,
					 TAB3_EXYNOS_GPIO1_SIZE);
	if (!xmm->gpio1_base)
		return dev_err_probe(dev, -ENOMEM,
				     "failed to map Exynos GPIO1 register window\n");
	xmm->usbphy_base = devm_ioremap(dev, TAB3_EXYNOS_USBPHY_PHYS,
					  TAB3_EXYNOS_USBPHY_SIZE);
	if (!xmm->usbphy_base)
		return dev_err_probe(dev, -ENOMEM,
				     "failed to map Exynos USB PHY register window\n");
	xmm->sysreg_base = devm_ioremap(dev, TAB3_EXYNOS_SYSREG_PHYS, TAB3_EXYNOS_SYSREG_SIZE);
	xmm->pmu_base = devm_ioremap(dev, TAB3_EXYNOS_PMU_PHYS, TAB3_EXYNOS_PMU_SIZE);
	xmm->cmu_base = devm_ioremap(dev, TAB3_EXYNOS_CMU_PHYS, TAB3_EXYNOS_CMU_SIZE);
	if (!xmm->sysreg_base || !xmm->pmu_base || !xmm->cmu_base)
		dev_warn(dev,
			 "v181 transient PMU/CMU snapshot mapping unavailable; modem behavior unchanged\n");
	mutex_init(&xmm->lock);
	spin_lock_init(&xmm->link_trace_lock);
	init_waitqueue_head(&xmm->state_wait);
	platform_set_drvdata(pdev, xmm);

	/* Samsung CBD directly owns the native OHCI/EHCI HCD lifecycle. */

	ret = tab3_xmm6262_get_gpios(xmm);
	if (ret)
		return ret;

	ret = tab3_xmm6262_config_core_gpios_d2a(xmm);
	if (ret)
		return ret;

	ret = tab3_xmm6262_config_link_pm_gpios_d2b(xmm);
	if (ret)
		return ret;
	tab3_xmm6262_diag_capture(xmm, TAB3_XMM_DIAG_PROBE);

	ret = tab3_xmm6262_map_irqs_d3a(xmm);
	if (ret)
		return ret;

	ret = tab3_xmm6262_request_irqs_d3b(xmm);
	if (ret)
		return ret;

	ret = tab3_xmm6262_enable_wake(xmm);
	if (ret)
		return ret;

	ret = devm_device_add_group(dev, &tab3_xmm6262_group);
	if (ret)
		return ret;

	xmm->link_pm_miscdev.minor = MISC_DYNAMIC_MINOR;
	xmm->link_pm_miscdev.name = "link_pm";
	xmm->link_pm_miscdev.fops = &tab3_xmm6262_link_pm_fops;
	xmm->link_pm_miscdev.parent = dev;

	ret = misc_register(&xmm->link_pm_miscdev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register /dev/link_pm\n");

	xmm->reboot_nb.notifier_call = tab3_xmm6262_reboot_notify;
	xmm->reboot_nb.priority = 255;
	ret = devm_register_reboot_notifier(dev, &xmm->reboot_nb);
	if (ret) {
		misc_deregister(&xmm->link_pm_miscdev);
		return dev_err_probe(dev, ret,
				     "failed to register XMM6262 reboot notifier\n");
	}
	xmm->handoff_trace_proc =
		proc_create_single_data("tab3_xmm6262_handoff_trace", 0444, NULL,
				tab3_xmm6262_handoff_trace_proc_show, xmm);
	if (!xmm->handoff_trace_proc)
		dev_warn(dev,
			 "v182 full handoff trace proc export unavailable; modem behavior unchanged\n");
	/* Publish the recorder target only after the platform probe fully succeeded. */
	WRITE_ONCE(tab3_xmm_transient_xmm, xmm);
	dev_dbg(dev, "STOCK_LINK_PM_ABI=READY node=/dev/link_pm\n");
	dev_dbg(dev, "LINK_ACTIVE_PARITY=READY link_active=GPL0_0 sequence=DAT_CON_DAT owner=CBD_DIRECT\n");
	dev_dbg(dev, "XMM6262 HCI ownership: CBD direct, native OHCI/EHCI\n");
	dev_dbg(dev, "CBD_BOOT_ORDER=READY pre_modem=OHCI_OFF_EHCI_OFF modem_on=GPIO_ONLY post_modem=EHCI_ON_OHCI_ON\n");
	dev_info(dev, "XMM6262 modem control ready (version %s, CBD direct HCD ownership)\n",
		 TAB3_XMM6262_DRV_VERSION);
	dev_dbg(xmm->dev,
		 "STOCK_BOOT_GLUE=READY susreq=OUTPUT_LOW_STOCK_INVARIANT hostwake_irq=IGNORE_UNTIL_RUNTIME_USB\n");
	dev_dbg(xmm->dev,
		 "SUSREQ_POLICY=PROBE_LOW_NO_RUNTIME_ACCESS\n");
	dev_dbg(xmm->dev,
		 "REVERSE_BIAS_COMPAT=READY scope=T311 stock_window=PHONE_ACTIVE_LOW+HOST_WAKE_LOW rf_touch=UNTOUCHED restore_after_CPON_plus_20ms\n");
	dev_dbg(dev,
		 "STOCK_MODEM_SEMANTICS=READY sim_detect=disabled_config_n rf_touch=UNTOUCHED boot_done=noop online=runtime-cmd hostwake-bias=stock\n");
	return 0;
}

static void tab3_xmm6262_shutdown(struct platform_device *pdev)
{
	struct tab3_xmm6262 *xmm = platform_get_drvdata(pdev);

	if (!xmm)
		return;

	dev_emerg(xmm->dev,
		  "TAB3_V162_XMM_SHUTDOWN_STOCK_ATOMIC start powered=%u state=%s\n",
		  xmm->powered, tab3_xmm6262_state_name(xmm->state));

	/*
	 * Samsung 3.0.31 modem_if shutdown does not serialize against the
	 * live modem/link-PM state machine.  It unconditionally drives CP_ON
	 * and CP_RESET low, then publishes STATE_OFFLINE.  Waiting for the
	 * runtime mutex here can deadlock global device_shutdown() if CBD,
	 * link-PM or an IRQ path happens to own it during reboot.
	 *
	 * Keep the stock hardware contract exact: do not touch LINK_ACTIVE,
	 * PDA_ACTIVE or SLAVE_WAKE, and do not make CP-off conditional on the
	 * software powered flag.
	 */
	tab3_xmm6262_power_off(xmm);
	xmm->state = TAB3_XMM_OFFLINE;

	dev_emerg(xmm->dev,
		  "TAB3_V162_XMM_SHUTDOWN_STOCK_ATOMIC done cp_on=0 cp_reset=0 state=OFFLINE\n");
}

static void tab3_xmm6262_remove(struct platform_device *pdev)
{
	struct tab3_xmm6262 *xmm = platform_get_drvdata(pdev);

	if (xmm) {
		if (READ_ONCE(tab3_xmm_transient_xmm) == xmm)
			WRITE_ONCE(tab3_xmm_transient_xmm, NULL);
		proc_remove(xmm->handoff_trace_proc);
		xmm->handoff_trace_proc = NULL;
		misc_deregister(&xmm->link_pm_miscdev);

		mutex_lock(&xmm->lock);
		if (xmm->powered)
			tab3_xmm6262_power_off(xmm);
		else if (xmm->boot_wake_held) {
			pm_relax(xmm->dev);
			xmm->boot_wake_held = false;
		}
		mutex_unlock(&xmm->lock);
	}
	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id tab3_xmm6262_of_match[] = {
	{ .compatible = "samsung,tab3-xmm6262" },
	{ }
};
MODULE_DEVICE_TABLE(of, tab3_xmm6262_of_match);

static struct platform_driver tab3_xmm6262_driver = {
	.probe = tab3_xmm6262_probe,
	.shutdown = tab3_xmm6262_shutdown,
	.remove = tab3_xmm6262_remove,
	.driver = {
		.name = "samsung-tab3-xmm6262",
		.of_match_table = tab3_xmm6262_of_match,
	},
};
static int __init tab3_xmm6262_init(void)
{
	return platform_driver_register(&tab3_xmm6262_driver);
}

static void __exit tab3_xmm6262_exit(void)
{
	platform_driver_unregister(&tab3_xmm6262_driver);
}

#if IS_BUILTIN(CONFIG_SAMSUNG_TAB3_XMM6262)
/* Samsung board-m0-modems.c registers the XMM6262 device at late_initcall. */
late_initcall_sync(tab3_xmm6262_init);
#else
module_init(tab3_xmm6262_init);
#endif
module_exit(tab3_xmm6262_exit);

MODULE_AUTHOR("OpenAI / SM-T311 porting work");
MODULE_DESCRIPTION("Samsung Galaxy Tab 3 SM-T311 XMM6262 stock power/HSIC control");
MODULE_LICENSE("GPL");
