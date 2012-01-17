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
#include "somagic.h"

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

/*
 * Module configuration-variables
 */

static int video_nr = -1;
/* Showing parameters under SYSFS */
module_param(video_nr, int, 0444);
// TODO: Find the right include for this macro
// MODULE_PARAM_DESC(video_nr, "Set video device number (/dev/videoX).
// Default: -1(autodetect)");

///////////////////////////////////////////////////////////////////////////////
/*****************************************************************************/
/*                                                                           */
/*            Function Declarations                                          */
/*                                                                           */
/*****************************************************************************/

/*
static inline struct usb_somagic *to_somagic(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct usb_somagic, video.v4l2_dev);
}
*/

static struct video_device *somagic_vdev_init(
																struct usb_somagic *somagic,
        									      struct video_device *vdev_template, char *name);

/*****************************************************************************/
/*                                                                           */
/*            Struct Declarations                                            */
/*                                                                           */
/*****************************************************************************/
static struct video_device somagic_video_template;


/*****************************************************************************/
/*                                                                           */
/* SYSFS Code	- Copied from the stv680.c usb module.												 */
/* Device information is located at /sys/class/video4linux/videoX            */
/* Device parameters information is located at /sys/module/somagic_easycap   */
/* Device USB information is located at /sys/bus/usb/drivers/somagic_easycap */
/*                                                                           */
/*****************************************************************************/

static ssize_t show_version(struct device *cd,
							struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", SOMAGIC_DRIVER_VERSION);
}

static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static void somagic_video_create_sysfs(struct video_device *vdev)
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

static void somagic_video_remove_sysfs(struct video_device *vdev)
{
	if (vdev) {
		device_remove_file(&vdev->dev, &dev_attr_version);
	}
}

/*****************************************************************************/
/*                                                                           */
/*            Video 4 Linux API  -  Init / Exit                              */
/*                                                                           */
/*****************************************************************************/

int __devinit somagic_connect_video(struct usb_somagic *somagic)
{
  if (v4l2_device_register(&somagic->dev->dev, &somagic->video.v4l2_dev)) {
    goto err_exit;
  }

	mutex_init(&somagic->video.v4l2_lock);

  somagic->video.vdev = somagic_vdev_init(somagic, &somagic_video_template, SOMAGIC_DRIVER_NAME);
	if (somagic->video.vdev == NULL) {
		goto err_exit;
	}

	if (video_register_device(somagic->video.vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		goto err_exit;
	}

	printk(KERN_INFO "Somagic[%d]: registered Somagic Video device %s [v4l2]\n",
					somagic->video.nr, video_device_node_name(somagic->video.vdev));

	somagic_video_create_sysfs(somagic->video.vdev);

	return 0;

	err_exit:
		dev_err(&somagic->dev->dev, "Somagic[%d]: video_register_device() failed\n",
						somagic->video.nr);

    if (somagic->video.vdev) {
      if (video_is_registered(somagic->video.vdev)) {
        video_unregister_device(somagic->video.vdev);
      } else {
        video_device_release(somagic->video.vdev);
      }
    }
    return -1;
}

void __devexit somagic_disconnect_video(struct usb_somagic *somagic)
{
	mutex_lock(&somagic->video.v4l2_lock);
	v4l2_device_disconnect(&somagic->video.v4l2_dev);
	usb_put_dev(somagic->dev);
	mutex_unlock(&somagic->video.v4l2_lock);

	somagic_video_remove_sysfs(somagic->video.vdev);

	if (somagic->video.vdev) {
		if (video_is_registered(somagic->video.vdev)) {
			video_unregister_device(somagic->video.vdev);
		} else {
			video_device_release(somagic->video.vdev);
		}
		somagic->video.vdev = NULL;
	}	
	v4l2_device_unregister(&somagic->video.v4l2_dev);
}

/*****************************************************************************/
/*                                                                           */
/*            V4L2 Functions                                                 */
/*                                                                           */
/*****************************************************************************/

/*
 * somagic_v4l2_open()
 *
 * This is part of the Video 4 Linux API.
 */
static int somagic_v4l2_open(struct file *file)
{
	struct usb_somagic *somagic = video_drvdata(file);
	int err_code = 0;

	printk(KERN_INFO "somagic::%s: CALLED\n", __func__);

	return 0;
//	return -EBUSY;
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
	struct usb_somagic *somagic = video_drvdata(file);
	if (somagic == NULL) {
		printk(KERN_ERR "somagic::%s: Driver-structure is NULL pointer\n", __func__);
		return -EINVAL;
	}
	strlcpy(vc->driver, SOMAGIC_DRIVER_NAME, sizeof(vc->driver));
	strlcpy(vc->card, "EasyCAP DC60", sizeof(vc->card));
	usb_make_path(somagic->dev, vc->bus_info, sizeof(vc->bus_info));
	vc->capabilities = 0;
	printk(KERN_INFO "somagic::%s: CALLED\n", __func__);
	return 0;
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

/*****************************************************************************/
/*                                                                           */
/*            Structs                                                        */
/*                                                                           */
/*****************************************************************************/

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

static struct video_device *somagic_vdev_init(
																struct usb_somagic *somagic,
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
	vdev->lock = &somagic->video.v4l2_lock;
	vdev->v4l2_dev = &somagic->video.v4l2_dev;
	snprintf(vdev->name, sizeof(vdev->name), "%s", name);
	video_set_drvdata(vdev, somagic);
	return vdev;
}

