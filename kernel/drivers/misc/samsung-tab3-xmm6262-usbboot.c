// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Galaxy Tab 3 SM-T311 XMM6262 boot transport.
 *
 * Persistent /dev/umts_boot0 with stock modem_if I/O semantics.
 *
 * Samsung's stock modem_if keeps the IPC_BOOT io_device alive while the
 * underlying HSIC USB link disconnects/re-enumerates.  Stock cbd opens
 * /dev/umts_boot0 once, cycles EHCI, powers the CP, and keeps using the
 * same file descriptor after 058b:0041 reconnects.
 *
 * Do the same on mainline: the misc device is registered independently
 * of the USB identity. USB probe/disconnect only attach/detach the current
 * 058b:0041 backend from one persistent port object.
 *
 * This restores the stock IPC_BOOT file semantics on top of that persistent
 * lifetime: asynchronous TX, one RX URB resubmitted only after status 0,
 * empty read returns 0, poll reports only readable data, and close purges RX.
 */

#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/samsung-tab3-xmm6262.h>

#define TAB3_XMM_BOOT_VID       0x058b
#define TAB3_XMM_BOOT_PID       0x0041
#define TAB3_XMM_BOOT_CHUNK     0x4008
#define TAB3_XMM_BOOT_RX_MAX    (16 * 1024)
#define TAB3_XMM_BOOT_RX_FRAG   (4096 - 512)
#define TAB3_XMM_BOOT_ALLOC_OVERHEAD 6
#define TAB3_XMM_BOOT_RX_RETRY_MS 50
#define TAB3_XMM_BOOT_TIMEOUT   5000

struct tab3_xmm_boot_port {
	struct miscdevice miscdev;
	struct mutex lock;
	wait_queue_head_t wait;

	struct usb_device *udev;
	struct usb_interface *ctrl_intf;
	struct usb_interface *data_intf;
	u8 bulk_in;
	u8 bulk_out;
	bool connected;

	struct sk_buff_head rx_queue;
	struct delayed_work rx_retry_work;
	unsigned int rx_retry_cnt;
	struct urb *rx_urb;
	u8 *rx_buf;

	struct sk_buff_head tx_queue;
	struct workqueue_struct *tx_wq;
	struct delayed_work tx_work;
	struct wakeup_source *tx_wake;

};


struct tab3_xmm_tx_ctx {
	struct usb_device *udev;
	struct sk_buff *skb;
	bool hw_reset;
};

static struct tab3_xmm_boot_port tab3_xmm_boot_port;
static struct usb_driver tab3_xmm_boot_driver;

#define TAB3_XMM_HANDOFF_QUIESCE_FAILSAFE_MS 2000
static atomic_t tab3_xmm_handoff_quiesce = ATOMIC_INIT(0);
static unsigned long tab3_xmm_handoff_quiesce_deadline;
static atomic_t tab3_xmm_host_rearm_pending = ATOMIC_INIT(0);
static unsigned long tab3_xmm_host_rearm_deadline;
static atomic_t tab3_xmm_hwreset_trace_seq = ATOMIC_INIT(0);

/*
 * v180: trace-only recorder for the complete 058b:0041 bootrom exchange.
 *
 * The existing handoff recorder intentionally arms only after a successful
 * HWRESET TX completion.  That makes it impossible to compare the USB boot
 * traffic which immediately precedes the 058b -> 1519 transition.  Keep a
 * separate circular RAM recorder here so the complete bootrom TX/RX sequence is
 * observable without printk, sleeps, MMIO reads or changes to USB ordering.
 *
 * Transfer submit events are recorded only after usb_submit_urb() returns.
 * HWRESET completion is recorded only after the existing quiesce/trace arm
 * logic has run.  Therefore the recorder cannot publish a descriptor or add a
 * barrier before the packet being diagnosed.
 */
#define TAB3_XMM_BOOTROM_TRACE_MAX 1024

enum tab3_xmm_bootrom_trace_event {
	TAB3_XMM_BOOTROM_ATTACH = 1,
	TAB3_XMM_BOOTROM_RX_SUBMIT,
	TAB3_XMM_BOOTROM_RX_DONE,
	TAB3_XMM_BOOTROM_TX_DONE,
	TAB3_XMM_BOOTROM_HWRESET_SUBMIT,
	TAB3_XMM_BOOTROM_HWRESET_DONE,
	TAB3_XMM_BOOTROM_DETACH,
};

struct tab3_xmm_bootrom_trace_record {
	u64 ns;
	u32 seq;
	u16 event;
	u16 cpu;
	s32 status;
	u32 len;
	u32 head0;
	u32 head1;
	u32 aux;
	u32 valid;
};

static struct tab3_xmm_bootrom_trace_record
	tab3_xmm_bootrom_trace[TAB3_XMM_BOOTROM_TRACE_MAX];
static atomic_t tab3_xmm_bootrom_trace_next = ATOMIC_INIT(0);
static u64 tab3_xmm_bootrom_trace_t0_ns;
static struct proc_dir_entry *tab3_xmm_bootrom_trace_proc;

static const char *tab3_xmm_bootrom_trace_event_name(u16 event)
{
	switch (event) {
	case TAB3_XMM_BOOTROM_ATTACH: return "ATTACH";
	case TAB3_XMM_BOOTROM_RX_SUBMIT: return "RX_SUBMIT";
	case TAB3_XMM_BOOTROM_RX_DONE: return "RX_DONE";
	case TAB3_XMM_BOOTROM_TX_DONE: return "TX_DONE";
	case TAB3_XMM_BOOTROM_HWRESET_SUBMIT: return "HWRESET_SUBMIT";
	case TAB3_XMM_BOOTROM_HWRESET_DONE: return "HWRESET_DONE";
	case TAB3_XMM_BOOTROM_DETACH: return "DETACH";
	default: return "UNKNOWN";
	}
}

static void tab3_xmm_bootrom_trace_reset(void)
{
	memset(tab3_xmm_bootrom_trace, 0, sizeof(tab3_xmm_bootrom_trace));
	atomic_set(&tab3_xmm_bootrom_trace_next, 0);
	WRITE_ONCE(tab3_xmm_bootrom_trace_t0_ns, ktime_get_mono_fast_ns());
}

static void tab3_xmm_bootrom_trace_record(u16 event, s32 status, u32 len,
					   const void *data, u32 aux)
{
	struct tab3_xmm_bootrom_trace_record *rec;
	u32 head[2] = {};
	u32 seq;
	u32 idx;

	if (data && len)
		memcpy(head, data, min_t(u32, len, sizeof(head)));

	seq = atomic_inc_return(&tab3_xmm_bootrom_trace_next);
	idx = (seq - 1) % TAB3_XMM_BOOTROM_TRACE_MAX;
	rec = &tab3_xmm_bootrom_trace[idx];

	WRITE_ONCE(rec->valid, 0);
	rec->ns = ktime_get_mono_fast_ns();
	rec->seq = seq;
	rec->event = event;
	rec->cpu = raw_smp_processor_id();
	rec->status = status;
	rec->len = len;
	rec->head0 = head[0];
	rec->head1 = head[1];
	rec->aux = aux;
	/* Publish the complete record before exposing its sequence as valid. */
	smp_wmb();
	WRITE_ONCE(rec->valid, seq);
}

static int tab3_xmm_bootrom_trace_show(struct seq_file *s, void *unused)
{
	u64 t0 = READ_ONCE(tab3_xmm_bootrom_trace_t0_ns);
	u32 total = max_t(int, atomic_read(&tab3_xmm_bootrom_trace_next), 0);
	u32 count = min_t(u32, total, TAB3_XMM_BOOTROM_TRACE_MAX);
	u32 first = total - count + 1;
	u32 seq;

	seq_printf(s,
		   "# v179 058b:0041 bootrom circular recorder total=%u kept=%u max=%u\n",
		   total, count, TAB3_XMM_BOOTROM_TRACE_MAX);
	seq_puts(s, "# dt_us seq cpu event status len head0 head1 aux\n");
	seq_puts(s, "# aux: RX=endpoint, TX=remaining_tx_queue, ATTACH/DETACH=vidpid\n");

	for (seq = first; seq <= total; seq++) {
		const struct tab3_xmm_bootrom_trace_record *rec;
		u32 idx = (seq - 1) % TAB3_XMM_BOOTROM_TRACE_MAX;
		u64 dt_us;

		rec = &tab3_xmm_bootrom_trace[idx];
		if (READ_ONCE(rec->valid) != seq)
			continue;
		/* Pair with the writer's publish barrier before reading fields. */
		smp_rmb();
		dt_us = t0 && rec->ns >= t0 ?
			div_u64(rec->ns - t0, 1000) : 0;
		seq_printf(s, "%llu %u %u %s %d %u %08x %08x %08x\n",
			   (unsigned long long)dt_us, rec->seq, rec->cpu,
			   tab3_xmm_bootrom_trace_event_name(rec->event),
			   rec->status, rec->len, rec->head0, rec->head1,
			   rec->aux);
	}

	return 0;
}

static bool tab3_xmm_boot_is_hw_reset(size_t count, const u8 *data)
{
	static const u8 hw_reset_head[16] = {
		0x2e, 0x02, 0x08, 0x02, 0x04, 0x00, 0x00, 0x00,
		0x01, 0x10, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	return count == TAB3_XMM_BOOT_CHUNK &&
	       !memcmp(data, hw_reset_head, sizeof(hw_reset_head));
}

void tab3_xmm6262_handoff_quiesce_arm(void)
{
	WRITE_ONCE(tab3_xmm_handoff_quiesce_deadline,
		   jiffies + msecs_to_jiffies(TAB3_XMM_HANDOFF_QUIESCE_FAILSAFE_MS));
	atomic_set(&tab3_xmm_handoff_quiesce, 1);
	WRITE_ONCE(tab3_xmm_host_rearm_deadline,
		   jiffies + msecs_to_jiffies(10000));
	atomic_set(&tab3_xmm_host_rearm_pending, 1);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_quiesce_arm);

bool tab3_xmm6262_claim_host_rearm(void)
{
	if (!atomic_read(&tab3_xmm_host_rearm_pending))
		return false;
	if (time_after(jiffies, READ_ONCE(tab3_xmm_host_rearm_deadline))) {
		atomic_set(&tab3_xmm_host_rearm_pending, 0);
		return false;
	}
	return atomic_cmpxchg(&tab3_xmm_host_rearm_pending, 1, 0) == 1;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_claim_host_rearm);

bool tab3_xmm6262_handoff_quiesce_active(void)
{
	unsigned long deadline;

	if (!atomic_read(&tab3_xmm_handoff_quiesce))
		return false;

	deadline = READ_ONCE(tab3_xmm_handoff_quiesce_deadline);
	if (time_after(jiffies, deadline)) {
		atomic_set(&tab3_xmm_handoff_quiesce, 0);
		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_quiesce_active);

void tab3_xmm6262_handoff_quiesce_end(void)
{
	atomic_set(&tab3_xmm_handoff_quiesce, 0);
	WRITE_ONCE(tab3_xmm_handoff_quiesce_deadline, 0);
}
EXPORT_SYMBOL_GPL(tab3_xmm6262_handoff_quiesce_end);

static int tab3_xmm_boot_get_backend(struct tab3_xmm_boot_port *port,
				    struct usb_device **udev,
				    u8 *bulk_in, u8 *bulk_out);
static void tab3_xmm_boot_rx_complete(struct urb *urb);

static int tab3_xmm_boot_submit_rx(struct tab3_xmm_boot_port *port,
				    gfp_t gfp)
{
	struct usb_device *udev;
	u8 bulk_in, bulk_out;
	int ret;

	ret = tab3_xmm_boot_get_backend(port, &udev, &bulk_in, &bulk_out);
	if (ret)
		return ret;

	port->rx_urb->transfer_flags = 0;
	usb_fill_bulk_urb(port->rx_urb, udev,
			  usb_rcvbulkpipe(udev, bulk_in),
			  port->rx_buf, TAB3_XMM_BOOT_RX_MAX,
			  tab3_xmm_boot_rx_complete, port);
	usb_mark_last_busy(udev);
	ret = usb_submit_urb(port->rx_urb, gfp);
	tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_RX_SUBMIT,
				       ret, TAB3_XMM_BOOT_RX_MAX, NULL, bulk_in);
	usb_put_dev(udev);

	return ret;
}

static int tab3_xmm_boot_queue_rx(struct tab3_xmm_boot_port *port,
				   const u8 *data, unsigned int len)
{
	struct sk_buff *skb;
	unsigned int rest;
	unsigned int frag;
	const u8 *cur;

	if (!len)
		return 0;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb) {
		memcpy(skb_put(skb, len), data, len);
		skb_queue_tail(&port->rx_queue, skb);
		wake_up(&port->wait);
		return len;
	}

	rest = len;
	cur = data;
	while (rest) {
		frag = min_t(unsigned int, TAB3_XMM_BOOT_RX_FRAG, rest);
		skb = alloc_skb(frag, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;
		memcpy(skb_put(skb, frag), cur, frag);
		skb_queue_tail(&port->rx_queue, skb);
		cur += frag;
		rest -= frag;
	}

	wake_up(&port->wait);
	return len;
}

static void tab3_xmm_boot_rx_retry_work(struct work_struct *work)
{
	struct tab3_xmm_boot_port *port = container_of(to_delayed_work(work),
						       struct tab3_xmm_boot_port,
						       rx_retry_work);
	struct urb *urb = port->rx_urb;
	int ret;

	if (!READ_ONCE(port->connected))
		return;

	if (urb->dev)
		usb_mark_last_busy(urb->dev);

	ret = tab3_xmm_boot_queue_rx(port, urb->transfer_buffer,
				     urb->actual_length);
	if (ret == -ENOMEM) {
		if (port->rx_retry_cnt++ < 10)
			queue_delayed_work(port->tx_wq, &port->rx_retry_work,
				   msecs_to_jiffies(TAB3_XMM_BOOT_RX_RETRY_MS));
		return;
	}

	if (ret < 0 || !READ_ONCE(port->connected))
		return;

	port->rx_retry_cnt = 0;
	urb->transfer_flags = 0;
	usb_fill_bulk_urb(urb, urb->dev,
			  usb_rcvbulkpipe(urb->dev, READ_ONCE(port->bulk_in)),
			  port->rx_buf, TAB3_XMM_BOOT_RX_MAX,
			  tab3_xmm_boot_rx_complete, port);
	if (!READ_ONCE(port->connected))
		return;
	usb_mark_last_busy(urb->dev);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -ENODEV && ret != -ESHUTDOWN)
		pr_warn("USBBOOT_RX_RESUBMIT_FAIL ret=%d\n", ret);
}

static void tab3_xmm_boot_rx_complete(struct urb *urb)
{
	struct tab3_xmm_boot_port *port = urb->context;
	u8 trace_head[8] = {};
	u32 trace_len = urb->actual_length;
	s32 trace_status = urb->status;
	int ret;

	if (urb->dev)
		usb_mark_last_busy(urb->dev);
	if (trace_len)
		memcpy(trace_head, urb->transfer_buffer,
		       min_t(u32, trace_len, sizeof(trace_head)));

	if (urb->status != 0 && urb->status != -ENOENT) {
		pr_err("usb_rx_complete: urb err status = %d\n", urb->status);
		wake_up(&port->wait);
		tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_RX_DONE,
				       trace_status, trace_len, trace_head,
				       READ_ONCE(port->bulk_in));
		return;
	}

	if (urb->actual_length > 0) {
		ret = tab3_xmm_boot_queue_rx(port, urb->transfer_buffer,
					     urb->actual_length);
		if (ret == -ENOMEM) {
			queue_delayed_work(port->tx_wq, &port->rx_retry_work, 0);
			tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_RX_DONE,
					       trace_status, trace_len, trace_head,
					       READ_ONCE(port->bulk_in));
			return;
		}
		port->rx_retry_cnt = 0;
	}

	if (urb->status != 0 || !READ_ONCE(port->connected)) {
		tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_RX_DONE,
				       trace_status, trace_len, trace_head,
				       READ_ONCE(port->bulk_in));
		return;
	}

	urb->transfer_flags = 0;
	usb_fill_bulk_urb(urb, urb->dev,
			  usb_rcvbulkpipe(urb->dev, READ_ONCE(port->bulk_in)),
			  port->rx_buf, TAB3_XMM_BOOT_RX_MAX,
			  tab3_xmm_boot_rx_complete, port);
	if (!READ_ONCE(port->connected))
		return;
	usb_mark_last_busy(urb->dev);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -ENODEV && ret != -ESHUTDOWN)
		pr_warn("USBBOOT_RX_RESUBMIT_FAIL ret=%d\n", ret);
	tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_RX_DONE,
				       trace_status, trace_len, trace_head,
				       READ_ONCE(port->bulk_in));
}

static int tab3_xmm_boot_get_backend(struct tab3_xmm_boot_port *port,
				    struct usb_device **udev,
				    u8 *bulk_in, u8 *bulk_out)
{
	int ret = 0;

	mutex_lock(&port->lock);

	if (!port->connected || !port->udev) {
		ret = -ENODEV;
		goto out;
	}

	*udev = usb_get_dev(port->udev);
	*bulk_in = port->bulk_in;
	*bulk_out = port->bulk_out;

out:
	mutex_unlock(&port->lock);
	return ret;
}

static void tab3_xmm_boot_tx_complete(struct urb *urb)
{
	struct tab3_xmm_tx_ctx *ctx = urb->context;

	if (ctx->hw_reset) {
		if (!urb->status && urb->actual_length == ctx->skb->len) {
			tab3_xmm6262_handoff_trace_arm(
				atomic_inc_return(&tab3_xmm_hwreset_trace_seq));
			tab3_xmm6262_handoff_quiesce_arm();
			tab3_xmm6262_trace_hw_snapshot(TAB3_XMM_TRANSIENT_HWRESET_DONE);
		} else {
			tab3_xmm6262_handoff_quiesce_end();
		}
	}

	/* Exact Samsung usb_tx_complete(): keep runtime-PM activity fresh. */
	if (urb->dev && READ_ONCE(tab3_xmm_boot_port.connected))
		usb_mark_last_busy(urb->dev);

	tab3_xmm_bootrom_trace_record(
		ctx->hw_reset ? TAB3_XMM_BOOTROM_HWRESET_DONE :
				TAB3_XMM_BOOTROM_TX_DONE,
		urb->status, urb->actual_length, ctx->skb->data,
		skb_queue_len(&tab3_xmm_boot_port.tx_queue));

	usb_put_dev(ctx->udev);
	kfree_skb(ctx->skb);
	kfree(ctx);
	usb_free_urb(urb);
}

static int tab3_xmm_boot_submit_skb(struct tab3_xmm_boot_port *port,
				    struct sk_buff *skb)
{
	struct tab3_xmm_tx_ctx *ctx;
	struct usb_device *udev;
	struct urb *urb;
	u8 bulk_in, bulk_out;
	int pm_ret;
	int ret;

	ret = tab3_xmm_boot_get_backend(port, &udev, &bulk_in, &bulk_out);
	if (ret)
		return ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		usb_put_dev(udev);
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		kfree(ctx);
		usb_put_dev(udev);
		return -ENOMEM;
	}

	ctx->udev = udev;
	ctx->skb = skb;
	ctx->hw_reset = tab3_xmm_boot_is_hw_reset(skb->len, skb->data);

	usb_fill_bulk_urb(urb, udev, usb_sndbulkpipe(udev, bulk_out),
			  skb->data, skb->len, tab3_xmm_boot_tx_complete, ctx);
	urb->transfer_flags |= URB_ZERO_PACKET;

	/* Samsung usb_tx_work takes a runtime-PM reference around each TX. */
	pm_ret = pm_runtime_get_sync(&udev->dev);
	if (pm_ret < 0) {
		pm_runtime_put_noidle(&udev->dev);
		ret = pm_ret;
		goto err;
	}

	usb_mark_last_busy(udev);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	pm_runtime_put(&udev->dev);
	if (ret) {
		pr_err("usb_submit_urb with ret(%d)\n", ret);
		goto err;
	}

	if (ctx->hw_reset)
		tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_HWRESET_SUBMIT,
					       0, skb->len, skb->data,
					       skb_queue_len(&port->tx_queue));

	return 0;

err:
	usb_free_urb(urb);
	usb_put_dev(ctx->udev);
	kfree(ctx);
	return ret;
}

static void tab3_xmm_boot_tx_work(struct work_struct *work)
{
	struct tab3_xmm_boot_port *port = container_of(to_delayed_work(work),
						       struct tab3_xmm_boot_port,
						       tx_work);
	struct sk_buff *skb;
	int ret;

	while ((skb = skb_dequeue(&port->tx_queue))) {
		ret = tab3_xmm_boot_submit_skb(port, skb);
		if (!ret)
			continue;

		skb_queue_head(&port->tx_queue, skb);
		if (ret == -ENODEV || ret == -ENOENT || ret == -ESHUTDOWN)
			return;

		queue_delayed_work(port->tx_wq, &port->tx_work,
				   msecs_to_jiffies(20));
		return;
	}

	/* Stock tx_async_wake is released only after both TX queues drain. */
	__pm_relax(port->tx_wake);
}

static int tab3_xmm_boot_open(struct inode *inode, struct file *file)
{
	file->private_data = &tab3_xmm_boot_port;
	return nonseekable_open(inode, file);
}

static int tab3_xmm_boot_release(struct inode *inode, struct file *file)
{
	struct tab3_xmm_boot_port *port = file->private_data;

	skb_queue_purge(&port->rx_queue);
	wake_up(&port->wait);
	file->private_data = NULL;
	return 0;
}

static ssize_t tab3_xmm_boot_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct tab3_xmm_boot_port *port = file->private_data;
	struct sk_buff *skb;
	size_t rest;
	size_t copy_len;
	char __user *cur = buf;

	if (!count)
		return 0;

	skb = skb_dequeue(&port->rx_queue);
	if (!skb)
		return 0;

	rest = count;
	while (rest) {
		if (skb->len > rest) {
			if (copy_to_user(cur, skb->data, rest)) {
				kfree_skb(skb);
				return -EFAULT;
			}
			skb_pull(skb, rest);
			if (skb->len)
				skb_queue_head(&port->rx_queue, skb);
			return rest;
		}

		copy_len = min_t(size_t, rest, skb->len);
		if (copy_to_user(cur, skb->data, copy_len)) {
			kfree_skb(skb);
			return -EFAULT;
		}
		cur += copy_len;
		rest -= copy_len;
		kfree_skb(skb);

		if (!rest)
			break;

		skb = skb_dequeue(&port->rx_queue);
		if (!skb)
			return count - rest;
	}

	return count;
}

static int tab3_xmm_boot_enqueue_user(struct tab3_xmm_boot_port *port,
				      const char __user *buf, size_t len,
				      size_t alloc_len)
{
	struct sk_buff *skb;

	skb = alloc_skb(alloc_len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	if (copy_from_user(skb_put(skb, len), buf, len)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	skb_queue_tail(&port->tx_queue, skb);
	__pm_stay_awake(port->tx_wake);
	queue_delayed_work(port->tx_wq, &port->tx_work, 0);
	return 0;
}

static ssize_t tab3_xmm_boot_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct tab3_xmm_boot_port *port = file->private_data;
	size_t done;
	int ret;

	if (!count)
		return 0;

	ret = tab3_xmm_boot_enqueue_user(port, buf, count,
					 count + TAB3_XMM_BOOT_ALLOC_OVERHEAD);
	if (!ret)
		return count;

	if (ret != -ENOMEM ||
	    count + TAB3_XMM_BOOT_ALLOC_OVERHEAD <= TAB3_XMM_BOOT_CHUNK)
		return ret;

	done = 0;
	while (done < count) {
		size_t len = min_t(size_t, count - done, TAB3_XMM_BOOT_CHUNK);
		ret = tab3_xmm_boot_enqueue_user(port, buf + done, len, len);
		if (ret)
			return ret;
		done += len;
	}

	return count;
}

static __poll_t tab3_xmm_boot_poll(struct file *file, poll_table *wait)
{
	struct tab3_xmm_boot_port *port = file->private_data;
	int state;

	poll_wait(file, &port->wait, wait);
	tab3_xmm6262_poll_state(file, wait);
	state = tab3_xmm6262_get_state();

	if (!skb_queue_empty(&port->rx_queue) && state != TAB3_XMM_OFFLINE)
		return EPOLLIN | EPOLLRDNORM;

	if (state == TAB3_XMM_CRASH_RESET || state == TAB3_XMM_CRASH_EXIT ||
	    state == TAB3_XMM_NV_REBUILDING)
		return EPOLLHUP;

	return 0;
}

static long tab3_xmm_boot_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	return tab3_xmm6262_modem_compat_ioctl(cmd);
}

static const struct file_operations tab3_xmm_boot_fops = {
	.owner = THIS_MODULE,
	.open = tab3_xmm_boot_open,
	.release = tab3_xmm_boot_release,
	.read = tab3_xmm_boot_read,
	.write = tab3_xmm_boot_write,
	.poll = tab3_xmm_boot_poll,
	.unlocked_ioctl = tab3_xmm_boot_ioctl,
};

static struct usb_interface *
tab3_xmm_boot_find_data_interface(struct usb_interface *ctrl)
{
	struct usb_device *udev = interface_to_usbdev(ctrl);
	const struct usb_host_interface *alts = ctrl->cur_altsetting;
	const unsigned char *buf;
	int len;

	buf = alts->extra;
	len = alts->extralen;

	if (!len && alts->desc.bNumEndpoints > 0) {
		buf = alts->endpoint[0].extra;
		len = alts->endpoint[0].extralen;
	}

	while (buf && len >= 3) {
		if (!buf[0] || buf[0] > len)
			break;

		if (buf[1] == USB_DT_CS_INTERFACE &&
		    buf[2] == USB_CDC_UNION_TYPE &&
		    buf[0] >= sizeof(struct usb_cdc_union_desc)) {
			const struct usb_cdc_union_desc *u =
				(const struct usb_cdc_union_desc *)buf;

			return usb_ifnum_to_if(udev, u->bSlaveInterface0);
		}

		len -= buf[0];
		buf += buf[0];
	}

	/* Exact T311 boot identity is CDC control interface 0 + data 1. */
	return usb_ifnum_to_if(udev, 1);
}

static int tab3_xmm_boot_find_bulk_eps(struct usb_interface *data_intf,
				       u8 *bulk_in, u8 *bulk_out)
{
	const struct usb_host_interface *alts = data_intf->cur_altsetting;
	int i;

	*bulk_in = 0;
	*bulk_out = 0;

	for (i = 0; i < alts->desc.bNumEndpoints; i++) {
		const struct usb_endpoint_descriptor *ep =
			&alts->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(ep) && !*bulk_in)
			*bulk_in = ep->bEndpointAddress;
		else if (usb_endpoint_is_bulk_out(ep) && !*bulk_out)
			*bulk_out = ep->bEndpointAddress;
	}

	return (*bulk_in && *bulk_out) ? 0 : -EINVAL;
}

static int tab3_xmm_boot_probe(struct usb_interface *intf,
			       const struct usb_device_id *id)
{
	struct tab3_xmm_boot_port *port = &tab3_xmm_boot_port;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_interface *data_intf;
	u8 bulk_in, bulk_out;
	int ret;

	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_COMM ||
	    intf->cur_altsetting->desc.bInterfaceSubClass !=
			USB_CDC_SUBCLASS_ACM)
		return -ENODEV;

	data_intf = tab3_xmm_boot_find_data_interface(intf);
	if (!data_intf)
		return -ENODEV;

	ret = tab3_xmm_boot_find_bulk_eps(data_intf,
					  &bulk_in, &bulk_out);
	if (ret)
		return ret;

	/* Stock BOOT_DOWN probe clears stale boot TX before binding a fresh pipe. */
	cancel_delayed_work_sync(&port->tx_work);
	skb_queue_purge(&port->tx_queue);

	/*
	 * Bind both control and data interfaces to this driver.  The persistent
	 * misc object is independent and is NOT registered/deregistered here.
	 */
	ret = usb_driver_claim_interface(&tab3_xmm_boot_driver,
					 data_intf, port);
	if (ret)
		return ret;

	mutex_lock(&port->lock);

	if (port->udev) {
		usb_put_dev(port->udev);
		port->udev = NULL;
	}

	port->udev = usb_get_dev(udev);
	port->ctrl_intf = intf;
	port->data_intf = data_intf;
	port->bulk_in = bulk_in;
	port->bulk_out = bulk_out;
	port->connected = true;

	mutex_unlock(&port->lock);

	usb_set_intfdata(intf, port);
	usb_set_intfdata(data_intf, port);

	pm_runtime_forbid(&udev->dev);
	pm_suspend_ignore_children(&udev->dev, true);

	/* A fresh 058b backend starts a fresh circular bootrom trace. */
	tab3_xmm_bootrom_trace_reset();

	ret = tab3_xmm_boot_submit_rx(port, GFP_KERNEL);
	if (ret) {
		mutex_lock(&port->lock);
		port->connected = false;
		port->ctrl_intf = NULL;
		port->data_intf = NULL;
		port->bulk_in = 0;
		port->bulk_out = 0;
		if (port->udev) {
			usb_put_dev(port->udev);
			port->udev = NULL;
		}
		mutex_unlock(&port->lock);
		usb_set_intfdata(intf, NULL);
		usb_set_intfdata(data_intf, NULL);
		return ret;
	}

	tab3_xmm6262_usb_connected_set(true);
	wake_up(&port->wait);
	tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_ATTACH, 0,
				       udev->devnum, NULL,
				       (le16_to_cpu(udev->descriptor.idVendor) << 16) |
				       le16_to_cpu(udev->descriptor.idProduct));

	tab3_xmm6262_handoff_trace_event(TAB3_XMM_HANDOFF_USBBOOT_ATTACH,
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		udev->devnum, 0);
	dev_dbg(&intf->dev,
		 "USBBOOT_ATTACH=PASS vid=%04x pid=%04x ctrl_if=%u data_if=%u node=/dev/umts_boot0 persistent=1 in=0x%02x out=0x%02x\n",
		 le16_to_cpu(udev->descriptor.idVendor),
		 le16_to_cpu(udev->descriptor.idProduct),
		 intf->cur_altsetting->desc.bInterfaceNumber,
		 data_intf->cur_altsetting->desc.bInterfaceNumber,
		 bulk_in, bulk_out);

	return 0;
}

static void tab3_xmm_boot_disconnect(struct usb_interface *intf)
{
	struct tab3_xmm_boot_port *port = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_device *old = NULL;
	bool detached = false;

	if (!port)
		port = &tab3_xmm_boot_port;

	mutex_lock(&port->lock);

	if (port->udev == udev) {
		tab3_xmm6262_usb_connected_set(false);
		old = port->udev;
		port->udev = NULL;
		port->ctrl_intf = NULL;
		port->data_intf = NULL;
		port->bulk_in = 0;
		port->bulk_out = 0;
		port->connected = false;
		detached = true;
	}

	mutex_unlock(&port->lock);

	usb_set_intfdata(intf, NULL);
	usb_kill_urb(port->rx_urb);
	cancel_delayed_work_sync(&port->rx_retry_work);
	cancel_delayed_work_sync(&port->tx_work);
	wake_up(&port->wait);
	tab3_xmm_bootrom_trace_record(TAB3_XMM_BOOTROM_DETACH, 0,
				       udev->devnum, NULL,
				       (le16_to_cpu(udev->descriptor.idVendor) << 16) |
				       le16_to_cpu(udev->descriptor.idProduct));

	if (detached) {
		struct device *hdev = NULL;

		/*
		 * Stock modem_link_device_hsic disconnect contract: forbid the
		 * EHCI parent from runtime-suspending, then hold boot_wake until
		 * the runtime command interface is established.
		 */
		if (udev->bus && udev->bus->root_hub &&
		    udev->bus->root_hub->dev.parent)
			hdev = udev->bus->root_hub->dev.parent;
		if (hdev)
			pm_runtime_forbid(hdev);
		tab3_xmm6262_boot_wake_hold();
		dev_dbg(&intf->dev,
			 "SAMSUNG_BSP_USBBOOT_DETACH ehci_runtime=FORBID boot_wake=HELD persistent_node=1\n");
	}

	if (old)
		usb_put_dev(old);

	if (detached) {
		tab3_xmm6262_handoff_trace_event(
			TAB3_XMM_HANDOFF_USBBOOT_DETACH,
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct),
			udev->devnum, 0);
		dev_dbg(&intf->dev,
			 "USBBOOT_DETACH persistent_node=1 backend_connected=0\n");
	}
}

static const struct usb_device_id tab3_xmm_boot_ids[] = {
	{
		USB_DEVICE_AND_INTERFACE_INFO(TAB3_XMM_BOOT_VID,
					      TAB3_XMM_BOOT_PID,
					      USB_CLASS_COMM,
					      USB_CDC_SUBCLASS_ACM,
					      USB_CDC_PROTO_NONE)
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, tab3_xmm_boot_ids);

static struct usb_driver tab3_xmm_boot_driver = {
	.name = "tab3_xmm6262_usbboot",
	.probe = tab3_xmm_boot_probe,
	.disconnect = tab3_xmm_boot_disconnect,
	.id_table = tab3_xmm_boot_ids,
	.supports_autosuspend = 1,
};

static int __init tab3_xmm_boot_init(void)
{
	struct tab3_xmm_boot_port *port = &tab3_xmm_boot_port;
	int ret;

	mutex_init(&port->lock);
	init_waitqueue_head(&port->wait);
	skb_queue_head_init(&port->rx_queue);
	skb_queue_head_init(&port->tx_queue);
	INIT_DELAYED_WORK(&port->rx_retry_work, tab3_xmm_boot_rx_retry_work);
	INIT_DELAYED_WORK(&port->tx_work, tab3_xmm_boot_tx_work);
	port->tx_wq = create_singlethread_workqueue("usb_tx_wq");
	if (!port->tx_wq)
		return -ENOMEM;

	port->tx_wake = wakeup_source_register(NULL, "tx_hsic");
	if (!port->tx_wake) {
		destroy_workqueue(port->tx_wq);
		port->tx_wq = NULL;
		return -ENOMEM;
	}

	port->rx_buf = kmalloc(TAB3_XMM_BOOT_RX_MAX, GFP_DMA | GFP_KERNEL);
	if (!port->rx_buf) {
		wakeup_source_unregister(port->tx_wake);
		port->tx_wake = NULL;
		destroy_workqueue(port->tx_wq);
		port->tx_wq = NULL;
		return -ENOMEM;
	}

	port->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!port->rx_urb) {
		kfree(port->rx_buf);
		port->rx_buf = NULL;
		wakeup_source_unregister(port->tx_wake);
		port->tx_wake = NULL;
		destroy_workqueue(port->tx_wq);
		port->tx_wq = NULL;
		return -ENOMEM;
	}

	port->miscdev.minor = MISC_DYNAMIC_MINOR;
	port->miscdev.name = "umts_boot0";
	port->miscdev.fops = &tab3_xmm_boot_fops;

	ret = misc_register(&port->miscdev);
	if (ret)
		goto err_resources;

	ret = usb_register(&tab3_xmm_boot_driver);
	if (ret) {
		misc_deregister(&port->miscdev);
		goto err_resources;
	}

	tab3_xmm_bootrom_trace_proc =
		proc_create_single("tab3_xmm6262_bootrom_trace", 0444, NULL,
				   tab3_xmm_bootrom_trace_show);
	if (!tab3_xmm_bootrom_trace_proc)
		pr_warn("XMM6262 v179 bootrom recorder proc entry unavailable\n");

	pr_info("XMM6262 boot transport ready on /dev/umts_boot0\n");
	pr_info("XMM6262 v179 bootrom RAM recorder ready\n");
	pr_debug("USBBOOT_TX_POLICY=READY queue=sk_fmt_tx_q_model worker=singlethread runtime_pm=per_skb retry_ms=20 fallback=0x4008_on_enomem zero_packet=1\n");

	return 0;

err_resources:
	usb_free_urb(port->rx_urb);
	port->rx_urb = NULL;
	kfree(port->rx_buf);
	port->rx_buf = NULL;
	if (port->tx_wq) {
		cancel_delayed_work_sync(&port->rx_retry_work);
		cancel_delayed_work_sync(&port->tx_work);
		skb_queue_purge(&port->rx_queue);
		skb_queue_purge(&port->tx_queue);
		destroy_workqueue(port->tx_wq);
		port->tx_wq = NULL;
	}
	if (port->tx_wake) {
		__pm_relax(port->tx_wake);
		wakeup_source_unregister(port->tx_wake);
		port->tx_wake = NULL;
	}
	return ret;
}

static void __exit tab3_xmm_boot_exit(void)
{
	struct tab3_xmm_boot_port *port = &tab3_xmm_boot_port;
	struct usb_device *old = NULL;

	proc_remove(tab3_xmm_bootrom_trace_proc);
	tab3_xmm_bootrom_trace_proc = NULL;
	usb_deregister(&tab3_xmm_boot_driver);

	mutex_lock(&port->lock);
	old = port->udev;
	port->udev = NULL;
	port->connected = false;
	mutex_unlock(&port->lock);

	if (old)
		usb_put_dev(old);

	misc_deregister(&port->miscdev);
	if (port->tx_wq) {
		cancel_delayed_work_sync(&port->rx_retry_work);
		cancel_delayed_work_sync(&port->tx_work);
		skb_queue_purge(&port->rx_queue);
		skb_queue_purge(&port->tx_queue);
		destroy_workqueue(port->tx_wq);
		port->tx_wq = NULL;
	}
	if (port->tx_wake) {
		__pm_relax(port->tx_wake);
		wakeup_source_unregister(port->tx_wake);
		port->tx_wake = NULL;
	}
	usb_free_urb(port->rx_urb);
	port->rx_urb = NULL;
	kfree(port->rx_buf);
	port->rx_buf = NULL;
}

module_init(tab3_xmm_boot_init);
module_exit(tab3_xmm_boot_exit);

MODULE_DESCRIPTION("SM-T311 XMM6262 persistent stock-semantics USB boot transport");
MODULE_LICENSE("GPL");
