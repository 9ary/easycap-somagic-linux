#include "somagic.h"
static inline
struct somagic_buffer *somagic_next_buffer(struct somagic_dev *dev)
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

static inline void somagic_buffer_done(struct somagic_dev *dev)
{
	struct somagic_buffer *buf = dev->isoc_ctl.buf;

	dev->buf_count++;

	buf->vb.v4l2_buf.sequence = dev->buf_count >> 1;
	buf->vb.v4l2_buf.field = V4L2_FIELD_INTERLACED;
	buf->vb.v4l2_buf.bytesused = buf->bytes_used;
	do_gettimeofday(&buf->vb.v4l2_buf.timestamp);

	vb2_set_plane_payload(&buf->vb, 0, buf->bytes_used);
	vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);

	dev->isoc_ctl.buf = NULL;
}

static inline 
struct somagic_buffer *parse_trc(struct somagic_dev *dev, u8 trc)
{
	struct somagic_buffer *buf = dev->isoc_ctl.buf;
	u8 start = SOMAGIC_TRC_EAV | SOMAGIC_TRC_VBI | SOMAGIC_TRC_FIELD_2;
	if (buf == NULL) {
		/* We are looking for first SAV for field 0 */
		if ((trc & start) == 0x00) {
			buf = somagic_next_buffer(dev);
			dev->isoc_ctl.buf = buf;
		}
		if (buf == NULL) {
			return NULL;
		}
		printk_ratelimited(KERN_INFO "new_buffer!\n");
	}

	if ((trc & SOMAGIC_TRC_EAV) == 0x00) {
		/* SAV */

		if (trc & SOMAGIC_TRC_VBI) {
			buf->in_blank = true;
		} else {
			buf->in_blank = false;
		}

		if (!buf->second_field &&
				(trc & SOMAGIC_TRC_FIELD_2) &&
				buf->vbi_lines) {
			buf->second_field = true;
			printk_ratelimited(KERN_INFO "found 2nd field after %d blanks\n",
						buf->vbi_lines);
		}

		if ((trc & SOMAGIC_TRC_FIELD_2) == 0x00 && buf->second_field) {
			printk_ratelimited(KERN_INFO "found 1st field after %d blanks\n",
						buf->vbi_lines);
			somagic_buffer_done(dev);
			buf = somagic_next_buffer(dev);
			if (buf == NULL) {
				return NULL;
			}
			dev->isoc_ctl.buf = buf;
		}

	} else {
		/* EAV */
		buf->in_blank = true;
		if (trc & SOMAGIC_TRC_VBI) {
			buf->vbi_lines++;
		}
	}


	return buf;

}

static inline void copy_video(struct somagic_dev *dev,
				struct somagic_buffer *buf, u8 p)
{
	u8 *dst;

	if (buf == NULL) {
		return;
	}
	
	if (buf->in_blank) {
		return;
	}

	if (buf->bytes_used >= buf->length) {
		printk_ratelimited(KERN_INFO "Buffer overflow!, max: %d bytes\n",
					buf->length);
		return;
	}

	dst = buf->mem;


/*
	if (buf->pos_in_line == bytes_per_line) {
		somagic_warn("Line-buffer overflow");
		return;
	}
	if (buf->line >= dev->height) {
		printk_ratelimited(KERN_INFO "Buffer overflow!, To many lines!\n");
		return;
	}
	if (buf->second_field) {
		dst += bytes_per_line; 
	}
*/
	dst += buf->pos;
	*dst = p;

	buf->bytes_used++;
	buf->pos++;
}

/*
 * Scan a chunk to find video data, and copy data to buffer
 */
static void parse_video_data(struct somagic_dev *dev, u8 **vptrs_v, int vptrs_c)
{
	int i, e, sync = 0;
	struct somagic_buffer *buf = NULL;
	u8 *p;

	for (i = 0; i < vptrs_c; i++) {
		p = vptrs_v[i];
		for (e = 4; e < 0x400; e++) {
			switch(sync) {
			case 0: {
				if (p[e] == 0xff) {
					sync++;	
				} else {
					copy_video(dev, buf, p[e]);
				}
				break;
			}
			case 1: {
				if (p[e] == 0x00) {
					sync++;
				} else {
					sync = 0;
					copy_video(dev, buf, 0xff);
					copy_video(dev, buf, p[e]);
				}
				break;
			}
			case 2: {
				if (p[e] == 0x00) {
					sync++;
				} else {
					sync = 0;
					copy_video(dev, buf, 0xff);
					copy_video(dev, buf, 0x00);
					copy_video(dev, buf, p[e]);
				}
				break;
			}
			case 3: {
				sync = 0;
				if (p[e] & SOMAGIC_TRC) {
					buf = parse_trc(dev, p[e]);
				} else {
					copy_video(dev, buf, 0xff);
					copy_video(dev, buf, 0x00);
					copy_video(dev, buf, 0x00);
					copy_video(dev, buf, p[e]);
					
				}

			}
			}
		}
	}

}

static void somagic_process_isoc(struct somagic_dev *dev, struct urb *urb)
{
	int i, j, status, len, vptrs_c = 0;
	u8 *p;
	u8 *vptrs_v[200];

	u32 *header;

	if (!dev) {
		somagic_warn("called with null device\n");
		return;	
	}

	if (urb->status < 0) {
		somagic_warn("Received urb with status: %d\n", urb->status);
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

		/*
 		 * The device deliver data in chunks of 0x400 (1024),
 		 * We check the first four bytes of the chunk
 		 * to see if it's video or audio data
 		 */
		if (len % 0x400) {
			printk_ratelimited(KERN_INFO "somagic::%s: len: %d\n",
					__func__, len);
			continue;
		}
		for (j = 0; j < len; j += 0x400) {
			header = (u32 *)(p + j);
			switch(__cpu_to_be32(*header)) {
			case 0xaaaa0000: {
				if (vptrs_c == 200) {
					printk_ratelimited(KERN_WARNING
						"somagic::%s: "
						"max video pointers reached\n",
						__func__);
					break;
				}
				vptrs_v[vptrs_c++] = p+j; /* +4; */
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
	parse_video_data(dev, vptrs_v, vptrs_c);
}

/*
 * Interrupt called by URB callback
 */
static void somagic_isoc_isr(struct urb *urb)
{
	int i, rc;
	struct somagic_dev *dev = urb->context;

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

	somagic_process_isoc(dev, urb);

	for (i = 0; i < urb->number_of_packets; i++) {
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
