/*
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
#include "somagic_capture_device.h"
#include "somagic.h"

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

//static int somagic_nr;

static int video_nr = -1;

/* Showing parameters under SYSFS */
module_param(video_nr, int, 0444);

// TODO: Find the right include for this macro
//MODULE_PARAM_DESC(video_nr, "Set video device number (/dev/videoX). Default: -1(autodetect)");

/*****************************************************************************/
/* SYSFS Code																																 */
/* Copied from media/video/usbvision module					 												 */

static ssize_t show_version(struct device *cd,
							struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", SOMAGIC_DRIVER_VERSION);
}

static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static void somagic_create_sysfs(struct video_device *vdev)
{
	int res;
	if (!vdev) {
		return;
	}

	do {
		res = device_create_file(&vdev->dev, &dev_attr_version);
		if (res < 0) {
			break;
		}
		if (res >= 0) {
			return;
		}
	} while(0);

	dev_err(&vdev->dev, "%s error: %d\n", __func__, res);
}

static void somagic_remove_sysfs(struct video_device *vdev)
{
	if (vdev) {
		device_remove_file(&vdev->dev, &dev_attr_version);
	}
}

static int somagic_v4l2_open(struct file *file)
{
	//struct usb_somagic *somagic = video_drvdata(file);
	return -EBUSY;
}

static int somagic_v4l2_close(struct file *file)
{
	//struct usb_somagic *somagic = video_drvdata(file);
	return 0;
}

/* Setup ioctl functions */
static int vidioc_querycap(struct file *file, void *priv,
							struct v4l2_capability *vc)
{
	return -EINVAL;
}

static int vidioc_enum_input(struct file *file, void *priv,
							struct v4l2_input *vi)
{
	return -EINVAL;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *input)
{
	return -EINVAL;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int input)
{
	return -EINVAL;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *id)
{
	return -EINVAL;
}

static int vidioc_g_tuner(struct file *file, void *priv, struct v4l2_tuner *vt)
{
	return -EINVAL;
}

static int vidioc_s_tuner(struct file *file, void *priv, struct v4l2_tuner *vt)
{
	return -EINVAL;
}

static int vidioc_g_frequency(struct file *file, void *priv,
							struct v4l2_frequency *freq)
{
	return -EINVAL;
}

static int vidioc_s_frequency(struct file *file, void *priv,
							struct v4l2_frequency *freq)
{
	return -EINVAL;
}

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	return -EINVAL;
}

static int vidioc_s_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	return -EINVAL;
}

static int vidioc_queryctrl(struct file *file, void *priv,
							struct v4l2_queryctrl *ctrl)
{
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
							struct v4l2_control *ctrl)
{
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
							struct v4l2_control *ctrl)
{
	return -EINVAL;
}

static int vidioc_reqbufs(struct file *file, void *priv,
							struct v4l2_requestbuffers *vr)
{
	return -EINVAL;
}

static int vidioc_querybuf(struct file *file, void *priv,
							struct v4l2_buffer *vb)
{
	return -EINVAL;
}

static int vidioc_qbuf(struct file *file, void *priv,
							struct v4l2_buffer *vb)
{
	return -EINVAL;
}

static int vidioc_dqbuf(struct file *file, void *priv,
							struct v4l2_buffer *vb)
{
	return -EINVAL;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	return -EINVAL;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type type)
{
	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_fmtdesc *vfd)
{
	return -EINVAL;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_format *vf)
{
	return -EINVAL;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_format *vf)
{
	return -EINVAL;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_format *vf)
{
	return -EINVAL;
}

static ssize_t somagic_v4l2_read(struct file *file, char __user *buf,
							size_t count, loff_t *ppos)
{
	return -EFAULT;
} 

static int somagic_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -EFAULT;
}

/************* Structs *********/

static const struct v4l2_file_operations somagic_fops = {
	.owner = THIS_MODULE,
	.open = somagic_v4l2_open,
	.release = somagic_v4l2_close,
	.read = somagic_v4l2_read,
	.mmap = somagic_v4l2_mmap,
	.unlocked_ioctl = video_ioctl2,
/* .poll = video_poll, */
};

static const struct v4l2_ioctl_ops somagic_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_audio       = vidioc_g_audio,
	.vidioc_s_audio       = vidioc_s_audio,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
};

static struct video_device somagic_video_template = {
	.fops = &somagic_fops,
	.ioctl_ops = &somagic_ioctl_ops,
	.name = "somagic-video",
	.release = video_device_release,
	.tvnorms = SOMAGIC_NORMS,
	.current_norm = V4L2_STD_PAL
};

/** Static function definitions **/
static struct usb_somagic *somagic_alloc(struct usb_device *dev,
              struct usb_interface *intf);
static void somagic_release(struct usb_somagic *somagic);

static int somagic_register_video(struct usb_somagic *somagic);
static void somagic_unregister_video(struct usb_somagic *somagic);

static struct video_device *somagic_vdev_init(struct usb_somagic *somagic,
              struct video_device *vdev_template, char *name);


int __devinit somagic_capture_device_register(struct usb_interface *intf)
{
  struct usb_somagic *somagic	= NULL;
  struct usb_device *dev = usb_get_dev(interface_to_usbdev(intf));
  if (dev->descriptor.idVendor != SOMAGIC_USB_VENDOR_ID
      || dev->descriptor.idProduct != SOMAGIC_USB_PRODUCT_ID) {
    return -ENODEV;
  }

	somagic = somagic_alloc(dev, intf);
	if (somagic == NULL) {
		dev_err(&intf->dev, "%s: couldn't allocate Somagic struct\n", __func__);
		return -ENOMEM;
	}

	//somagic_configure_video(somagic);
	somagic_register_video(somagic);

	somagic_create_sysfs(somagic->vdev);

	return 0;
}

void somagic_capture_device_deregister(struct usb_interface *intf)
{
	struct usb_somagic *somagic = to_somagic(usb_get_intfdata(intf));

	mutex_lock(&somagic->v4l2_lock);

	v4l2_device_disconnect(&somagic->v4l2_dev);

	usb_put_dev(somagic->dev);
	somagic->dev = NULL;

	mutex_unlock(&somagic->v4l2_lock);

	somagic_release(somagic);
}

static struct usb_somagic *somagic_alloc(struct usb_device *dev,
              struct usb_interface *intf)
{
  struct usb_somagic *somagic;
  somagic = kzalloc(sizeof(struct usb_somagic), GFP_KERNEL);
  if (somagic == NULL) {
    return NULL;
  }

  somagic->dev = dev;
  if (v4l2_device_register(&intf->dev, &somagic->v4l2_dev)) {
    kfree(somagic);
    return NULL;
  }

	mutex_init(&somagic->v4l2_lock);

  printk(KERN_INFO "%s: v4l2 Device Registered!\n", __func__);

  return somagic;
}

static void somagic_release(struct usb_somagic *somagic)
{
  somagic->initialized = 0;	

	somagic_remove_sysfs(somagic->vdev);
	somagic_unregister_video(somagic);

  v4l2_device_unregister(&somagic->v4l2_dev);
  kfree(somagic);
}

/* Register v4l2 Devices */
static int __devinit somagic_register_video(struct usb_somagic *somagic)
{
  somagic->vdev = somagic_vdev_init(somagic, &somagic_video_template, "Somagic Video");
	if (somagic->vdev == NULL) {
		goto err_exit;
	}

	if (video_register_device(somagic->vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		goto err_exit;
	}

	printk(KERN_INFO "Somagic[%d]: registered Somagic Video device %s [v4l2]\n",
					somagic->nr, video_device_node_name(somagic->vdev));

	return 0;

	err_exit:
		dev_err(&somagic->dev->dev, "Somagic[%d]: video_register_device() failed\n",
						somagic->nr);
		somagic_unregister_video(somagic);
		return -1;
}

/* Unregister v4l2 Devices */
static void somagic_unregister_video(struct usb_somagic *somagic)
{
	if (video_is_registered(somagic->vdev)) {
		video_unregister_device(somagic->vdev);
	} else {
		video_device_release(somagic->vdev);
	}
	somagic->vdev = NULL;
} 

static struct video_device *somagic_vdev_init(struct usb_somagic *somagic,
              struct video_device *vdev_template, char *name)
{
  struct usb_device *usb_dev = somagic->dev;
  struct video_device *vdev;

  if (usb_dev == NULL) {
    dev_err(&somagic->dev->dev, "%s: somagic->dev is not set\n", __func__);
    return NULL;
  }

  vdev = video_device_alloc();
  if (vdev == NULL) {
    return NULL;
  }
	*vdev = *vdev_template;
	vdev->lock = &somagic->v4l2_lock;
	vdev->v4l2_dev = &somagic->v4l2_dev;
	snprintf(vdev->name, sizeof(vdev->name), "%s", name);
	video_set_drvdata(vdev, somagic);
	return vdev;
}

