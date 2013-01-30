#include "somagic.h"

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

	/* somagic_process_isoc(dev, urb); */

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
