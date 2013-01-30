/*******************************************************************************
 * somagic_v4l2.c                                                              *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011 Jon Arne Jørgensen
 *
 * This file is part of somagic_dc60
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
 */

#include "somagic.h"

static struct somagic_fmt format[] = {
	{
		.name = "16bpp YU2, 4:2:2, packed",
		.fourcc = V4L2_PIX_FMT_UYVY,
		.depth = 16,
	}
};

static int somagic_start_streaming(struct somagic_dev *dev)
{
	u8 data[2];
	int i, rc = 0;
	data[0] = 0x01;
	data[1] = 0x05;

	if (!dev->udev) {
		return -ENODEV;
	}


	if (mutex_lock_interruptible(&dev->v4l2_lock)) {
		return -ERESTARTSYS;
	}

	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 1);

	rc = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0x00),
			0x01, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x01, 0x00, data, sizeof(data), 1000);
	if (rc < 0) {
		somagic_err("Could not start device!\n");
		goto out_unlock;
	}
	
	/* It's mandatory to set alt interface before allocating isoc buffer */
	usb_set_interface(dev->udev, 0, 2);

	somagic_write_reg(dev, 0, 0x1740, 0x1d);

	if (!dev->isoc_ctl.num_bufs) {
		rc = somagic_alloc_isoc(dev);
		if (rc < 0) {
			goto out_stop_hw;
		}
	
	}

	/* submit urbs and enable IRQ */
	for (i = 0; i < dev->isoc_ctl.num_bufs; i++) {
		rc = usb_submit_urb(dev->isoc_ctl.urb[i], GFP_KERNEL);
		if (rc) {
			somagic_err("cannot submit urb[%d] (%d)\n", i, rc);
			goto out_uninit;
		}
	}

	
	mutex_unlock(&dev->v4l2_lock);
	return 0;

out_uninit:
	somagic_uninit_isoc(dev);
out_stop_hw:
	usb_set_interface(dev->udev, 0, 0);
	somagic_clear_queue(dev);

out_unlock:
	mutex_unlock(&dev->v4l2_lock);

	return rc;
}

/* Must be called with v4l2_lock hold */
static void somagic_stop_hw(struct somagic_dev *dev)
{
	int rc = 0;
	u8 data[] = { 0x01, 0x03 };

	if (!dev->udev) {
		return;
	}

	usb_set_interface(dev->udev, 0, 0);

	rc = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0x00),
			0x01, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x01, 0x00, data, sizeof(data), 1000);
	if (rc < 0) {
		somagic_err("Could not stop device!\n");
	}

	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 0);
	
}

static int somagic_stop_streaming(struct somagic_dev *dev)
{
	if (mutex_lock_interruptible(&dev->v4l2_lock)) {
		return -ERESTARTSYS;
	}

	somagic_cancel_isoc(dev);
	somagic_free_isoc(dev);
	somagic_stop_hw(dev);
	somagic_clear_queue(dev);

	somagic_dbg("Streaming stopped!\n");

	mutex_unlock(&dev->v4l2_lock);

	return 0;
}

static struct v4l2_file_operations somagic_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

/******************************************************************************/
/*                                                                            */
/*          Vidioc IOCTLS                                                     */
/*                                                                            */
/******************************************************************************/

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_fmtdesc *f)
{
	if (f->index != 0) {
		return -EINVAL;
	}

	strlcpy(f->description, format[f->index].name, sizeof(f->description));
	f->pixelformat = format[f->index].fourcc;

	return 0;
}

static int vidioc_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct somagic_dev *dev = video_drvdata(file);

	strcpy(cap->driver, "somagic_easycap_dc60");
	strcpy(cap->card, "somagic_easycap_dc60");
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct somagic_dev *dev = video_drvdata(file);

	f->fmt.pix.pixelformat = dev->fmt->fourcc;
	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.bytesperline = dev->width * 2;
	f->fmt.pix.sizeimage = dev->height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct somagic_dev *dev = video_drvdata(file);

	f->fmt.pix.pixelformat = dev->fmt->fourcc;
	f->fmt.pix.width = dev->width;
	f->fmt.pix.height = dev->height;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.bytesperline = dev->width * 2;
	f->fmt.pix.sizeimage = dev->height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct somagic_dev *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb_vidq;

	if (vb2_is_busy(q)) {
		return -EBUSY;
	}

	vidioc_try_fmt_vid_cap(file, priv, f);
	return 0;
}

static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct somagic_dev *dev = video_drvdata(file);

	/* TODO: Debug this, somagic cannot read i2c values! */
	v4l2_device_call_all(&dev->v4l2_dev, 0, video,querystd, norm);
	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct somagic_dev *dev = video_drvdata(file);

	*norm = dev->norm;
	return 0;	
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct somagic_dev *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb_vidq;

	if (vb2_is_busy(q)) {
		return -EBUSY;
	}

	if (!dev->udev) {
		return -ENODEV;
	}

	dev->norm = *norm;
	if (dev->norm & V4L2_STD_525_60) {
		dev->width = 720;
		dev->height = 480;
	} else if (dev->norm & V4L2_STD_625_50) {
		dev->width = 720;
		dev->height = 576;
	} else {
		printk(KERN_ERR "somagic::%s: Invalid standard\n", __func__);
		return -EINVAL;
	}

	/* somagic_set_std(dev); */
	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_std, dev->norm);
	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct somagic_dev *dev = video_drvdata(file);

	/* TODO: Remove hardcoded values */
	if (i->index > 1) {
		return -EINVAL;
	}

	sprintf(i->name, "Composite");
	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = dev->vdev.tvnorms;
	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct somagic_dev *dev = video_drvdata(file);
	*i = dev->ctl_input;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct somagic_dev *dev = video_drvdata(file);

	if (vb2_is_busy(&dev->vb_vidq)) {
		return -EBUSY;
	}

	/* TODO: REMOVE HARD HACK */
	if (i > 1) {
		return -EINVAL;
	}

	dev->ctl_input = i;
	/* somagic_select_input(dev); */

	return 0;
}

static int vidioc_g_chip_ident(struct file *file, void *priv,
			struct v4l2_dbg_chip_ident *chip)
{
	switch (chip->match.type) {
	case V4L2_CHIP_MATCH_HOST:
		chip->ident = V4L2_IDENT_NONE;
		chip->revision = 0;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops somagic_ioctl_ops = {
	.vidioc_querycap          = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_querystd          = vidioc_querystd,
	.vidioc_g_std             = vidioc_g_std,
	.vidioc_s_std             = vidioc_s_std,
	.vidioc_enum_input        = vidioc_enum_input,
	.vidioc_g_input           = vidioc_g_input,
	.vidioc_s_input           = vidioc_s_input,

	/* vb2 handle these */
	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,
	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,

	.vidioc_log_status        = v4l2_ctrl_log_status,
	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_g_chip_ident      = vidioc_g_chip_ident,

};

/******************************************************************************/
/*                                                                            */
/*          Videobuf2 operations                                              */
/*                                                                            */
/******************************************************************************/
static int queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *v4l2_fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct somagic_dev *dev = vb2_get_drv_priv(vq);
	unsigned long size;

	size = dev->width * dev->height * 2;

	*nbuffers = clamp_t(unsigned int, *nbuffers, 2, 4);

	/* Packed color format */
	*nplanes = 1;
	sizes[0] = size;

	printk(KERN_INFO "somagic::%s: buffer count %d, each %ld bytes\n",
				 __func__, *nbuffers, size);

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	unsigned long flags;
	struct somagic_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct somagic_buffer *buf = container_of(vb, struct somagic_buffer, vb);

	spin_lock_irqsave(&dev->buf_lock, flags);
	if (!dev->udev) {
		/*
 		 * If the device is disconnected return the buffer to userspace
 		 * directly. The next QBUF call will fail with -ENODEV.
 		 */
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	} else {
		buf->mem = vb2_plane_vaddr(vb, 0);
		buf->length = vb2_plane_size(vb, 0);
		buf->bytes_used = 0;
		buf->pos = 0;

		if (buf->length < 829440) {
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		} else {
			list_add_tail(&buf->list, &dev->avail_bufs);
		}
	}
	spin_unlock_irqrestore(&dev->buf_lock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct somagic_dev *dev = vb2_get_drv_priv(vq);
	return somagic_start_streaming(dev);
}

static int stop_streaming(struct vb2_queue *vq)
{
	struct somagic_dev *dev = vb2_get_drv_priv(vq);
	return somagic_stop_streaming(dev);
}

static struct vb2_ops somagic_video_qops = {
	.queue_setup			= queue_setup,
	.buf_queue				= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare			= vb2_ops_wait_prepare,
	.wait_finish			= vb2_ops_wait_finish,
};

static struct video_device v4l2_template = {
	.name = "easycap_somagic_dc60",
	.tvnorms = V4L2_STD_625_50,
	.fops = &somagic_fops,
	.ioctl_ops = &somagic_ioctl_ops,
	.release = video_device_release_empty,
};

void somagic_clear_queue(struct somagic_dev *dev)
{
	struct somagic_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&dev->buf_lock, flags);
	while(!list_empty(&dev->avail_bufs)) {
		buf = list_first_entry(&dev->avail_bufs,
			struct somagic_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}
	dev->isoc_ctl.buf = NULL;
	spin_unlock_irqrestore(&dev->buf_lock, flags);
}

int somagic_vb2_setup(struct somagic_dev *dev)
{
	int rc;
	struct vb2_queue *q;

	q = &dev->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_READ | VB2_MMAP | VB2_USERPTR;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct somagic_buffer);
	q->ops = &somagic_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;

	rc = vb2_queue_init(q);
	if (rc < 0) {
		return rc;
	}

	/* Initialize video dma queue */
	INIT_LIST_HEAD(&dev->avail_bufs);

	return 0;
}

int somagic_video_register(struct somagic_dev *dev)
{
	int rc;

	dev->vdev = v4l2_template;
	dev->vdev.debug = 0;
	dev->vdev.queue = &dev->vb_vidq;

	dev->vdev.lock = &dev->v4l2_lock;
	dev->vdev.queue->lock = &dev->vb_queue_lock;

	dev->vdev.v4l2_dev = &dev->v4l2_dev;
	set_bit(V4L2_FL_USE_FH_PRIO, &dev->vdev.flags);

	/* PAL is default */
	dev->norm = V4L2_STD_PAL;
	dev->width = 720;
	dev->height = 576;

	dev->fmt = &format[0];
	/* somagic_set_std(dev); */

	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_std, dev->norm);

	video_set_drvdata(&dev->vdev, dev);
	rc = video_register_device(&dev->vdev, VFL_TYPE_GRABBER, -1);
	if (rc < 0) {
		printk(KERN_ERR "somagic::%s: video_register_device failed(%d)\n",
				 __func__, rc);
		return rc;
	}

	v4l2_info(&dev->v4l2_dev, "V4L2 device registered as %s\n",
		video_device_node_name(&dev->vdev));

	return 0;
}
