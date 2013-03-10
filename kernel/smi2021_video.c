/*******************************************************************************
 * smi2021_video.c                                                             *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCAP                                            *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This file is part of SMI2021x
 * http://code.google.com/p/easycap-somagic-linux/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#include "smi2021.h"

static void print_usb_err(struct smi2021_dev *dev, int packet, int status)
{
	char *errmsg;

	switch(status) {
	case -ENOENT:
		errmsg = "unlinked synchronuously";
		break;
	case -ECONNRESET:
		errmsg = "unlinked asynchronuously";
		break;
	case -ENOSR:
		errmsg = "Buffer error (overrun)";
		break;
	case -EPIPE:
		errmsg = "Stalled (device not responding)";
		break;
	case -EOVERFLOW:
		errmsg = "Babble (bad cable?)";
		break;
	case -EPROTO:
		errmsg = "Bit-stuff error (bad cable?)";
		break;
	case -EILSEQ:
		errmsg = "CRC/Timeout (could be anything)";
		break;
	case -ETIME:
		errmsg = "Device does not respond";
		break;
	default:
		errmsg = "Unknown";
	}

	if (packet < 0) {
		printk_ratelimited(KERN_WARNING "Urb status %d [%s]\n",
					status, errmsg);
	} else {
		printk_ratelimited(KERN_INFO "URB packet %d, status %d [%s]\n",
					packet, status, errmsg);
	}
}

static struct smi2021_buffer *smi2021_next_buffer(struct smi2021_dev *dev)
{
	struct smi2021_buffer *buf = NULL;
	unsigned long flags = 0;

	BUG_ON(dev->isoc_ctl.buf);

	spin_lock_irqsave(&dev->buf_lock, flags);
	if (!list_empty(&dev->avail_bufs)) {
		buf = list_first_entry(&dev->avail_bufs, struct smi2021_buffer,
									list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&dev->buf_lock, flags);

	return buf;
}

static void smi2021_buffer_done(struct smi2021_dev *dev)
{
	struct smi2021_buffer *buf = dev->isoc_ctl.buf;

	dev->buf_count++;

	buf->vb.v4l2_buf.sequence = dev->buf_count >> 1;
	buf->vb.v4l2_buf.field = V4L2_FIELD_INTERLACED;
	buf->vb.v4l2_buf.bytesused = buf->pos;
	do_gettimeofday(&buf->vb.v4l2_buf.timestamp);

	vb2_set_plane_payload(&buf->vb, 0, buf->pos);
	vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);

	dev->isoc_ctl.buf = NULL;
}

static void copy_video(struct smi2021_dev *dev, u8 p)
{
	struct smi2021_buffer *buf = dev->isoc_ctl.buf;

	int lines_per_field = dev->height / 2;
	int line = 0;
	int pos_in_line = 0;
	unsigned int offset = 0;
	u8 *dst;

	if (buf == NULL) {
		return;
	}
	
	if (buf->in_blank) {
		return;
	}

	if (buf->pos >= buf->length) {
		if (buf->second_field == 0) {
			/* We are probably trying to capture from
			 * a unconnected input
			 */
			smi2021_buffer_done(dev);
		} else {
			printk_ratelimited(KERN_WARNING
			"Buffer overflow!, max: %d bytes, av_lines_found: %d, second_field: %d\n",
					buf->length, buf->trc_av, buf->second_field);
		}
		return;
	}
	
	pos_in_line = buf->pos % SMI2021_BYTES_PER_LINE;
	line = buf->pos / SMI2021_BYTES_PER_LINE;
	if (line >= lines_per_field) {
			line -= lines_per_field;
	}

	if (line != buf->trc_av - 1) {
		/* Keep video synchronized.
		 * The device will sometimes give us to many bytes
		 * for a line, before we get a new TRC.
		 * We just drop these bytes */
		return;
	}
	
	if (buf->second_field) {
		offset += SMI2021_BYTES_PER_LINE;
	}

	offset += (SMI2021_BYTES_PER_LINE * line * 2) + pos_in_line;

	/* Will this ever happen? */
	if (offset >= buf->length) {
		printk_ratelimited(KERN_INFO
		"Offset calculation error, field: %d, line: %d, pos_in_line: %d\n",
			buf->second_field, line, pos_in_line);
		return;
	}

	dst = buf->mem + offset;
	*dst = p;
	buf->pos++;
}

#define is_sav(trc)						\
	((trc & SMI2021_TRC_EAV) == 0x00)
#define is_field2(trc)						\
	((trc & SMI2021_TRC_FIELD_2) == SMI2021_TRC_FIELD_2)
#define is_active_video(trc)					\
	((trc & SMI2021_TRC_VBI) == 0x00)
/*
 * Parse the TRC.
 * Grab a new buffer from the queue if don't have one
 * and we are recieving the start of a video frame.
 *
 * Mark video buffers as done if we have one full frame.
 */
static void parse_trc(struct smi2021_dev *dev, u8 trc)
{
	struct smi2021_buffer *buf = dev->isoc_ctl.buf;
	int lines_per_field = dev->height / 2;
	int line = 0;

	if (buf == NULL) {
		if (!is_sav(trc)) {
			return;
		}

		if (!is_active_video(trc)) {
			return;
		}

		if (is_field2(trc)) {
			return;
		}

		buf = smi2021_next_buffer(dev);
		if (buf == NULL) {
			return;
		}

		dev->isoc_ctl.buf = buf;
	}

	if (is_sav(trc)) {
		/* Start of VBI or ACTIVE VIDEO */
		if (is_active_video(trc)) {
			buf->in_blank = false;
			buf->trc_av++;
		} else {
			/* VBI */	
			buf->in_blank = true;
		}

		if (!buf->second_field && is_field2(trc)) {
			line = buf->pos / SMI2021_BYTES_PER_LINE;
			if (line < lines_per_field) {
				goto buf_done;
			}
			buf->second_field = true;
			buf->trc_av = 0;
		}

		if (buf->second_field && !is_field2(trc)) {
			goto buf_done;
		}

	} else {
		/* End of VBI or ACTIVE VIDEO */
		buf->in_blank = true;
	}

	return;

buf_done:
	smi2021_buffer_done(dev);
}

/*
 * Scan the saa7113 Active video data.
 * This data is:
 * 	4 bytes header (0xff 0x00 0x00 [TRC/SAV])
 * 	1440 bytes of UYUV Video data
 * 	4 bytes footer (0xff 0x00 0x00 [TRC/EAV])
 *
 * TRC = Time Reference Code.
 * SAV = Start Active Video.
 * EAV = End Active Video.
 * This is described in the saa7113 datasheet.
 */
static void parse_video(struct smi2021_dev *dev, u8 *p, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		switch(dev->sync_state) {
		case HSYNC:
			if (p[i] == 0xff)
				dev->sync_state = SYNCZ1;
			else
				copy_video(dev, p[i]);
			break;
		case SYNCZ1:
			if (p[i] == 0x00) {
				dev->sync_state = SYNCZ2;
			} else {
				dev->sync_state = HSYNC;
				copy_video(dev, 0xff);
				copy_video(dev, p[i]);
			}
			break;
		case SYNCZ2:
			if (p[i] == 0x00) {
				dev->sync_state = TRC;
			} else {
				dev->sync_state = HSYNC;
				copy_video(dev, 0xff);
				copy_video(dev, 0x00);
				copy_video(dev, p[i]);
			}
			break;
		case TRC:
			dev->sync_state = HSYNC;
			parse_trc(dev, p[i]);
			break;
		}
	}

}
/*
 *
 * The device delivers data in chunks of 0x400 bytes.
 * The four first bytes is a magic header to identify the chunks.
 *	0xaa 0xaa 0x00 0x00 = saa7113 Active Video Data
 *	0xaa 0xaa 0x00 0x01 = PCM - 24Bit 2 Channel audio data
 */
static void process_packet(struct smi2021_dev *dev, u8 *p, int len)
{
	int i;
	u32 *header;

	if (len % 0x400 != 0) {
		printk_ratelimited(KERN_INFO "smi2021::%s: len: %d\n",
				__func__, len);
		return;
	}

	for (i = 0; i < len; i += 0x400) {
		header = (u32 *)(p + i);
		switch(__cpu_to_be32(*header)) {
		case 0xaaaa0000: {
			parse_video(dev, p+i+4, 0x400-4);
			break;
		}
		case 0xaaaa0001: {
			smi2021_audio(dev, p+i+4, 0x400-4);
			break;
		}
		default: {
			/* Nothing */
		}
		}
	}
}

/*
 * Interrupt called by URB callback
 */
static void smi2021_isoc_isr(struct urb *urb)
{
	int i, rc, status, len;
	struct smi2021_dev *dev = urb->context;
	u8 *p;

	switch(urb->status) {
	case 0:
		break;
	case -ECONNRESET: /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		/* uvc driver frees the queue here */
		return;
	default:
		smi2021_err("urb error! status %d\n", urb->status);
		return;
	}

	if (urb->status < 0) {
		print_usb_err(dev, -1, status);
	}

	if (dev == NULL) {
		smi2021_warn("called with null device\n");
		return;	
	}

	for (i = 0; i < urb->number_of_packets; i++) {

		status = urb->iso_frame_desc[i].status;
		if (status == -18) {
			/* This seems to happen when the device
			 * trying to stream from an unconnected input
			 * */
			continue;
		}

		if (status < 0) {
			print_usb_err(dev, i, status);
			continue;
		}

		p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		len = urb->iso_frame_desc[i].actual_length;
		process_packet(dev, p, len);
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc) {
		smi2021_err("urb re-submit failed (%d)\n", rc);
	}
}

/*
 * Cancel urbs
 * This function can not be called in atomic context
 */
void smi2021_cancel_isoc(struct smi2021_dev *dev)
{
	int i, num_bufs = dev->isoc_ctl.num_bufs;
	if (!num_bufs) {
		return;
	}

	smi2021_dbg("killing %d urbs...\n", num_bufs);

	for (i = 0; i < num_bufs; i++) {
		usb_kill_urb(dev->isoc_ctl.urb[i]);
	}

	smi2021_dbg("all urbs killed\n");

}

/*
 * Releases urb and transfer buffers
 * Obviously, associated urb must be killed before releasing it
 */
void smi2021_free_isoc(struct smi2021_dev *dev)
{
	struct urb *urb;
	int i, num_bufs = dev->isoc_ctl.num_bufs;

	smi2021_dbg("freeing %d urb buffers...\n", num_bufs);

	for (i = 0; i < num_bufs; i++) {
		urb = dev->isoc_ctl.urb[i];
		if (urb) {
			if (dev->isoc_ctl.transfer_buffer[i]) {
#ifndef CONFIG_DMA_NONCOHERENT
				usb_free_coherent(dev->udev,
					urb->transfer_buffer_length,
					dev->isoc_ctl.transfer_buffer[i],
					urb->transfer_dma);
#else
				kfree(dev->isoc_ctl.transfer_buffer[i]);
#endif
			}
			usb_free_urb(urb);
			dev->isoc_ctl.urb[i] = NULL;
		}
		dev->isoc_ctl.transfer_buffer[i] = NULL;
	}

	kfree(dev->isoc_ctl.urb);
	kfree(dev->isoc_ctl.transfer_buffer);

	dev->isoc_ctl.urb = NULL;
	dev->isoc_ctl.transfer_buffer = NULL;
	dev->isoc_ctl.num_bufs = 0;

	smi2021_dbg("all urb buffers freed\n");
}

/*
 * Helper for canceling and freeing urbs
 * This function can not be called in atomic context
 */
void smi2021_uninit_isoc(struct smi2021_dev *dev)
{
	smi2021_cancel_isoc(dev);
	smi2021_free_isoc(dev);
}


int smi2021_alloc_isoc(struct smi2021_dev *dev)
{
	struct urb *urb;
	int i, j, k, sb_size, max_packets, num_bufs;

	if (dev->isoc_ctl.num_bufs) {
		smi2021_uninit_isoc(dev);
	}

	num_bufs = SMI2021_ISOC_BUFS;
	max_packets = SMI2021_ISOC_PACKETS; 
	sb_size = max_packets * SMI2021_MAX_PKT_SIZE;

	dev->isoc_ctl.buf = NULL;
	dev->isoc_ctl.max_pkt_size = SMI2021_MAX_PKT_SIZE;
	dev->isoc_ctl.urb = kzalloc(sizeof(void *) * num_bufs, GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		smi2021_err("out of memory for urb array\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kzalloc(sizeof(void *) * num_bufs,
							GFP_KERNEL);
	if (!dev->isoc_ctl.transfer_buffer) {
		smi2021_err("out of memory for usb transfer\n");
		kfree(dev->isoc_ctl.urb);
		return -ENOMEM;
	}

	for (i = 0; i < num_bufs; i++) {
		urb = usb_alloc_urb(max_packets, GFP_KERNEL);
		if (!urb) {
			smi2021_err("connot allocate urb[%d]\n", i);
			goto free_i_bufs;
		}
		dev->isoc_ctl.urb[i] = urb;
#ifndef CONFIG_DMA_NONCOHERENT
		dev->isoc_ctl.transfer_buffer[i] = usb_alloc_coherent(
					dev->udev, sb_size, GFP_KERNEL,
					&urb->transfer_dma);
#else
		dev->isoc_ctl.transfer_buffer[i] = kmalloc(sb_size,
								GFP_KERNEL);
#endif
		if (!dev->isoc_ctl.transfer_buffer[i]) {
			smi2021_err("cannot alloc %d bytes for tx[%d] buffer",
					sb_size, i);
			goto free_i_bufs;
		}
		/* Do not leak kernel data */
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

		urb->dev = dev->udev;
		urb->pipe = usb_rcvisocpipe(dev->udev, SMI2021_ISOC_EP);
		urb->transfer_buffer = dev->isoc_ctl.transfer_buffer[i];
		urb->transfer_buffer_length = sb_size;
		urb->complete = smi2021_isoc_isr;
		urb->context = dev;
		urb->interval = 1;
		urb->start_frame = 0;
		urb->number_of_packets = max_packets;
#ifndef CONFIG_DMA_NONCOHERENT
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
#else
		urb->transfer_flags = URB_ISO_ASAP;
#endif
		k = 0;
		for (j = 0; j < max_packets; j++) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length =
						dev->isoc_ctl.max_pkt_size;
			k += dev->isoc_ctl.max_pkt_size;
		}
	}
	smi2021_dbg("urbs allocated\n");
	dev->isoc_ctl.num_bufs = num_bufs;
	return 0;

free_i_bufs:
	dev->isoc_ctl.num_bufs = i+1;
	smi2021_free_isoc(dev);
	return -ENOMEM;
}
