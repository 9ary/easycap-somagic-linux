#include "somagic.h"
static struct somagic_buffer *somagic_next_buffer(struct somagic_dev *dev)
{
	struct somagic_buffer *buf = NULL;
	unsigned long flags = 0;

	BUG_ON(dev->isoc_ctl.buf);

	spin_lock_irqsave(&dev->buf_lock, flags);
	if (!list_empty(&dev->avail_bufs)) {
		buf = list_first_entry(&dev->avail_bufs, struct somagic_buffer,
									list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&dev->buf_lock, flags);

	return buf;
}

static void somagic_buffer_done(struct somagic_dev *dev)
{
	struct somagic_buffer *buf = dev->isoc_ctl.buf;

	dev->buf_count++;

	buf->vb.v4l2_buf.sequence = dev->buf_count >> 1;
	buf->vb.v4l2_buf.field = V4L2_FIELD_INTERLACED;
	buf->vb.v4l2_buf.bytesused = buf->pos;
	do_gettimeofday(&buf->vb.v4l2_buf.timestamp);

	vb2_set_plane_payload(&buf->vb, 0, buf->pos);
	vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);

	dev->isoc_ctl.buf = NULL;
}

static void copy_video(struct somagic_dev *dev, struct somagic_buffer *buf,
			u8 p)
{
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
		printk_ratelimited(KERN_INFO "Buffer overflow!, max: %d bytes\n",
					buf->length);
		return;
	}
	
	line = buf->pos / SOMAGIC_BYTES_PER_LINE;
	pos_in_line = buf->pos % SOMAGIC_BYTES_PER_LINE;
	
	if (buf->second_field) {
		offset += SOMAGIC_BYTES_PER_LINE;
		if (line >= lines_per_field)
			line -= lines_per_field;
	}

	offset += (SOMAGIC_BYTES_PER_LINE * line * 2) + pos_in_line;

	/* Will this ever happen? */
	if (offset >= buf->length) {
		printk_ratelimited(KERN_INFO "Buffer overflow!, field: %d, line: %d, pos_in_line: %d\n",
					buf->second_field, line, pos_in_line);
		return;
	}

	dst = buf->mem + offset;
	*dst = p;
	buf->pos++;
}

#define is_sav(trc)						\
	((trc & SOMAGIC_TRC_EAV) == 0x00)
#define is_field2(trc)						\
	((trc & SOMAGIC_TRC_FIELD_2) == SOMAGIC_TRC_FIELD_2)
#define is_vbi(trc)						\
	((trc & SOMAGIC_TRC_VBI) == SOMAGIC_TRC_VBI)
/*
 * Parse the TRC.
 * Grab a new buffer from the queue if don't have one
 * and we are recieving the start of a video frame.
 *
 * Mark video buffers as done if we have one full frame.
 */
static struct somagic_buffer *parse_trc(struct somagic_dev *dev, u8 trc)
{
	struct somagic_buffer *buf = dev->isoc_ctl.buf;
	int lines_per_field = dev->height / 2;
	int line = 0;

	if (buf == NULL) {
		if (!is_sav(trc)) {
			return NULL;
		}

		if (is_vbi(trc)) {
			return NULL;
		}

		if (is_field2(trc)) {
			return NULL;
		}

		buf = somagic_next_buffer(dev);
		if (buf == NULL) {
			return NULL;
		}

		dev->isoc_ctl.buf = buf;
	}

	if (is_sav(trc)) {
		if (!is_vbi(trc)) {
			buf->in_blank = false;
		} else {
			buf->in_blank = true;
		}

		if (!buf->second_field && is_field2(trc)) {
			line = buf->pos / SOMAGIC_BYTES_PER_LINE;
			if (line < lines_per_field) {
				goto buf_done;
			}
			buf->second_field = true;
		}

		if (buf->second_field && !is_field2(trc)) {
			goto buf_done;
		}

	} else {
		buf->in_blank = true;
	}

	return buf;

buf_done:
	somagic_buffer_done(dev);
	return NULL;
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
static void parse_video(struct somagic_dev *dev, u8 *p, int len)
{
	struct somagic_buffer *buf = dev->isoc_ctl.buf;
	enum {
		VIDEO_DATA,
		ALMOST_TRC_1,
		ALMOST_TRC_2,
		TRC
		
	} trc = VIDEO_DATA;
	int i;

	for (i = 0; i < len; i++) {
		switch(trc) {
		case VIDEO_DATA: {
			if (p[i] == 0xff)
				trc = ALMOST_TRC_1;
			else
				copy_video(dev, buf, p[i]);
			break;
		}
		case ALMOST_TRC_1: {
			if (p[i] == 0x00) {
				trc = ALMOST_TRC_2;
			} else {
				trc = VIDEO_DATA;
				copy_video(dev, buf, 0xff);
				copy_video(dev, buf, p[i]);
			}
			break;
		}
		case ALMOST_TRC_2: {
			if (p[i] == 0x00) {
				trc = TRC;
			} else {
				trc = VIDEO_DATA;
				copy_video(dev, buf, 0xff);
				copy_video(dev, buf, 0x00);
				copy_video(dev, buf, p[i]);
			}
			break;
		}
		case TRC: {
			buf = parse_trc(dev, p[i]);
			trc = VIDEO_DATA;
			break;
		}
		default: {
			/* Just for safety */
			trc = VIDEO_DATA;
		}
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
static void process_packet(struct somagic_dev *dev, u8 *p, int len)
{
	int i;
	u32 *header;

	if (len % 0x400 != 0) {
		printk_ratelimited(KERN_INFO "somagic::%s: len: %d\n",
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
			/*printk_ratelimited("%x\n", __cpu_to_be32(*test));*/
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
static void somagic_isoc_isr(struct urb *urb)
{
	int i, rc, status, len;
	struct somagic_dev *dev = urb->context;
	u8 *p;

	switch(urb->status) {
	case 0:
		break;
	case -ECONNRESET: /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		somagic_err("urb error! status %d\n", urb->status);
		return;
	}

	if (dev == NULL) {
		somagic_warn("called with null device\n");
		return;	
	}

	for (i = 0; i < urb->number_of_packets; i++) {

		status = urb->iso_frame_desc[i].status;
		if (status < 0) {
			printk_ratelimited(KERN_INFO "somagic::%s: "
					"Received urb with status: %d\n",
					__func__, status);
			continue;
		}

		p = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		len = urb->iso_frame_desc[i].actual_length;
		process_packet(dev, p, len);

		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc) {
		somagic_err("urb re-submit failed (%d)\n", rc);
	}
}

/*
 * Cancel urbs
 * This function can not be called in atomic context
 */
void somagic_cancel_isoc(struct somagic_dev *dev)
{
	int i, num_bufs = dev->isoc_ctl.num_bufs;
	if (!num_bufs) {
		return;
	}

	somagic_dbg("killing %d urbs...\n", num_bufs);

	for (i = 0; i < num_bufs; i++) {
		usb_kill_urb(dev->isoc_ctl.urb[i]);
	}

	somagic_dbg("all urbs killed\n");
}

/*
 * Releases urb and transfer buffers
 * Obviously, associated urb must be killed before releasing it
 */
void somagic_free_isoc(struct somagic_dev *dev)
{
	struct urb *urb;
	int i, num_bufs = dev->isoc_ctl.num_bufs;

	somagic_dbg("freeing %d urb buffers...\n", num_bufs);

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

	somagic_dbg("all urb buffers freed\n");
}

/*
 * Helper for canceling and freeing urbs
 * This function can not be called in atomic context
 */
void somagic_uninit_isoc(struct somagic_dev *dev)
{
	somagic_cancel_isoc(dev);
	somagic_free_isoc(dev);
}


int somagic_alloc_isoc(struct somagic_dev *dev)
{
	struct urb *urb;
	int i, j, k, sb_size, max_packets, num_bufs;

	if (dev->isoc_ctl.num_bufs) {
		somagic_uninit_isoc(dev);
	}

	num_bufs = SOMAGIC_NUM_BUFS;
	max_packets = SOMAGIC_NUM_PACKETS; 
	sb_size = max_packets * SOMAGIC_MAX_PKT_SIZE;

	dev->isoc_ctl.buf = NULL;
	dev->isoc_ctl.max_pkt_size = SOMAGIC_MAX_PKT_SIZE;
	dev->isoc_ctl.urb = kzalloc(sizeof(void *) * num_bufs, GFP_KERNEL);
	if (!dev->isoc_ctl.urb) {
		somagic_err("out of memory for urb array\n");
		return -ENOMEM;
	}

	dev->isoc_ctl.transfer_buffer = kzalloc(sizeof(void *) * num_bufs,
							GFP_KERNEL);
	if (!dev->isoc_ctl.transfer_buffer) {
		somagic_err("out of memory for usb transfer\n");
		kfree(dev->isoc_ctl.urb);
		return -ENOMEM;
	}

	for (i = 0; i < num_bufs; i++) {
		urb = usb_alloc_urb(max_packets, GFP_KERNEL);
		if (!urb) {
			somagic_err("connot allocate urb[%d]\n", i);
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
			somagic_err("cannot alloc %d bytes for tx[%d] buffer",
					sb_size, i);
			goto free_i_bufs;
		}
		/* Do not leak kernel data */
		memset(dev->isoc_ctl.transfer_buffer[i], 0, sb_size);

		urb->dev = dev->udev;
		urb->pipe = usb_rcvisocpipe(dev->udev, SOMAGIC_ISOC_EP);
		urb->transfer_buffer = dev->isoc_ctl.transfer_buffer[i];
		urb->transfer_buffer_length = sb_size;
		urb->complete = somagic_isoc_isr;
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
	somagic_dbg("urbs allocated\n");
	dev->isoc_ctl.num_bufs = num_bufs;
	return 0;

free_i_bufs:
	dev->isoc_ctl.num_bufs = i+1;
	somagic_free_isoc(dev);
	return -ENOMEM;
}
