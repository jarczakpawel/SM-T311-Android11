// SPDX-License-Identifier: GPL-2.0

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/if_arp.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/poll.h>
#include <linux/samsung-tab3-xmm6262.h>
#include <linux/skbuff.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define XMM_RUNTIME_VID 0x1519
#define XMM_RUNTIME_PID 0x0020
#define XMM_PIPE_COUNT 4
#define XMM_RX_SIZE (16 * 1024)
#define XMM_RX_CHUNK (4096 - 512)
#define XMM_FMT_MAX 0x4000
#define XMM_HDLC_START 0x7f
#define XMM_HDLC_END 0x7e
#define XMM_CMD_SUSPEND 0x00ca
#define XMM_CMD_RESUME 0x00cb
#define XMM_RECONNECT_TRIES 5
#define XMM_RX_RETRY_MAX 10
#define XMM_RX_RETRY_MS 50
#define XMM_TX_RETRY_MS 20
/* Keep USB enumerated while the first Samsung IPC/RIL session starts.
 * Stock HSIC also forbids runtime PM until its control channel comes online.
 * G372 12s grace ended before Android issued PDP, which in physical
 * traces came about 56 seconds after vendor RIL_Init. Limit wake to
 * 75 seconds at boot ONLY and restore normal 200ms autosuspend after.
 * This bootstrap has one bounded grace period only; no permanent wakelock.
 */
#define XMM_PM_BOOT_MAX_WAIT_MS 45000
#define XMM_PM_BOOT_POLL_MS 250
#define XMM_PM_BOOT_GRACE_MS 75000

#define IOCTL_MODEM_PROTOCOL_SUSPEND _IO('o', 0x25)
#define IOCTL_MODEM_PROTOCOL_RESUME _IO('o', 0x26)

#ifndef ETH_P_IPV6
#define ETH_P_IPV6 0x86dd
#endif

enum xmm_pipe_id {
	XMM_PIPE_FMT = 0,
	XMM_PIPE_RAW,
	XMM_PIPE_RFS,
	XMM_PIPE_CMD,
};

enum xmm_io_format {
	XMM_IO_FMT,
	XMM_IO_RAW,
	XMM_IO_RFS,
	XMM_IO_RAMDUMP,
	XMM_IO_MULTI_RAW,
};

enum xmm_io_type {
	XMM_IO_MISC,
	XMM_IO_NET,
	XMM_IO_DUMMY,
};

enum xmm_io_index {
	XMM_IOD_FMT,
	XMM_IOD_RFS,
	XMM_IOD_MULTIPDP,
	XMM_IOD_RMNET0,
	XMM_IOD_RMNET1,
	XMM_IOD_RMNET2,
	XMM_IOD_ROUTER,
	XMM_IOD_CSD,
	XMM_IOD_RAMDUMP,
	XMM_IOD_LOOPBACK,
	XMM_IOD_COUNT,
};

enum xmm_parse_phase {
	XMM_PARSE_START,
	XMM_PARSE_HEADER,
	XMM_PARSE_ALLOC,
	XMM_PARSE_DATA,
	XMM_PARSE_END,
	XMM_PARSE_DISPATCH,
};

struct xmm_runtime;

struct xmm_parser {
	enum xmm_parse_phase phase;
	u8 header[6];
	u8 header_len;
	u8 header_pos;
	u8 control;
	u8 channel;
	u32 data_len;
	u32 data_pos;
	struct sk_buff *frame;
};

struct xmm_pipe {
	struct xmm_runtime *rt;
	struct usb_interface *ctrl_intf;
	struct usb_interface *data_intf;
	struct urb *rx_urb;
	u8 *rx_buf;
	unsigned int rx_pipe;
	unsigned int tx_pipe;
	struct sk_buff_head rx_queue;
	struct delayed_work rx_work;
	struct delayed_work rx_retry_work;
	struct delayed_work rx_submit_work;
	struct xmm_parser parser;
	unsigned int rx_retry_count;
	u8 cmd_byte;
	bool cmd_half;
	bool present;
	bool suspended;
	int id;
};

struct xmm_io {
	struct xmm_runtime *rt;
	const char *name;
	u8 id;
	enum xmm_io_format format;
	enum xmm_io_type type;
	struct miscdevice miscdev;
	struct net_device *ndev;
	struct sk_buff_head rx_queue;
	wait_queue_head_t wait;
	atomic_t opened;
	unsigned int waketime_ms;
};

struct xmm_net_priv {
	struct xmm_io *io;
};

struct xmm_tx_ctx {
	struct xmm_runtime *rt;
	struct usb_device *udev;
	struct sk_buff *skb;
};

struct xmm_runtime {
	struct mutex lock;
	spinlock_t tx_gate_lock;
	struct usb_device *udev;
	struct xmm_pipe pipe[XMM_PIPE_COUNT];
	struct xmm_io io[XMM_IOD_COUNT];
	struct sk_buff_head fmt_tx_queue;
	struct sk_buff_head raw_tx_queue;
	struct delayed_work tx_work;
	struct delayed_work pm_start_work;
	struct delayed_work reconnect_work;
	struct usb_anchor tx_anchor;
	wait_queue_head_t raw_wait;
	struct sk_buff *fmt_multi[128];
	/* Privacy-safe FMT transaction/transport counters: no PDU or IDs. */
	atomic64_t fmt_rx_urbs;
	atomic64_t fmt_rx_bytes;
	atomic64_t fmt_rx_urb_errors;
	atomic64_t fmt_rx_oom_drops;
	atomic64_t fmt_rx_submit_retries;
	atomic64_t fmt_rx_submit_errors;
	atomic64_t fmt_rx_frames;
	atomic64_t fmt_parse_errors;
	atomic64_t fmt_tx_ok;
	atomic64_t fmt_tx_errors;
	atomic64_t fmt_pm_suspend;
	atomic64_t fmt_pm_resume;
	/* Metadata only: modem wakeup callback and USB runtime PM outcomes. */
	atomic64_t hostwake_active;
	atomic64_t hostwake_idle;
	atomic64_t hostwake_pm_ok;
	atomic64_t hostwake_pm_err;
	atomic64_t pm_boot_fmt_waits;
	atomic64_t pm_boot_grace_runs;
	atomic64_t pm_boot_deadline_expired;
	unsigned long pm_boot_deadline;
	bool pm_boot_grace_started;
	unsigned long pipe_mask;
	__be32 loopback_ip;
	bool raw_tx_suspended;
	bool runtime_connected;
	bool pm_started;
	bool hostwake_registered;
	unsigned int reconnect_left;
};

struct xmm_tx_cb {
	u8 pipe;
};

#define XMM_TX_CB(skb) ((struct xmm_tx_cb *)((skb)->cb))

static struct xmm_runtime *xmm_rt;

static const struct {
	const char *name;
	u8 id;
	enum xmm_io_format format;
	enum xmm_io_type type;
} xmm_io_desc[XMM_IOD_COUNT] = {
	[XMM_IOD_FMT] = { "umts_ipc0", 0x01, XMM_IO_FMT, XMM_IO_MISC },
	[XMM_IOD_RFS] = { "umts_rfs0", 0x41, XMM_IO_RFS, XMM_IO_MISC },
	[XMM_IOD_MULTIPDP] = { "multipdp", 0x01, XMM_IO_MULTI_RAW, XMM_IO_DUMMY },
	[XMM_IOD_RMNET0] = { "rmnet0", 0x2a, XMM_IO_RAW, XMM_IO_NET },
	[XMM_IOD_RMNET1] = { "rmnet1", 0x2b, XMM_IO_RAW, XMM_IO_NET },
	[XMM_IOD_RMNET2] = { "rmnet2", 0x2c, XMM_IO_RAW, XMM_IO_NET },
	[XMM_IOD_ROUTER] = { "umts_router", 0x39, XMM_IO_RAW, XMM_IO_MISC },
	[XMM_IOD_CSD] = { "umts_csd", 0x21, XMM_IO_RAW, XMM_IO_MISC },
	[XMM_IOD_RAMDUMP] = { "umts_ramdump0", 0x00, XMM_IO_RAMDUMP, XMM_IO_MISC },
	[XMM_IOD_LOOPBACK] = { "umts_loopback0", 0x3f, XMM_IO_RAW, XMM_IO_MISC },
};

static struct xmm_io *xmm_find_raw_io(struct xmm_runtime *rt, u8 id)
{
	int i;

	for (i = 0; i < XMM_IOD_COUNT; i++)
		if (rt->io[i].format == XMM_IO_RAW && rt->io[i].id == id)
			return &rt->io[i];

	return NULL;
}

static void xmm_wake_io(struct xmm_io *io)
{
	if (io->miscdev.this_device && io->waketime_ms)
		pm_wakeup_event(io->miscdev.this_device, io->waketime_ms);
}

static void xmm_stop_net_queues(struct xmm_runtime *rt)
{
	int i;

	for (i = 0; i < XMM_IOD_COUNT; i++)
		if (rt->io[i].type == XMM_IO_NET && rt->io[i].ndev)
			netif_stop_queue(rt->io[i].ndev);
}

static void xmm_wake_net_queues(struct xmm_runtime *rt)
{
	int i;

	for (i = 0; i < XMM_IOD_COUNT; i++)
		if (rt->io[i].type == XMM_IO_NET && rt->io[i].ndev &&
		    netif_running(rt->io[i].ndev))
			netif_wake_queue(rt->io[i].ndev);
}

static void xmm_set_raw_suspended(struct xmm_runtime *rt, bool suspended)
{
	WRITE_ONCE(rt->raw_tx_suspended, suspended);
	if (suspended)
		xmm_stop_net_queues(rt);
	else
		xmm_wake_net_queues(rt);
	wake_up_interruptible(&rt->raw_wait);
}

static void xmm_queue_misc(struct xmm_io *io, struct sk_buff *skb)
{
	skb_queue_tail(&io->rx_queue, skb);
	xmm_wake_io(io);
	wake_up_interruptible(&io->wait);
}

static void xmm_receive_net(struct xmm_io *io, struct sk_buff *skb)
{
	u8 version;

	if (!io->ndev || !netif_running(io->ndev)) {
		kfree_skb(skb);
		return;
	}

	if (!skb->len) {
		kfree_skb(skb);
		return;
	}

	version = skb->data[0] >> 4;
	if (version == 4)
		skb->protocol = htons(ETH_P_IP);
	else if (version == 6)
		skb->protocol = htons(ETH_P_IPV6);
	else {
		kfree_skb(skb);
		return;
	}

	skb->dev = io->ndev;
	skb_reset_network_header(skb);
	io->ndev->stats.rx_packets++;
	io->ndev->stats.rx_bytes += skb->len;
	netif_rx(skb);
}

static int xmm_dispatch_fmt(struct xmm_runtime *rt, struct xmm_parser *p)
{
	struct sk_buff *skb = p->frame;
	struct sk_buff *multi;
	u8 id = p->control & 0x7f;
	bool more = p->control & 0x80;

	if (!skb)
		skb = alloc_skb(0, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	p->frame = NULL;
	multi = rt->fmt_multi[id];
	if (more || multi) {
		if (!multi) {
			multi = alloc_skb(XMM_FMT_MAX, GFP_KERNEL);
			if (!multi) {
				p->frame = skb;
				return -ENOMEM;
			}
			rt->fmt_multi[id] = multi;
		}
		if (multi->len + skb->len > XMM_FMT_MAX) {
			kfree_skb(skb);
			kfree_skb(multi);
			rt->fmt_multi[id] = NULL;
			return -EMSGSIZE;
		}
		skb_put_data(multi, skb->data, skb->len);
		kfree_skb(skb);
		if (more)
			return 0;
		rt->fmt_multi[id] = NULL;
		skb = multi;
	}

	atomic64_inc(&rt->fmt_rx_frames);
	xmm_queue_misc(&rt->io[XMM_IOD_FMT], skb);
	return 0;
}

static int xmm_dispatch_raw(struct xmm_runtime *rt, struct xmm_parser *p)
{
	struct sk_buff *skb = p->frame;
	struct xmm_io *io;
	u8 id = 0x20 | (p->channel & 0x1f);

	if (!skb)
		skb = alloc_skb(0, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	p->frame = NULL;
	xmm_wake_io(&rt->io[XMM_IOD_MULTIPDP]);

	if ((p->channel & 0x1f) == 0x1f && rt->loopback_ip)
		io = &rt->io[XMM_IOD_RMNET0];
	else
		io = xmm_find_raw_io(rt, id);

	if (!io) {
		kfree_skb(skb);
		return 0;
	}

	if (io->type == XMM_IO_NET)
		xmm_receive_net(io, skb);
	else
		xmm_queue_misc(io, skb);

	return 0;
}

static void xmm_parser_reset(struct xmm_parser *p, u8 header_len)
{
	kfree_skb(p->frame);
	memset(p, 0, sizeof(*p));
	p->phase = XMM_PARSE_START;
	p->header_len = header_len;
}

static int xmm_rfs_header(struct xmm_runtime *rt, struct xmm_parser *p)
{
	struct sk_buff *skb;

	skb = alloc_skb(sizeof(p->header), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_put_data(skb, p->header, sizeof(p->header));
	xmm_queue_misc(&rt->io[XMM_IOD_RFS], skb);
	return 0;
}

static int xmm_parser_header_done(struct xmm_pipe *pipe)
{
	struct xmm_parser *p = &pipe->parser;
	u32 frame_len;

	switch (pipe->id) {
	case XMM_PIPE_FMT:
		frame_len = get_unaligned_le16(p->header);
		if (frame_len < 3 || frame_len > XMM_FMT_MAX + 3)
			return -EBADMSG;
		p->control = p->header[2];
		p->data_len = frame_len - 3;
		p->phase = p->data_len ? XMM_PARSE_ALLOC : XMM_PARSE_END;
		break;
	case XMM_PIPE_RAW:
		frame_len = get_unaligned_le32(p->header);
		if (frame_len < 6 || frame_len > 1024 * 1024)
			return -EBADMSG;
		p->channel = p->header[4];
		p->control = p->header[5];
		p->data_len = frame_len - 6;
		p->phase = p->data_len ? XMM_PARSE_ALLOC : XMM_PARSE_END;
		break;
	case XMM_PIPE_RFS:
		frame_len = get_unaligned_le32(p->header);
		if (frame_len < 6 || frame_len > 16 * 1024 * 1024)
			return -EBADMSG;
		p->data_len = frame_len - 6;
		p->phase = XMM_PARSE_ALLOC;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int xmm_parse_stream(struct xmm_pipe *pipe, struct sk_buff *skb)
{
	struct xmm_runtime *rt = pipe->rt;
	struct xmm_parser *p = &pipe->parser;
	u32 left;
	unsigned int n;
	u8 byte;
	int ret;

	while (skb->len || p->phase == XMM_PARSE_ALLOC ||
	       p->phase == XMM_PARSE_DISPATCH) {
		switch (p->phase) {
		case XMM_PARSE_START:
			byte = skb->data[0];
			skb_pull(skb, 1);
			if (byte != XMM_HDLC_START) {
				/*
				 * A stray byte or optional link padding must not
				 * discard valid IPC frames later in this same USB
				 * transfer. Consume it and look for the next 0x7f.
				 */
				if (pipe->id == XMM_PIPE_FMT)
					atomic64_inc(&rt->fmt_parse_errors);
				break;
			}
			p->header_pos = 0;
			p->phase = XMM_PARSE_HEADER;
			break;
		case XMM_PARSE_HEADER:
			n = min_t(unsigned int, skb->len,
				  p->header_len - p->header_pos);
			memcpy(p->header + p->header_pos, skb->data, n);
			p->header_pos += n;
			skb_pull(skb, n);
			if (p->header_pos == p->header_len) {
				ret = xmm_parser_header_done(pipe);
				if (ret) {
					/* Malformed header: resynchronize instead of
					 * dropping all subsequent frames in this URB. */
					if (pipe->id == XMM_PIPE_FMT)
						atomic64_inc(&rt->fmt_parse_errors);
					xmm_parser_reset(p, p->header_len);
					break;
				}
			}
			break;
		case XMM_PARSE_ALLOC:
			if (pipe->id == XMM_PIPE_RFS) {
				ret = xmm_rfs_header(rt, p);
				if (ret)
					return ret;
				p->phase = p->data_len ? XMM_PARSE_DATA : XMM_PARSE_END;
				break;
			}
			p->frame = alloc_skb(p->data_len, GFP_KERNEL);
			if (!p->frame)
				return -ENOMEM;
			p->phase = XMM_PARSE_DATA;
			break;
		case XMM_PARSE_DATA:
			left = p->data_len - p->data_pos;
			if (pipe->id == XMM_PIPE_RFS) {
				if (!p->frame) {
					p->frame = alloc_skb(min_t(u32, left, XMM_RX_CHUNK),
							     GFP_KERNEL);
					if (!p->frame)
						return -ENOMEM;
				}
				n = min_t(unsigned int, skb->len, left);
				n = min_t(unsigned int, n, skb_tailroom(p->frame));
				skb_put_data(p->frame, skb->data, n);
				skb_pull(skb, n);
				p->data_pos += n;
				if (!skb_tailroom(p->frame) || p->data_pos == p->data_len) {
					xmm_queue_misc(&rt->io[XMM_IOD_RFS], p->frame);
					p->frame = NULL;
				}
			} else {
				n = min_t(unsigned int, skb->len, left);
				skb_put_data(p->frame, skb->data, n);
				skb_pull(skb, n);
				p->data_pos += n;
			}
			if (p->data_pos == p->data_len)
				p->phase = XMM_PARSE_END;
			break;
		case XMM_PARSE_END:
			byte = skb->data[0];
			skb_pull(skb, 1);
			if (byte != XMM_HDLC_END) {
				/*
				 * Bad terminator: discard only the damaged frame.
				 * If it was a new START, retain that delimiter and
				 * continue reading the next frame's header.
				 */
				if (pipe->id == XMM_PIPE_FMT)
					atomic64_inc(&rt->fmt_parse_errors);
				xmm_parser_reset(p, p->header_len);
				if (byte == XMM_HDLC_START)
					p->phase = XMM_PARSE_HEADER;
				break;
			}
			p->phase = XMM_PARSE_DISPATCH;
			fallthrough;
		case XMM_PARSE_DISPATCH:
			if (pipe->id == XMM_PIPE_FMT)
				ret = xmm_dispatch_fmt(rt, p);
			else if (pipe->id == XMM_PIPE_RAW)
				ret = xmm_dispatch_raw(rt, p);
			else
				ret = 0;
			if (ret == -ENOMEM)
				return ret;
			if (ret) {
				xmm_parser_reset(p, p->header_len);
				return ret;
			}
			xmm_parser_reset(p, p->header_len);
			break;
		}
	}

	return 0;
}

static void xmm_handle_cmd(struct xmm_runtime *rt, struct xmm_pipe *pipe,
			   struct sk_buff *skb)
{
	u16 cmd;

	while (skb->len) {
		if (!pipe->cmd_half) {
			pipe->cmd_byte = skb->data[0];
			pipe->cmd_half = true;
			skb_pull(skb, 1);
			continue;
		}

		cmd = pipe->cmd_byte | ((u16)skb->data[0] << 8);
		pipe->cmd_half = false;
		skb_pull(skb, 1);

		if (cmd == XMM_CMD_SUSPEND)
			xmm_set_raw_suspended(rt, true);
		else if (cmd == XMM_CMD_RESUME)
			xmm_set_raw_suspended(rt, false);
	}
}

static void xmm_rx_work(struct work_struct *work)
{
	struct xmm_pipe *pipe = container_of(to_delayed_work(work),
					      struct xmm_pipe, rx_work);
	struct sk_buff *skb;
	int ret;

	while ((skb = skb_dequeue(&pipe->rx_queue))) {
		if (pipe->id == XMM_PIPE_CMD) {
			xmm_handle_cmd(pipe->rt, pipe, skb);
			kfree_skb(skb);
			continue;
		}

		ret = xmm_parse_stream(pipe, skb);
		if (ret < 0 && ret != -ENOMEM && pipe->id == XMM_PIPE_FMT)
			atomic64_inc(&pipe->rt->fmt_parse_errors);
		if (ret == -ENOMEM) {
			skb_queue_head(&pipe->rx_queue, skb);
			queue_delayed_work(system_wq, &pipe->rx_work,
					   msecs_to_jiffies(XMM_RX_RETRY_MS));
			return;
		}
		kfree_skb(skb);
	}
}

static void xmm_rx_complete(struct urb *urb);

static int xmm_resubmit_rx(struct xmm_pipe *pipe, gfp_t gfp)
{
	struct usb_device *udev;

	if (!pipe->present || pipe->suspended)
		return -ENODEV;

	udev = interface_to_usbdev(pipe->ctrl_intf);
	usb_fill_bulk_urb(pipe->rx_urb, udev, pipe->rx_pipe, pipe->rx_buf,
			  XMM_RX_SIZE, xmm_rx_complete, pipe);
	return usb_submit_urb(pipe->rx_urb, gfp);
}

/* USB can reject a completed RX URB's immediate atomic resubmission,
 * notably under temporary allocation pressure. Retry just the SUBMISSION;
 * never recopy the prior packet, which would duplicate a modem IPC response. */
static bool xmm_rx_submit_transient(int ret)
{
	return ret == -ENOMEM || ret == -EAGAIN;
}

static void xmm_rx_submit_retry_work(struct work_struct *work)
{
	struct xmm_pipe *pipe = container_of(to_delayed_work(work),
					      struct xmm_pipe, rx_submit_work);
	int ret;

	ret = xmm_resubmit_rx(pipe, GFP_KERNEL);
	if (xmm_rx_submit_transient(ret) &&
	    READ_ONCE(pipe->present) && !READ_ONCE(pipe->suspended)) {
		if (pipe->id == XMM_PIPE_FMT)
			atomic64_inc(&pipe->rt->fmt_rx_submit_retries);
		queue_delayed_work(system_wq, &pipe->rx_submit_work,
					   msecs_to_jiffies(XMM_RX_RETRY_MS));
	} else if (ret && ret != -ENODEV && ret != -EBUSY) {
		if (pipe->id == XMM_PIPE_FMT)
			atomic64_inc(&pipe->rt->fmt_rx_submit_errors);
		pr_warn_ratelimited("xmm6262-runtime: RX resubmit pipe %d failed: %d\n",
				    pipe->id, ret);
	}
}

static int xmm_rearm_rx(struct xmm_pipe *pipe, gfp_t gfp)
{
	int ret = xmm_resubmit_rx(pipe, gfp);

	if (xmm_rx_submit_transient(ret) &&
	    READ_ONCE(pipe->present) && !READ_ONCE(pipe->suspended)) {
		if (pipe->id == XMM_PIPE_FMT)
			atomic64_inc(&pipe->rt->fmt_rx_submit_retries);
		queue_delayed_work(system_wq, &pipe->rx_submit_work,
					   msecs_to_jiffies(XMM_RX_RETRY_MS));
		return 0;
	}
	if (ret && ret != -ENODEV && ret != -EBUSY &&
	    pipe->id == XMM_PIPE_FMT)
		atomic64_inc(&pipe->rt->fmt_rx_submit_errors);
	return ret;
}

static int xmm_copy_rx_urb(struct xmm_pipe *pipe)
{
	struct sk_buff *skb;

	if (!pipe->rx_urb->actual_length)
		return 0;

	if (pipe->id == XMM_PIPE_FMT)
		xmm_wake_io(&pipe->rt->io[XMM_IOD_FMT]);
	else if (pipe->id == XMM_PIPE_RAW)
		xmm_wake_io(&pipe->rt->io[XMM_IOD_MULTIPDP]);
	else if (pipe->id == XMM_PIPE_RFS)
		xmm_wake_io(&pipe->rt->io[XMM_IOD_RFS]);

	skb = alloc_skb(pipe->rx_urb->actual_length, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, pipe->rx_urb->transfer_buffer,
		     pipe->rx_urb->actual_length);
	skb_queue_tail(&pipe->rx_queue, skb);
	queue_delayed_work(system_wq, &pipe->rx_work, 0);
	return 0;
}

static void xmm_rx_retry_work(struct work_struct *work)
{
	struct xmm_pipe *pipe = container_of(to_delayed_work(work),
					      struct xmm_pipe, rx_retry_work);
	int status;
	int ret;

	/* Suspend/disconnect must never copy a cancelled URB buffer or
	 * resurrect its receive loop after usb_kill_urb(). */
	if (!READ_ONCE(pipe->present) || READ_ONCE(pipe->suspended))
		return;
	status = READ_ONCE(pipe->rx_urb->status);
	if (status)
		return;

	ret = xmm_copy_rx_urb(pipe);
	if (ret == -ENOMEM) {
		if (pipe->rx_retry_count++ < XMM_RX_RETRY_MAX) {
			queue_delayed_work(system_wq, &pipe->rx_retry_work,
					   msecs_to_jiffies(XMM_RX_RETRY_MS));
			return;
		}
		/*
		 * The stock retry path just returned here forever, leaving
		 * this RX URB idle and the modem IPC channel unresponsive.
		 * Preserve later calls/SMS by dropping only the unrecoverable
		 * buffer and rearming USB reception after retries are exhausted.
		 */
		if (pipe->id == XMM_PIPE_FMT)
			atomic64_inc(&pipe->rt->fmt_rx_oom_drops);
		pr_warn_ratelimited("xmm6262-runtime: RX pipe %d OOM after %d retries; resuming\n",
				    pipe->id, XMM_RX_RETRY_MAX);
	}

	pipe->rx_retry_count = 0;
	if (!status)
		(void)xmm_rearm_rx(pipe, GFP_KERNEL);
}

static void xmm_rx_complete(struct urb *urb)
{
	struct xmm_pipe *pipe = urb->context;
	int status = urb->status;
	int ret;

	if (status) {
		/*
		 * A cancelled/failed URB has no trusted IPC payload. Never retry
		 * copying its stale RX buffer, particularly across HSIC suspend.
		 * Only transient transport errors should restart USB reception;
		 * usb_kill_urb() completion is terminal for this URB.
		 */
		if (status == -ENOENT || status == -ECONNRESET ||
		    status == -ESHUTDOWN)
			return;
		if (pipe->id == XMM_PIPE_FMT)
			atomic64_inc(&pipe->rt->fmt_rx_urb_errors);
		(void)xmm_rearm_rx(pipe, GFP_ATOMIC);
		return;
	}

	if (pipe->id == XMM_PIPE_FMT) {
		atomic64_inc(&pipe->rt->fmt_rx_urbs);
		atomic64_add(urb->actual_length, &pipe->rt->fmt_rx_bytes);
	}
	ret = xmm_copy_rx_urb(pipe);
	if (ret == -ENOMEM) {
		pipe->rx_retry_count = 0;
		queue_delayed_work(system_wq, &pipe->rx_retry_work,
				   msecs_to_jiffies(XMM_RX_RETRY_MS));
		return;
	}

	if (!status)
		(void)xmm_rearm_rx(pipe, GFP_ATOMIC);
}

static void xmm_tx_complete(struct urb *urb)
{
	struct xmm_tx_ctx *ctx = urb->context;

	if (XMM_TX_CB(ctx->skb)->pipe == XMM_PIPE_FMT) {
		if (urb->status)
			atomic64_inc(&ctx->rt->fmt_tx_errors);
		else
			atomic64_inc(&ctx->rt->fmt_tx_ok);
	}
	usb_unanchor_urb(urb);
	pm_runtime_mark_last_busy(&ctx->udev->dev);
	pm_runtime_put_autosuspend(&ctx->udev->dev);
	usb_put_dev(ctx->udev);
	kfree_skb(ctx->skb);
	kfree(ctx);
}

static int xmm_submit_tx(struct xmm_runtime *rt, struct sk_buff *skb)
{
	struct xmm_tx_ctx *ctx;
	struct xmm_pipe *pipe;
	struct urb *urb;
	struct usb_device *udev;
	int ret;

	pipe = &rt->pipe[XMM_TX_CB(skb)->pipe];
	mutex_lock(&rt->lock);
	if (!rt->runtime_connected || !pipe->present || !rt->udev) {
		mutex_unlock(&rt->lock);
		return -ENODEV;
	}
	udev = usb_get_dev(rt->udev);
	mutex_unlock(&rt->lock);


	ret = pm_runtime_resume_and_get(&udev->dev);
	if (ret < 0) {
		usb_put_dev(udev);
		return ret;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		pm_runtime_put(&udev->dev);
		usb_put_dev(udev);
		return -ENOMEM;
	}

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		usb_free_urb(urb);
		pm_runtime_put(&udev->dev);
		usb_put_dev(udev);
		return -ENOMEM;
	}

	ctx->rt = rt;
	ctx->udev = udev;
	ctx->skb = skb;
	usb_fill_bulk_urb(urb, udev, pipe->tx_pipe, skb->data, skb->len,
			  xmm_tx_complete, ctx);
	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &rt->tx_anchor);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		usb_unanchor_urb(urb);
		pm_runtime_put(&udev->dev);
		usb_put_dev(ctx->udev);
		kfree(ctx);
	}
	usb_free_urb(urb);
	return ret;
}

static void xmm_tx_work(struct work_struct *work)
{
	struct xmm_runtime *rt = container_of(to_delayed_work(work),
					       struct xmm_runtime, tx_work);
	struct sk_buff *skb;
	int ret;

	for (;;) {
		skb = skb_dequeue(&rt->fmt_tx_queue);
		if (skb) {
			ret = xmm_submit_tx(rt, skb);
			if (ret) {
				skb_queue_head(&rt->fmt_tx_queue, skb);
				goto retry;
			}
		}

		skb = skb_dequeue(&rt->raw_tx_queue);
		if (skb) {
			ret = xmm_submit_tx(rt, skb);
			if (ret) {
				skb_queue_head(&rt->raw_tx_queue, skb);
				goto retry;
			}
		}

		if (skb_queue_empty(&rt->fmt_tx_queue) &&
		    skb_queue_empty(&rt->raw_tx_queue))
			return;
	}

retry:
	if (READ_ONCE(rt->runtime_connected))
		queue_delayed_work(system_wq, &rt->tx_work,
				   msecs_to_jiffies(XMM_TX_RETRY_MS));
}

static int xmm_enqueue_tx(struct xmm_runtime *rt, struct sk_buff *skb,
			  int pipe, bool raw)
{
	unsigned long flags;
	int ret;

	/*
	 * Serialize the link-generation gate with queued FMT/RAW writes,
	 * including netdev xmit (atomic context). A request may not enter
	 * the old session's queue after disconnect has closed the gate.
	 */
	spin_lock_irqsave(&rt->tx_gate_lock, flags);
	if (!READ_ONCE(rt->runtime_connected)) {
		spin_unlock_irqrestore(&rt->tx_gate_lock, flags);
		return -ENODEV;
	}

	XMM_TX_CB(skb)->pipe = pipe;
	if (raw)
		skb_queue_tail(&rt->raw_tx_queue, skb);
	else
		skb_queue_tail(&rt->fmt_tx_queue, skb);
	ret = skb->len;
	queue_delayed_work(system_wq, &rt->tx_work, 0);
	spin_unlock_irqrestore(&rt->tx_gate_lock, flags);
	return ret;
}

static struct sk_buff *xmm_build_fmt(const char __user *buf, size_t count)
{
	struct sk_buff *skb;
	u8 *p;

	if (count > U16_MAX - 3)
		return ERR_PTR(-EMSGSIZE);

	skb = alloc_skb(count + 5, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	p = skb_put(skb, 4);
	p[0] = XMM_HDLC_START;
	put_unaligned_le16(count + 3, p + 1);
	p[3] = 0;
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return ERR_PTR(-EFAULT);
	}
	skb_put_u8(skb, XMM_HDLC_END);
	return skb;
}

static struct sk_buff *xmm_build_rfs(const char __user *buf, size_t count)
{
	struct sk_buff *skb;

	skb = alloc_skb(count + 2, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);
	skb_put_u8(skb, XMM_HDLC_START);
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return ERR_PTR(-EFAULT);
	}
	skb_put_u8(skb, XMM_HDLC_END);
	return skb;
}

static struct sk_buff *xmm_build_raw_user(struct xmm_io *io,
					 const char __user *buf, size_t count)
{
	struct sk_buff *skb;
	u8 *p;

	if (count > U32_MAX - 6)
		return ERR_PTR(-EMSGSIZE);

	skb = alloc_skb(count + 8, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	p = skb_put(skb, 7);
	p[0] = XMM_HDLC_START;
	put_unaligned_le32(count + 6, p + 1);
	p[5] = io->id & 0x1f;
	p[6] = 0;
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		return ERR_PTR(-EFAULT);
	}
	skb_put_u8(skb, XMM_HDLC_END);
	return skb;
}

static struct sk_buff *xmm_build_raw_skb(struct xmm_runtime *rt,
					struct xmm_io *io,
					struct sk_buff *skb)
{
	struct sk_buff *out;
	struct iphdr *iph;
	u8 channel = io->id & 0x1f;
	u8 *p;

	if (skb->len && rt->loopback_ip && (skb->data[0] >> 4) == 4 &&
	    skb->len >= sizeof(*iph) &&
	    ((struct iphdr *)skb->data)->daddr == rt->loopback_ip)
		channel = 0x1f;

	out = alloc_skb(skb->len + 8, GFP_ATOMIC);
	if (!out)
		return ERR_PTR(-ENOMEM);

	p = skb_put(out, 7);
	p[0] = XMM_HDLC_START;
	put_unaligned_le32(skb->len + 6, p + 1);
	p[5] = channel;
	p[6] = 0;
	skb_put_data(out, skb->data, skb->len);
	if (channel == 0x1f && skb->len >= sizeof(*iph)) {
		iph = (struct iphdr *)(out->data + 7);
		swap(iph->saddr, iph->daddr);
	}
	skb_put_u8(out, XMM_HDLC_END);
	return out;
}

static int xmm_start_ipc(struct xmm_runtime *rt)
{
	struct sk_buff *skb;

	/* L1: runtime transport owns its connectivity gate; modem-control stays untouched. */
	if (!READ_ONCE(rt->runtime_connected))
		return -ENODEV;

	skb = alloc_skb(1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_put_u8(skb, 'a');
	if (xmm_enqueue_tx(rt, skb, XMM_PIPE_FMT, false) < 0) {
		kfree_skb(skb);
		return -ENODEV;
	}
	return 0;
}

static int xmm_misc_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct xmm_io *io = container_of(misc, struct xmm_io, miscdev);
	int ret = 0;

	file->private_data = io;
	atomic_inc(&io->opened);
	if (io->format == XMM_IO_FMT)
		tab3_xmm6262_fmt_open(true);
	if (io->id == 0x01)
		ret = xmm_start_ipc(io->rt);
	if (ret) {
		if (io->format == XMM_IO_FMT)
			tab3_xmm6262_fmt_open(false);
		atomic_dec(&io->opened);
	}
	return ret;
}

static int xmm_misc_release(struct inode *inode, struct file *file)
{
	struct xmm_io *io = file->private_data;

	if (io->format == XMM_IO_FMT)
		tab3_xmm6262_fmt_open(false);
	atomic_dec(&io->opened);
	skb_queue_purge(&io->rx_queue);
	return 0;
}

static ssize_t xmm_misc_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct xmm_io *io = file->private_data;
	struct sk_buff *skb;
	size_t len;

	skb = skb_dequeue(&io->rx_queue);
	if (!skb)
		return 0;

	if (skb->len > count) {
		kfree_skb(skb);
		return -EFAULT;
	}

	len = skb->len;
	if (copy_to_user(buf, skb->data, len)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	kfree_skb(skb);
	return len;
}

static ssize_t xmm_misc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct xmm_io *io = file->private_data;
	struct xmm_runtime *rt = io->rt;
	struct sk_buff *skb;
	int pipe;
	bool raw = false;
	int ret;

	if (io->format == XMM_IO_RAW || io->format == XMM_IO_MULTI_RAW) {
		ret = wait_event_interruptible(rt->raw_wait,
			!READ_ONCE(rt->raw_tx_suspended) ||
			!READ_ONCE(rt->runtime_connected));
		if (ret)
			return ret;
		if (!READ_ONCE(rt->runtime_connected))
			return -ENODEV;
	}

	switch (io->format) {
	case XMM_IO_FMT:
		skb = xmm_build_fmt(buf, count);
		pipe = XMM_PIPE_FMT;
		break;
	case XMM_IO_RFS:
		skb = xmm_build_rfs(buf, count);
		pipe = XMM_PIPE_RFS;
		break;
	case XMM_IO_RAW:
	case XMM_IO_MULTI_RAW:
		skb = xmm_build_raw_user(io, buf, count);
		pipe = XMM_PIPE_RAW;
		raw = true;
		break;
	case XMM_IO_RAMDUMP:
		return -ENODEV;
	default:
		return -EINVAL;
	}

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = xmm_enqueue_tx(rt, skb, pipe, raw);
	if (ret < 0) {
		kfree_skb(skb);
		return ret;
	}
	return count;
}

static __poll_t xmm_misc_poll(struct file *file, poll_table *wait)
{
	struct xmm_io *io = file->private_data;
	int state;

	poll_wait(file, &io->wait, wait);
	tab3_xmm6262_poll_state(file, wait);
	state = tab3_xmm6262_get_state();

	if (!skb_queue_empty(&io->rx_queue) && state != TAB3_XMM_OFFLINE)
		return EPOLLIN | EPOLLRDNORM;
	if (state == TAB3_XMM_CRASH_RESET || state == TAB3_XMM_CRASH_EXIT ||
	    state == TAB3_XMM_NV_REBUILDING || tab3_xmm6262_sim_changed()) {
		if (io->format == XMM_IO_RAW) {
			msleep(20);
			return 0;
		}
		return EPOLLHUP;
	}
	return 0;
}

static long xmm_misc_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct xmm_io *io = file->private_data;
	struct xmm_runtime *rt = io->rt;
	long ret;

	if (cmd == IOCTL_MODEM_PROTOCOL_SUSPEND) {
		if (io->format != XMM_IO_MULTI_RAW)
			return -EINVAL;
		xmm_stop_net_queues(rt);
		return 0;
	}
	if (cmd == IOCTL_MODEM_PROTOCOL_RESUME) {
		if (io->format != XMM_IO_MULTI_RAW)
			return -EINVAL;
		xmm_wake_net_queues(rt);
		return 0;
	}

	ret = tab3_xmm6262_modem_compat_ioctl(cmd);
	return ret == -ENOIOCTLCMD ? -EINVAL : ret;
}

static const struct file_operations xmm_misc_fops = {
	.owner = THIS_MODULE,
	.open = xmm_misc_open,
	.release = xmm_misc_release,
	.read = xmm_misc_read,
	.write = xmm_misc_write,
	.poll = xmm_misc_poll,
	.unlocked_ioctl = xmm_misc_ioctl,
};

static ssize_t waketime_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct miscdevice *misc = dev_get_drvdata(dev);
	struct xmm_io *io = container_of(misc, struct xmm_io, miscdev);

	return sysfs_emit(buf, "raw waketime : %ums\n", io->waketime_ms);
}

static ssize_t waketime_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct miscdevice *misc = dev_get_drvdata(dev);
	struct xmm_io *io = container_of(misc, struct xmm_io, miscdev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return count;
	io->waketime_ms = value;
	return count;
}

static DEVICE_ATTR_RW(waketime);

static ssize_t loopback_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct miscdevice *misc = dev_get_drvdata(dev);
	struct xmm_io *io = container_of(misc, struct xmm_io, miscdev);

	return sysfs_emit(buf, "%pI4\n", &io->rt->loopback_ip);
}

static ssize_t loopback_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct miscdevice *misc = dev_get_drvdata(dev);
	struct xmm_io *io = container_of(misc, struct xmm_io, miscdev);
	char tmp[32];
	size_t len = min(count, sizeof(tmp) - 1);
	__be32 addr;

	memcpy(tmp, buf, len);
	tmp[len] = '\0';
	strim(tmp);
	if (!in4_pton(tmp, -1, (u8 *)&addr, -1, NULL))
		addr = 0;
	io->rt->loopback_ip = addr;
	return count;
}

static DEVICE_ATTR_RW(loopback);

/* One read-only snapshot before/after an authorized SMS test isolates whether
 * FMT RX/TX, parsing or HSIC power transitions changed. Never expose payloads. */
static ssize_t fmt_stats_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct miscdevice *misc = dev_get_drvdata(dev);
	struct xmm_io *io = container_of(misc, struct xmm_io, miscdev);
	struct xmm_runtime *rt = io->rt;

	return sysfs_emit(buf,
		"rx_urbs=%lld rx_bytes=%lld rx_urb_errors=%lld rx_oom_drops=%lld "
		"rx_submit_retries=%lld rx_submit_errors=%lld "
		"rx_frames=%lld parse_errors=%lld tx_ok=%lld tx_errors=%lld "
		"pm_suspend=%lld pm_resume=%lld "
		"hostwake_active=%lld hostwake_idle=%lld "
		"hostwake_pm_ok=%lld hostwake_pm_err=%lld "
		"pm_boot_fmt_waits=%lld pm_boot_grace_runs=%lld "
		"pm_boot_deadline_expired=%lld\n",
		(long long)atomic64_read(&rt->fmt_rx_urbs),
		(long long)atomic64_read(&rt->fmt_rx_bytes),
		(long long)atomic64_read(&rt->fmt_rx_urb_errors),
		(long long)atomic64_read(&rt->fmt_rx_oom_drops),
		(long long)atomic64_read(&rt->fmt_rx_submit_retries),
		(long long)atomic64_read(&rt->fmt_rx_submit_errors),
		(long long)atomic64_read(&rt->fmt_rx_frames),
		(long long)atomic64_read(&rt->fmt_parse_errors),
		(long long)atomic64_read(&rt->fmt_tx_ok),
		(long long)atomic64_read(&rt->fmt_tx_errors),
		(long long)atomic64_read(&rt->fmt_pm_suspend),
		(long long)atomic64_read(&rt->fmt_pm_resume),
		(long long)atomic64_read(&rt->hostwake_active),
		(long long)atomic64_read(&rt->hostwake_idle),
		(long long)atomic64_read(&rt->hostwake_pm_ok),
		(long long)atomic64_read(&rt->hostwake_pm_err),
		(long long)atomic64_read(&rt->pm_boot_fmt_waits),
		(long long)atomic64_read(&rt->pm_boot_grace_runs),
		(long long)atomic64_read(&rt->pm_boot_deadline_expired));
}
static DEVICE_ATTR_RO(fmt_stats);

static int xmm_net_open(struct net_device *ndev)
{
	struct xmm_net_priv *priv = netdev_priv(ndev);

	atomic_inc(&priv->io->opened);
	if (!READ_ONCE(priv->io->rt->raw_tx_suspended))
		netif_start_queue(ndev);
	return 0;
}

static int xmm_net_stop(struct net_device *ndev)
{
	struct xmm_net_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	atomic_dec(&priv->io->opened);
	return 0;
}

static netdev_tx_t xmm_net_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct xmm_net_priv *priv = netdev_priv(ndev);
	struct xmm_io *io = priv->io;
	struct xmm_runtime *rt = io->rt;
	struct sk_buff *out;
	unsigned int len = skb->len;
	int ret;

	if (READ_ONCE(rt->raw_tx_suspended) || !READ_ONCE(rt->runtime_connected)) {
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	out = xmm_build_raw_skb(rt, io, skb);
	if (IS_ERR(out))
		return NETDEV_TX_BUSY;

	ret = xmm_enqueue_tx(rt, out, XMM_PIPE_RAW, true);
	if (ret < 0) {
		kfree_skb(out);
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}
	consume_skb(skb);

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += len;
	return NETDEV_TX_OK;
}

static const struct net_device_ops xmm_net_ops = {
	.ndo_open = xmm_net_open,
	.ndo_stop = xmm_net_stop,
	.ndo_start_xmit = xmm_net_xmit,
};

static void xmm_net_setup(struct net_device *ndev)
{
	ndev->netdev_ops = &xmm_net_ops;
	ndev->type = ARPHRD_PPP;
	ndev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	ndev->addr_len = 0;
	ndev->hard_header_len = 0;
	ndev->tx_queue_len = 1000;
	ndev->mtu = 1500;
	ndev->watchdog_timeo = 5 * HZ;
}

static void xmm_runtime_hostwake_cb(bool active, void *data)
{
	struct xmm_runtime *rt = data;
	struct usb_device *udev = NULL;
	int ret;

	if (!active) {
		atomic64_inc(&rt->hostwake_idle);
		return;
	}
	atomic64_inc(&rt->hostwake_active);
	mutex_lock(&rt->lock);
	if (rt->runtime_connected && rt->pm_started && rt->udev)
		udev = usb_get_dev(rt->udev);
	mutex_unlock(&rt->lock);
	if (!udev)
		return;

	/*
	 * pm_runtime_resume() alone doesn't acquire a usage reference:
	 * with USB autosuspend=200ms a concurrent idle decision can win
	 * before the CP's unsolicited FMT RX has been rearmed.  Take a
	 * synchronous reference, then start the autosuspend grace period.
	 * Do not pin the bus for the entire asserted HOSTWAKE level:
	 * stock XMM6262 can leave that GPIO low for extended idle periods.
	 * Neither GPIO levels, SIM state nor IPC frames are modified here.
	 */
	ret = pm_runtime_resume_and_get(&udev->dev);
	if (ret >= 0) {
		atomic64_inc(&rt->hostwake_pm_ok);
		pm_runtime_mark_last_busy(&udev->dev);
		pm_runtime_put_autosuspend(&udev->dev);
	} else {
		atomic64_inc(&rt->hostwake_pm_err);
	}
	pr_debug("xmm6262-runtime: HOSTWAKE_PM_WAKE ret=%d status=%d\n",
		ret, udev->dev.power.runtime_status);
	usb_put_dev(udev);
}

/* Pure and bounded policy; separate from the USB runtime-PM operations.
 * Poll for the FIRST /dev/umts_ipc0 RIL open, then leave modem USB active
 * for at most 75 seconds to complete late Android11 carrier registration
 * and the FIRST PDP setup; boot RIL is ready ~50s before SETUP_DATA_CALL. If no
 * client opens, stop polling after 45 seconds so power saving still works.
 * No APN/operator assumptions and no modem commands are generated.
 */
static unsigned int xmm_pm_boot_hold_ms(unsigned long now,
		unsigned long deadline, bool fmt_open, bool *grace_started)
{
	if (*grace_started)
		return 0;
	if (fmt_open) {
		*grace_started = true;
		return XMM_PM_BOOT_GRACE_MS;
	}
	if (time_before(now, deadline))
		return XMM_PM_BOOT_POLL_MS;
	return 0;
}

static void xmm_pm_start_work(struct work_struct *work)
{
	struct xmm_runtime *rt = container_of(to_delayed_work(work),
					       struct xmm_runtime, pm_start_work);
	struct usb_device *udev;
	struct device *hdev;
	unsigned int boot_delay;
	bool had_grace;
	bool fmt_open;

	mutex_lock(&rt->lock);
	if (!rt->runtime_connected || !rt->hostwake_registered || !rt->udev) {
		mutex_unlock(&rt->lock);
		return;
	}
	udev = usb_get_dev(rt->udev);
	mutex_unlock(&rt->lock);

	if (udev->dev.power.runtime_status == RPM_SUSPENDED) {
		usb_put_dev(udev);
		queue_delayed_work(system_wq, &rt->pm_start_work,
				   msecs_to_jiffies(20));
		return;
	}

	/* G372: stock-style delayed PM activation until first live FMT user.
	 * G371 proved USB /power/control=on for 11 seconds repairs cold-start
	 * PDP while auto before any registration can fail with NAS_SIGNALLING.
	 * Never apply this after the first grace or beyond bounded deadline.
	 */
	fmt_open = atomic_read(&rt->io[XMM_IOD_FMT].opened) > 0;
	had_grace = rt->pm_boot_grace_started;
	boot_delay = xmm_pm_boot_hold_ms(jiffies, rt->pm_boot_deadline,
				fmt_open, &rt->pm_boot_grace_started);
	if (boot_delay) {
		if (!had_grace && rt->pm_boot_grace_started)
			atomic64_inc(&rt->pm_boot_grace_runs);
		else
			atomic64_inc(&rt->pm_boot_fmt_waits);
		usb_put_dev(udev);
		queue_delayed_work(system_wq, &rt->pm_start_work,
				msecs_to_jiffies(boot_delay));
		return;
	}
	if (!rt->pm_boot_grace_started && !fmt_open)
		atomic64_inc(&rt->pm_boot_deadline_expired);
	pm_runtime_set_autosuspend_delay(&udev->dev, 200);
	/* G626 A/B only: keep XMM6262 USB child awake after first FMT.
	 * Isolate late CALL_RELEASE ACK from HSIC autosuspend without
	 * poking SLAVE_WAKE (rejected G514). Increased idle power possible. */
	pm_runtime_forbid(&udev->dev);
	hdev = udev->bus->root_hub->dev.parent;
	if (hdev)
		pm_runtime_allow(hdev);

	mutex_lock(&rt->lock);
	if (rt->runtime_connected && rt->udev == udev) {
		rt->pm_started = true;
		rt->reconnect_left = XMM_RECONNECT_TRIES;
	}
	mutex_unlock(&rt->lock);

	pr_debug("xmm6262-runtime: RUNTIME_PM_START autosuspend_ms=200 ehci=%s\n",
		hdev ? dev_name(hdev) : "none");
	queue_delayed_work(system_wq, &rt->tx_work, 0);
	usb_put_dev(udev);
}

static void xmm_reconnect_work(struct work_struct *work)
{
	struct xmm_runtime *rt = container_of(to_delayed_work(work),
					       struct xmm_runtime, reconnect_work);
	unsigned int left;
	int state;
	int ret;

	if (READ_ONCE(rt->runtime_connected))
		return;
	state = tab3_xmm6262_get_state();
	if (state != TAB3_XMM_ONLINE)
		return;

	mutex_lock(&rt->lock);
	left = rt->reconnect_left;
	if (left)
		rt->reconnect_left--;
	mutex_unlock(&rt->lock);

	if (!left) {
		pr_err("xmm6262-runtime: RECONNECT exhausted -> CRASH_RESET\n");
		tab3_xmm6262_set_state(TAB3_XMM_CRASH_RESET);
		return;
	}

	ret = tab3_xmm6262_hsic_link_reconnect();
	pr_debug("xmm6262-runtime: RECONNECT try_left=%u ret=%d\n",
		left - 1, ret);
	if (!ret)
		queue_delayed_work(system_wq, &rt->reconnect_work,
				   msecs_to_jiffies(500));
}

static const struct usb_cdc_union_desc *xmm_find_union(struct usb_interface *intf)
{
	const struct usb_cdc_union_desc *union_desc = NULL;
	const u8 *buf = intf->cur_altsetting->extra;
	int len = intf->cur_altsetting->extralen;

	if (!len && intf->cur_altsetting->desc.bNumEndpoints) {
		buf = intf->cur_altsetting->endpoint[0].extra;
		len = intf->cur_altsetting->endpoint[0].extralen;
	}

	while (buf && len >= 3 && buf[0] >= 3 && buf[0] <= len) {
		if (buf[1] == USB_DT_CS_INTERFACE &&
		    buf[2] == USB_CDC_UNION_TYPE &&
		    buf[0] >= sizeof(*union_desc)) {
			union_desc = (const struct usb_cdc_union_desc *)buf;
			break;
		}
		len -= buf[0];
		buf += buf[0];
	}

	return union_desc;
}

static int xmm_runtime_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
	struct xmm_runtime *rt = xmm_rt;
	const struct usb_cdc_union_desc *union_desc;
	struct usb_endpoint_descriptor *ep_in, *ep_out;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_interface *data_intf;
	struct xmm_pipe *pipe;
	int pipe_id;
	int ret;
	bool pm_was_auto;
	unsigned long tx_flags;

	if (!rt)
		return -ENODEV;
	if (intf->cur_altsetting->desc.bInterfaceSubClass != USB_CDC_SUBCLASS_ACM)
		return -ENODEV;

	pipe_id = intf->cur_altsetting->desc.bInterfaceNumber / 2;
	if (pipe_id < 0 || pipe_id >= XMM_PIPE_COUNT)
		return -ENODEV;

	union_desc = xmm_find_union(intf);
	if (!union_desc)
		return -EINVAL;
	data_intf = usb_ifnum_to_if(udev, union_desc->bSlaveInterface0);
	if (!data_intf)
		return -ENODEV;
	ret = usb_find_common_endpoints(data_intf->cur_altsetting,
					&ep_in, &ep_out, NULL, NULL);
	if (ret)
		return ret;

	pipe = &rt->pipe[pipe_id];
	cancel_delayed_work_sync(&rt->reconnect_work);
	mutex_lock(&rt->lock);
	if (pipe->present) {
		mutex_unlock(&rt->lock);
		return -EBUSY;
	}
	if (!rt->udev)
		rt->udev = usb_get_dev(udev);
	else if (rt->udev != udev) {
		mutex_unlock(&rt->lock);
		return -EBUSY;
	}

	pm_was_auto = udev->dev.power.runtime_auto;
	pm_runtime_forbid(&udev->dev);
	pipe->ctrl_intf = intf;
	pipe->data_intf = data_intf;
	pipe->rx_pipe = usb_rcvbulkpipe(udev, ep_in->bEndpointAddress);
	pipe->tx_pipe = usb_sndbulkpipe(udev, ep_out->bEndpointAddress);
	pipe->suspended = false;
	pipe->present = true;
	pipe->cmd_half = false;
	pipe->rx_retry_count = 0;

	pipe->rx_buf = kmalloc(XMM_RX_SIZE, GFP_KERNEL);
	if (!pipe->rx_buf) {
		ret = -ENOMEM;
		goto err_pipe;
	}
	pipe->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pipe->rx_urb) {
		ret = -ENOMEM;
		goto err_buf;
	}

	ret = usb_driver_claim_interface(to_usb_driver(intf->dev.driver),
					 data_intf, pipe);
	if (ret)
		goto err_urb;
	usb_set_intfdata(intf, pipe);
	usb_set_intfdata(data_intf, pipe);
	pm_suspend_ignore_children(&udev->dev, true);

	usb_fill_bulk_urb(pipe->rx_urb, udev, pipe->rx_pipe, pipe->rx_buf,
			  XMM_RX_SIZE, xmm_rx_complete, pipe);
	ret = usb_submit_urb(pipe->rx_urb, GFP_KERNEL);
	if (ret)
		goto err_claim;

	set_bit(pipe_id, &rt->pipe_mask);
	if (pipe_id == XMM_PIPE_CMD) {
		if (!rt->hostwake_registered) {
			ret = tab3_xmm6262_register_hostwake_cb(
				xmm_runtime_hostwake_cb, rt);
			if (!ret)
				rt->hostwake_registered = true;
			else
				pr_warn("xmm6262-runtime: hostwake callback registration failed: %d; runtime PM stays forbidden\n", ret);
		}
		spin_lock_irqsave(&rt->tx_gate_lock, tx_flags);
		WRITE_ONCE(rt->runtime_connected, true);
		spin_unlock_irqrestore(&rt->tx_gate_lock, tx_flags);
		tab3_xmm6262_usb_connected_set(true);
		rt->reconnect_left = XMM_RECONNECT_TRIES;
		rt->pm_boot_deadline = jiffies +
			msecs_to_jiffies(XMM_PM_BOOT_MAX_WAIT_MS);
		rt->pm_boot_grace_started = false;
		tab3_xmm6262_set_state(TAB3_XMM_ONLINE);
		tab3_xmm6262_handoff_trace_event(
			TAB3_XMM_HANDOFF_RUNTIME_CMD_ONLINE,
			((u32)XMM_RUNTIME_VID << 16) | XMM_RUNTIME_PID,
			(u32)udev->devnum, (u32)rt->pipe_mask,
			(u32)pipe_id);
		/* Stock releases boot_wake when the runtime IPC channel exists. */
		tab3_xmm6262_boot_wake_release();
		xmm_wake_net_queues(rt);
		queue_delayed_work(system_wq, &rt->tx_work, 0);
		if (rt->hostwake_registered &&
		    !delayed_work_pending(&rt->pm_start_work))
			queue_delayed_work(system_wq, &rt->pm_start_work,
					   msecs_to_jiffies(500));
		pr_debug("xmm6262-runtime: RUNTIME_CMD_ONLINE vid=%04x pid=%04x pm_callback=%u\n",
			XMM_RUNTIME_VID, XMM_RUNTIME_PID,
			rt->hostwake_registered);
	}
	mutex_unlock(&rt->lock);
	return 0;

err_claim:
	usb_set_intfdata(intf, NULL);
	usb_set_intfdata(data_intf, NULL);
	usb_driver_release_interface(to_usb_driver(intf->dev.driver), data_intf);
err_urb:
	usb_free_urb(pipe->rx_urb);
	pipe->rx_urb = NULL;
err_buf:
	kfree(pipe->rx_buf);
	pipe->rx_buf = NULL;
err_pipe:
	pipe->present = false;
	pipe->ctrl_intf = NULL;
	pipe->data_intf = NULL;
	if (!rt->pipe_mask && rt->udev) {
		if (pm_was_auto)
			pm_runtime_allow(&rt->udev->dev);
		usb_put_dev(rt->udev);
		rt->udev = NULL;
	}
	mutex_unlock(&rt->lock);
	return ret;
}

static void xmm_runtime_disconnect(struct usb_interface *intf)
{
	struct xmm_pipe *pipe = usb_get_intfdata(intf);
	struct xmm_runtime *rt;
	struct usb_interface *data_intf;
	struct usb_device *old_udev = NULL;
	struct device *hdev = NULL;
	bool was_connected;
	bool ctrl;
	unsigned long tx_flags;
	int i;

	if (!pipe)
		return;
	rt = pipe->rt;
	ctrl = intf == pipe->ctrl_intf;
	usb_set_intfdata(intf, NULL);
	if (!ctrl)
		return;

	mutex_lock(&rt->lock);
	if (!pipe->present) {
		mutex_unlock(&rt->lock);
		return;
	}
	pipe->present = false;
	pipe->suspended = true;
	clear_bit(pipe->id, &rt->pipe_mask);
	data_intf = pipe->data_intf;
	was_connected = rt->runtime_connected;
	if (was_connected && rt->udev && rt->udev->bus->root_hub->dev.parent)
		hdev = get_device(rt->udev->bus->root_hub->dev.parent);
	if (was_connected) {
		spin_lock_irqsave(&rt->tx_gate_lock, tx_flags);
		WRITE_ONCE(rt->runtime_connected, false);
		spin_unlock_irqrestore(&rt->tx_gate_lock, tx_flags);
		tab3_xmm6262_usb_connected_set(false);
		rt->pm_started = false;
	}
	if (!rt->pipe_mask && rt->udev) {
		old_udev = rt->udev;
		rt->udev = NULL;
	}
	mutex_unlock(&rt->lock);

	/*
	 * Drain both resubmission paths before killing the last RX URB.
	 * Otherwise a worker already inside usb_submit_urb() can race
	 * with usb_kill_urb() and leave an unexpected live request.
	 */
	cancel_delayed_work_sync(&pipe->rx_submit_work);
	cancel_delayed_work_sync(&pipe->rx_retry_work);
	usb_kill_urb(pipe->rx_urb);
	cancel_delayed_work_sync(&pipe->rx_work);
	skb_queue_purge(&pipe->rx_queue);
	xmm_parser_reset(&pipe->parser, pipe->parser.header_len);
	if (data_intf) {
		usb_set_intfdata(data_intf, NULL);
		usb_driver_release_interface(to_usb_driver(intf->dev.driver), data_intf);
	}
	usb_free_urb(pipe->rx_urb);
	kfree(pipe->rx_buf);
	pipe->rx_urb = NULL;
	pipe->rx_buf = NULL;
	pipe->ctrl_intf = NULL;
	pipe->data_intf = NULL;
	if (was_connected) {
		/* Stock holds boot_wake across every HSIC disconnect/reconnect. */
		tab3_xmm6262_boot_wake_hold();
		xmm_stop_net_queues(rt);
		cancel_delayed_work_sync(&rt->pm_start_work);
		cancel_delayed_work_sync(&rt->tx_work);
		usb_kill_anchored_urbs(&rt->tx_anchor);
		/*
		 * Stock HSIC purges FMT/RAW TX queues when entering a new
		 * boot session. Keeping pending IPC across this disconnect
		 * would replay an old SMS or call request on reconnect.
		 */
		skb_queue_purge(&rt->fmt_tx_queue);
		skb_queue_purge(&rt->raw_tx_queue);
		if (hdev)
			pm_runtime_forbid(hdev);
		for (i = 0; i < ARRAY_SIZE(rt->fmt_multi); i++) {
			kfree_skb(rt->fmt_multi[i]);
			rt->fmt_multi[i] = NULL;
		}
		if (tab3_xmm6262_get_state() == TAB3_XMM_ONLINE) {
			mutex_lock(&rt->lock);
			rt->reconnect_left = XMM_RECONNECT_TRIES;
			mutex_unlock(&rt->lock);
			queue_delayed_work(system_wq, &rt->reconnect_work,
					   msecs_to_jiffies(500));
		}
	}
	if (hdev)
		put_device(hdev);
	if (old_udev)
		usb_put_dev(old_udev);
}

static int xmm_runtime_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct xmm_pipe *pipe = usb_get_intfdata(intf);

	if (!pipe || intf != pipe->ctrl_intf)
		return 0;
	WRITE_ONCE(pipe->suspended, true);
	if (pipe->id == XMM_PIPE_FMT)
		atomic64_inc(&pipe->rt->fmt_pm_suspend);
	/*
	 * Quiesce submit/copy retry workers before killing the active URB.
	 * Otherwise a running submit retry can race with usb_kill_urb()
	 * and arm a new receive transfer as the interface suspends.
	 */
	cancel_delayed_work_sync(&pipe->rx_submit_work);
	cancel_delayed_work_sync(&pipe->rx_retry_work);
	usb_kill_urb(pipe->rx_urb);
	return 0;
}

static int xmm_runtime_resume(struct usb_interface *intf)
{
	struct xmm_pipe *pipe = usb_get_intfdata(intf);

	if (!pipe || intf != pipe->ctrl_intf)
		return 0;
	pipe->suspended = false;
	if (pipe->id == XMM_PIPE_FMT)
		atomic64_inc(&pipe->rt->fmt_pm_resume);
	return xmm_rearm_rx(pipe, GFP_NOIO);
}

static int xmm_runtime_reset_resume(struct usb_interface *intf)
{
	return xmm_runtime_resume(intf);
}

static const struct usb_device_id xmm_runtime_ids[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(XMM_RUNTIME_VID, XMM_RUNTIME_PID,
		USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM, 1) },
	{ }
};
MODULE_DEVICE_TABLE(usb, xmm_runtime_ids);

static struct usb_driver xmm_runtime_driver = {
	.name = "samsung_tab3_xmm6262_runtime",
	.probe = xmm_runtime_probe,
	.disconnect = xmm_runtime_disconnect,
	.suspend = xmm_runtime_suspend,
	.resume = xmm_runtime_resume,
	.reset_resume = xmm_runtime_reset_resume,
	.id_table = xmm_runtime_ids,
	.supports_autosuspend = 1,
};

static int xmm_register_io(struct xmm_runtime *rt, int index)
{
	struct xmm_io *io = &rt->io[index];
	struct xmm_net_priv *priv;
	int ret;

	io->rt = rt;
	io->name = xmm_io_desc[index].name;
	io->id = xmm_io_desc[index].id;
	io->format = xmm_io_desc[index].format;
	io->type = xmm_io_desc[index].type;
	skb_queue_head_init(&io->rx_queue);
	init_waitqueue_head(&io->wait);
	atomic_set(&io->opened, 0);
	if (index == XMM_IOD_FMT)
		io->waketime_ms = 500;
	else if (index == XMM_IOD_RFS)
		io->waketime_ms = 3000;
	else if (index == XMM_IOD_MULTIPDP)
		io->waketime_ms = 6000;

	if (io->type == XMM_IO_NET) {
		io->ndev = alloc_netdev(sizeof(*priv), io->name, NET_NAME_UNKNOWN,
					 xmm_net_setup);
		if (!io->ndev)
			return -ENOMEM;
		priv = netdev_priv(io->ndev);
		priv->io = io;
		ret = register_netdev(io->ndev);
		if (ret) {
			free_netdev(io->ndev);
			io->ndev = NULL;
			return ret;
		}
		return 0;
	}

	io->miscdev.minor = MISC_DYNAMIC_MINOR;
	io->miscdev.name = io->name;
	io->miscdev.fops = &xmm_misc_fops;
	ret = misc_register(&io->miscdev);
	if (ret)
		return ret;
	device_init_wakeup(io->miscdev.this_device, true);
	if (index == XMM_IOD_FMT) {
		ret = device_create_file(io->miscdev.this_device, &dev_attr_fmt_stats);
		if (ret)
			goto err_misc;
	}
	if (io->type == XMM_IO_DUMMY) {
		ret = device_create_file(io->miscdev.this_device, &dev_attr_waketime);
		if (ret)
			goto err_misc;
		ret = device_create_file(io->miscdev.this_device, &dev_attr_loopback);
		if (ret) {
			device_remove_file(io->miscdev.this_device, &dev_attr_waketime);
			goto err_misc;
		}
	}
	return 0;

err_misc:
	misc_deregister(&io->miscdev);
	return ret;
}

static void xmm_unregister_io(struct xmm_io *io)
{
	if (io->type == XMM_IO_NET) {
		if (io->ndev) {
			unregister_netdev(io->ndev);
			free_netdev(io->ndev);
			io->ndev = NULL;
		}
	} else {
		if (io->format == XMM_IO_FMT && io->miscdev.this_device)
			device_remove_file(io->miscdev.this_device, &dev_attr_fmt_stats);
		if (io->type == XMM_IO_DUMMY && io->miscdev.this_device) {
			device_remove_file(io->miscdev.this_device, &dev_attr_loopback);
			device_remove_file(io->miscdev.this_device, &dev_attr_waketime);
		}
		misc_deregister(&io->miscdev);
	}
	skb_queue_purge(&io->rx_queue);
}

static int __init xmm_runtime_init(void)
{
	struct xmm_runtime *rt;
	int i;
	int ret;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return -ENOMEM;

	mutex_init(&rt->lock);
	spin_lock_init(&rt->tx_gate_lock);
	atomic64_set(&rt->fmt_rx_urbs, 0);
	atomic64_set(&rt->fmt_rx_bytes, 0);
	atomic64_set(&rt->fmt_rx_urb_errors, 0);
	atomic64_set(&rt->fmt_rx_oom_drops, 0);
	atomic64_set(&rt->fmt_rx_submit_retries, 0);
	atomic64_set(&rt->fmt_rx_submit_errors, 0);
	atomic64_set(&rt->fmt_rx_frames, 0);
	atomic64_set(&rt->fmt_parse_errors, 0);
	atomic64_set(&rt->fmt_tx_ok, 0);
	atomic64_set(&rt->fmt_tx_errors, 0);
	atomic64_set(&rt->fmt_pm_suspend, 0);
	atomic64_set(&rt->fmt_pm_resume, 0);
	atomic64_set(&rt->hostwake_active, 0);
	atomic64_set(&rt->hostwake_idle, 0);
	atomic64_set(&rt->hostwake_pm_ok, 0);
	atomic64_set(&rt->hostwake_pm_err, 0);
	atomic64_set(&rt->pm_boot_fmt_waits, 0);
	atomic64_set(&rt->pm_boot_grace_runs, 0);
	atomic64_set(&rt->pm_boot_deadline_expired, 0);
	skb_queue_head_init(&rt->fmt_tx_queue);
	skb_queue_head_init(&rt->raw_tx_queue);
	init_waitqueue_head(&rt->raw_wait);
	init_usb_anchor(&rt->tx_anchor);
	INIT_DELAYED_WORK(&rt->tx_work, xmm_tx_work);
	INIT_DELAYED_WORK(&rt->pm_start_work, xmm_pm_start_work);
	INIT_DELAYED_WORK(&rt->reconnect_work, xmm_reconnect_work);

	for (i = 0; i < XMM_PIPE_COUNT; i++) {
		rt->pipe[i].rt = rt;
		rt->pipe[i].id = i;
		skb_queue_head_init(&rt->pipe[i].rx_queue);
		INIT_DELAYED_WORK(&rt->pipe[i].rx_work, xmm_rx_work);
		INIT_DELAYED_WORK(&rt->pipe[i].rx_retry_work, xmm_rx_retry_work);
		INIT_DELAYED_WORK(&rt->pipe[i].rx_submit_work,
				  xmm_rx_submit_retry_work);
		xmm_parser_reset(&rt->pipe[i].parser,
			i == XMM_PIPE_FMT ? 3 : 6);
	}

	for (i = 0; i < XMM_IOD_COUNT; i++) {
		ret = xmm_register_io(rt, i);
		if (ret)
			goto err_io;
	}

	xmm_rt = rt;
	ret = usb_register(&xmm_runtime_driver);
	if (ret)
		goto err_io;
	return 0;

err_io:
	while (--i >= 0)
		xmm_unregister_io(&rt->io[i]);
	for (i = 0; i < XMM_PIPE_COUNT; i++)
		xmm_parser_reset(&rt->pipe[i].parser, rt->pipe[i].parser.header_len);
	xmm_rt = NULL;
	kfree(rt);
	return ret;
}

static void __exit xmm_runtime_exit(void)
{
	struct xmm_runtime *rt = xmm_rt;
	int i;

	if (!rt)
		return;
	if (rt->hostwake_registered) {
		tab3_xmm6262_unregister_hostwake_cb(xmm_runtime_hostwake_cb, rt);
		rt->hostwake_registered = false;
	}
	usb_deregister(&xmm_runtime_driver);
	cancel_delayed_work_sync(&rt->reconnect_work);
	cancel_delayed_work_sync(&rt->pm_start_work);
	cancel_delayed_work_sync(&rt->tx_work);
	usb_kill_anchored_urbs(&rt->tx_anchor);
	for (i = 0; i < XMM_PIPE_COUNT; i++) {
		cancel_delayed_work_sync(&rt->pipe[i].rx_submit_work);
		cancel_delayed_work_sync(&rt->pipe[i].rx_retry_work);
		cancel_delayed_work_sync(&rt->pipe[i].rx_work);
		skb_queue_purge(&rt->pipe[i].rx_queue);
		xmm_parser_reset(&rt->pipe[i].parser, rt->pipe[i].parser.header_len);
	}
	for (i = XMM_IOD_COUNT - 1; i >= 0; i--)
		xmm_unregister_io(&rt->io[i]);
	for (i = 0; i < ARRAY_SIZE(rt->fmt_multi); i++)
		kfree_skb(rt->fmt_multi[i]);
	skb_queue_purge(&rt->fmt_tx_queue);
	skb_queue_purge(&rt->raw_tx_queue);
	xmm_rt = NULL;
	kfree(rt);
}

module_init(xmm_runtime_init);
module_exit(xmm_runtime_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung SM-T311 XMM6262 runtime HSIC/SIPC4 transport");
