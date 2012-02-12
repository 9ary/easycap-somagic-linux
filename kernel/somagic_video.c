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

static ssize_t show_isoc_count(struct device *cd,
							struct device_attribute *attr, char *buf)
{
	struct video_device *vdev = container_of(cd, struct video_device, dev);
	struct usb_somagic *somagic = video_get_drvdata(vdev);
	return sprintf(buf, "%d\n", somagic->video.received_urbs); 
}

static DEVICE_ATTR(received_isocs, S_IRUGO, show_isoc_count, NULL);

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
		res = device_create_file(&vdev->dev, &dev_attr_received_isocs);
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
		device_remove_file(&vdev->dev, &dev_attr_received_isocs);
	}
}

/*****************************************************************************/
/*                                                                           */
/*            Video 4 Linux API  -  Init / Exit                              */
/*            Called when we plug/unplug the device                          */
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

	// Send SAA7113 - Setup
	//somagic_dev_init_video(somagic, V4L2_STD_PAL);
	somagic_dev_init_video(somagic, V4L2_STD_NTSC); //PMartos
	
	// Allocate scratch - ring buffer
	somagic_dev_video_alloc_scratch(somagic);

	// All setup done, we can register the v4l2 device.
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
	somagic_dev_video_free_scratch(somagic);

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
/*            V4L2 Ioctl handling functions                                  */
/*                                                                           */
/*****************************************************************************/

/*
 * vidioc_querycap()
 *
 * Query device capabilities.
 */
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
	vc->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

/*
 * vidioc_enum_input()
 *
 * The userspace application will call this function several times
 * and increase the index-value of the v4l2_input struct to get
 * the list of video input interfaces we support.
 */
static int vidioc_enum_input(struct file *file, void *priv,
							struct v4l2_input *vi)
{
	struct usb_somagic *somagic = video_drvdata(file);

	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	switch(vi->index) {
		case INPUT_CVBS : {
			strcpy(vi->name, "CVBS");
			break;
		}
		case INPUT_SVIDEO : {
			strcpy(vi->name, "S-VIDEO");
			break;
		}
		default :
			return -EINVAL;
	}

	vi->type = V4L2_INPUT_TYPE_CAMERA;
	vi->audioset = 0;
	vi->tuner = 0;
	vi->std = somagic->video.cur_std;
	vi->status = 0x00;
	vi->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;	
}

/*
 * vidioc_g_input()
 *
 * Return current selected input interface
 */
static int vidioc_g_input(struct file *file, void *priv, unsigned int *input)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	*input = (unsigned int)INPUT_CVBS;
	return 0;
}

/*
 * vidioc_s_input
 *
 * Set video input interface
 */
static int vidioc_s_input(struct file *file, void *priv, unsigned int input)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	if (input >= INPUT_MANY) {
		return -EINVAL;
	}

	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *id)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return 0;
}

static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	struct usb_somagic *somagic = video_drvdata(file);

	*a = somagic->video.cur_std;
	return 0;
}

static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return -EINVAL;
}

static int vidioc_s_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return -EINVAL;
}

/*
 * vidioc_queryctrl()
 *
 */
static int vidioc_queryctrl(struct file *file, void *priv,
							struct v4l2_queryctrl *ctrl)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
							struct v4l2_control *ctrl)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
							struct v4l2_control *ctrl)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return -EINVAL;
}

// Initialize buffers
static int vidioc_reqbufs(struct file *file, void *priv,
							struct v4l2_requestbuffers *vr)
{
	struct usb_somagic *somagic = video_drvdata(file);

	if (vr->memory != V4L2_MEMORY_MMAP) {
		return -EINVAL;
	}

	if (vr->count < 2) {
		vr->count = 2;
	} else if (vr->count > SOMAGIC_NUM_FRAMES) {
		vr->count = SOMAGIC_NUM_FRAMES;
	}

	somagic_dev_video_free_frames(somagic);
	somagic_dev_video_empty_framequeues(somagic);

	// Allocate the frames data-buffers
	// and return the number of frames we have available
	vr->count = somagic_dev_video_alloc_frames(somagic, vr->count);

	somagic->video.cur_frame = NULL;
	return 0;
}

// Request for buffer info
static int vidioc_querybuf(struct file *file, void *priv,
							struct v4l2_buffer *vb)
{
	struct usb_somagic *somagic = video_drvdata(file);
	struct somagic_frame *frame;

	// printk(KERN_ERR "somagic:: %s Called\n", __func__);

	if (vb->index >= somagic->video.num_frames) {
		return -EINVAL;
	}

	vb->flags = 0;
	frame = &somagic->video.frame[vb->index];

	switch (frame->grabstate) {
		case FRAME_STATE_READY : {
			vb->flags |= V4L2_BUF_FLAG_QUEUED;
			break;
		}
		case FRAME_STATE_DONE : {
			vb->flags |= V4L2_BUF_FLAG_DONE;
			break;
		}
		case FRAME_STATE_UNUSED : {
			vb->flags |= V4L2_BUF_FLAG_MAPPED;
			break;
		}
	}
	vb->memory = V4L2_MEMORY_MMAP;
	vb->m.offset = vb->index * PAGE_ALIGN(somagic->video.max_frame_size);
	vb->field = V4L2_FIELD_INTERLACED;
	vb->length = 720 * 2 * 627 * 2;
	vb->timestamp = frame->timestamp;
	vb->sequence = frame->sequence;

	return 0;
}

// Receive a buffer from userspace
static int vidioc_qbuf(struct file *file, void *priv,
							struct v4l2_buffer *vb)
{
	struct usb_somagic *somagic = video_drvdata(file);
	struct somagic_frame *frame;
	unsigned long lock_flags;

	// printk(KERN_ERR "somagic:: %s Called\n", __func__);
	
	if (vb->index >= somagic->video.num_frames) {
		return -EINVAL;
	}

	frame = &somagic->video.frame[vb->index];

	if (frame->grabstate != FRAME_STATE_UNUSED) {
		return -EAGAIN;
	}

	frame->grabstate = FRAME_STATE_READY;
	frame->scanlength = 0;
	frame->line = 0;
	frame->col = 0;

	vb->flags &= ~V4L2_BUF_FLAG_DONE;

	spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
	list_add_tail(&frame->frame, &somagic->video.inqueue);	
	spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);

	return 0;
}

// Send a buffer to userspace
static int vidioc_dqbuf(struct file *file, void *priv,
							struct v4l2_buffer *vb)
{
	struct usb_somagic *somagic = video_drvdata(file);
	struct somagic_frame *f;
	unsigned long lock_flags;
	int rc;

//	printk(KERN_ERR "somagic:: %s Called\n", __func__);

	if (list_empty(&(somagic->video.outqueue))) {
		rc = wait_event_interruptible(somagic->video.wait_frame,
																	!list_empty(&(somagic->video.outqueue)));
		if (rc) {
			return rc;
		}
	}

	spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
	f = list_entry(somagic->video.outqueue.next,
										 struct somagic_frame, frame);
	list_del(somagic->video.outqueue.next);
	spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);
	f->grabstate = FRAME_STATE_UNUSED;

/*
	printk(KERN_ERR "somagic::%s: About to pass a buffer to userspace: "\
									"scanlenght=%d", __func__, f->scanlength);
*/

	vb->memory = V4L2_MEMORY_MMAP;
	vb->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE;
	vb->index = f->index;
	vb->sequence = f->sequence;
	vb->timestamp = f->timestamp;
	vb->field = V4L2_FIELD_INTERLACED;
	vb->bytesused = f->scanlength;

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct usb_somagic *somagic = video_drvdata(file);

	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	somagic_dev_video_start_stream(somagic);

	return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type type)
{
	struct usb_somagic *somagic = video_drvdata(file);

	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	somagic_dev_video_stop_stream(somagic);

	return 0;
}

// Describe PIX Format
static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_fmtdesc *f)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	if (f->index != 0) {
		return -EINVAL;
	}

	f->flags = 0;
	f->pixelformat = V4L2_PIX_FMT_UYVY;
	strcpy(f->description, "Packed UYVY");
	return 0;
}

// Get Current Format
static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_format *vf)
{
	struct usb_somagic *somagic = video_drvdata(file);
	struct v4l2_pix_format *pix = &vf->fmt.pix;

	printk(KERN_ERR "somagic:: %s Called\n", __func__);

	pix->width = SOMAGIC_LINE_WIDTH; 
	pix->height = 2 * somagic->video.field_lines;
	pix->pixelformat = V4L2_PIX_FMT_UYVY;
	pix->field = V4L2_FIELD_INTERLACED;
	pix->bytesperline = SOMAGIC_BYTES_PER_LINE;
	pix->sizeimage = somagic->video.frame_size;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

// Try to set Video Format
static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_format *vf)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return 0;
}

// Set Video Format
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
							struct v4l2_format *vf)
{
	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	return 0;
}

/*****************************************************************************/
/*                                                                           */
/*            V4L2 General file-handling functions                           */
/*                                                                           */
/*****************************************************************************/

/*
 * somagic_v4l2_open()
 *
 * The can be several open instances of the device,
 * but only one instance should be able to stream data from the device.
 *
 * This enables us to send control messages to the device without 
 * stopping the streaming interface.
 */
static int somagic_v4l2_open(struct file *file)
{
	struct usb_somagic *somagic = video_drvdata(file);

	somagic->video.open_instances++;
	printk(KERN_INFO "somagic::%s: %d open instances\n",
         __func__, somagic->video.open_instances);

	if (somagic->video.open_instances == 1) {
		somagic_dev_video_alloc_isoc(somagic);
	}

	return 0;
//	return -EBUSY;
}

/*
 * somagic_v4l2_close()
 *
 * We need to count the open instances, 
 * and make sure we clean up after the last instance is closed!
 * 
 */
static int somagic_v4l2_close(struct file *file)
{
	struct usb_somagic *somagic = video_drvdata(file);
	somagic->video.open_instances--;


	if (!somagic->video.open_instances) {
		somagic_dev_video_free_isoc(somagic);

		somagic_dev_video_free_frames(somagic);
		somagic->video.cur_frame = NULL;
		somagic->video.streaming = 0;

		printk(KERN_INFO "somagic::%s: Freed frames!\n", __func__);
	}

	printk(KERN_INFO "somagic::%s: %d open instances\n",
         __func__, somagic->video.open_instances);

	return 0;
}

/*
 * somagic_v4l2_read()
 *
 * This gives us the ability to read streaming video data
 * from the device.
 *
 */
static ssize_t somagic_v4l2_read(struct file *file, char __user *buf,
							size_t count, loff_t *ppos)
{
	struct usb_somagic *somagic = video_drvdata(file);
	int noblock = file->f_flags & O_NONBLOCK;
	unsigned long lock_flags;
	int rc, i;
	struct somagic_frame *frame;

/*
	printk(KERN_INFO "somagic::%s: %ld bytes, noblock=%d", __func__,
										(unsigned long)count, noblock);
 */

	if (buf == NULL) {
		return -EFAULT;
	}

	/* We setup the frames, just like a regular v4l2 call to vidioc_reqbufs
   * would do
   */
	if (!somagic->video.num_frames) {
		somagic->video.cur_read_frame = NULL;
		somagic_dev_video_free_frames(somagic);
		somagic_dev_video_empty_framequeues(somagic);
		somagic_dev_video_alloc_frames(somagic, SOMAGIC_NUM_FRAMES);
		printk(KERN_INFO "somagic::%s: Allocated frames!\n", __func__);
	}

	somagic_dev_video_start_stream(somagic);

	// Enqueue Videoframes
	for (i = 0; i < somagic->video.num_frames; i++) {
		frame = &somagic->video.frame[i];

		if (frame->grabstate == FRAME_STATE_UNUSED) {
			// Mark frame as ready and enqueue the frame!
			frame->grabstate = FRAME_STATE_READY;
			frame->scanlength = 0;
			frame->line = 0;
			frame->bytes_read = 0;

			spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
			list_add_tail(&frame->frame, &somagic->video.inqueue);
			spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);
		}
	}

	// Now we wait for the driver to put a frame in the outqueue.
	if (somagic->video.cur_read_frame == NULL) {
		if (list_empty(&(somagic->video.outqueue))) {
			if (noblock) {
				return -EAGAIN;
			}

			rc = wait_event_interruptible(somagic->video.wait_frame,
													!list_empty(&(somagic->video.outqueue)));

			if (rc) {
				return rc;
			}
		}

		spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
		frame = list_entry(somagic->video.outqueue.next,
													struct somagic_frame, frame);
		list_del(somagic->video.outqueue.next);
		spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);

		somagic->video.cur_read_frame = frame;
	} else {
		frame = somagic->video.cur_read_frame;
	}

	if (frame->grabstate == FRAME_STATE_ERROR) {
		frame->bytes_read = 0;
		return 0;
	}

	if ((count + frame->bytes_read) > (unsigned long)frame->scanlength) {
		count = frame->scanlength - frame->bytes_read;
	}

	// We have a verified frame,
	// now copy the data from this frame to userspace!
	if (copy_to_user(buf, frame->data + frame->bytes_read, count)) {
		return -EFAULT;
	}

	// We store the amount of bytes copied to userspace in the frame,
	// so we can copy the rest of the frame in successive reads!
	frame->bytes_read += count;

/*
	printk(KERN_INFO "somagic::%s: frmx=%d, bytes_read=%d, scanlength=%d",
				 __func__, frame->index, frame->bytes_read, frame->scanlength);
*/

	if (frame->bytes_read >= frame->scanlength) {
		frame->bytes_read = 0;
		frame->grabstate = FRAME_STATE_UNUSED;
		somagic->video.cur_read_frame = NULL;
	}

	return count;
} 

/*
 * somagic_v4l2_mmap
 *
 * Memmory mapping?
 */
static int somagic_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long start = vma->vm_start;
	void *pos;
	int i;
	struct usb_somagic *somagic = video_drvdata(file);

	printk(KERN_ERR "somagic:: %s Called\n", __func__);

	if (!(vma->vm_flags & VM_WRITE) ||
			size != PAGE_ALIGN(somagic->video.max_frame_size)) {

		printk(KERN_ERR "somagic::%s: VM_Write not set Or wrong size!\n"\
										"max_frame_size=%d, size=%ld",
										__func__, somagic->video.max_frame_size, size);
		
		return -EFAULT;
	}

	for (i = 0; i < somagic->video.num_frames; i++) {
		if (((PAGE_ALIGN(somagic->video.max_frame_size)*i) >> PAGE_SHIFT) ==
				vma->vm_pgoff) {
			break;
		}
	}

	if (i == somagic->video.num_frames) {
		printk(KERN_ERR "somagic::%s: mmap:" \
										"user supplied mapping address is out of range!\n",
					 __func__ );
		return -EINVAL;
	}

	vma->vm_flags |= VM_IO;	
	vma->vm_flags |= VM_RESERVED;	// Avoid to swap out this VMA

	pos = somagic->video.frame[i].data;
	while(size > 0) {
		if (vm_insert_page(vma, start, vmalloc_to_page(pos))) {
			printk(KERN_WARNING "somagic::%s: mmap: vm_insert_page failed!\n",
						 __func__);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return 0;
}

/*****************************************************************************/
/*                                                                           */
/*            Structs                                                        */
/*                                                                           */
/*****************************************************************************/

static const struct v4l2_ioctl_ops somagic_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,	// LWN v4l2 article part5/5b
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,			// Get Video Format (YUYV) / http://v4l2spec.bytesex.org/spec/x6386.htm
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,			// Set Video Format -- N/A
	.vidioc_reqbufs       = vidioc_reqbufs,								// Setup Video buffer // LWN v4l2 article part6b
	.vidioc_querybuf      = vidioc_querybuf,							//
	.vidioc_qbuf          = vidioc_qbuf,									// Put a Video Buffer into incomming queue // LWN v4l2 arictle part6b
	.vidioc_dqbuf         = vidioc_dqbuf,									// Get a Video Buffer from the outgoing queue // LWN v4l2 arictle part6b
	.vidioc_s_std         = vidioc_s_std,									// Set TV Standard
	.vidioc_querystd      = vidioc_querystd,							// What standard the device believe it's receiving
	.vidioc_enum_input    = vidioc_enum_input,						// List of videoinputs on card // LWN v4l2 article part4
	.vidioc_g_input       = vidioc_g_input,								// Get Current Video Input
	.vidioc_s_input       = vidioc_s_input,								// Set Video input
	.vidioc_queryctrl     = vidioc_queryctrl,							// Send a list of available hardware controls // LWN v4l2 article part7
	.vidioc_g_audio       = vidioc_g_audio,
	.vidioc_s_audio       = vidioc_s_audio,
	.vidioc_g_ctrl        = vidioc_g_ctrl,								// Get hardware ctrl // LWN v4l2 article part7
	.vidioc_s_ctrl        = vidioc_s_ctrl,								// Set hardware ctrl // LWN v4l2 article part7
	.vidioc_streamon      = vidioc_streamon,							// Start streaming
	.vidioc_streamoff     = vidioc_streamoff,							// Stop Streaming, transfer all remaing buffers to userspace
};

static const struct v4l2_file_operations somagic_fops = {
	.owner = THIS_MODULE,
	.open = somagic_v4l2_open,					// Called when we open the v4l2 device
	.release = somagic_v4l2_close,
	.read = somagic_v4l2_read,
	.mmap = somagic_v4l2_mmap,
	.unlocked_ioctl = video_ioctl2,			// Use the struct v4l2_ioctl_ops for ioctl handling
/* .poll = video_poll, */
};


static struct video_device somagic_video_template = {
	.fops = &somagic_fops,
	.ioctl_ops = &somagic_ioctl_ops,
	.name = SOMAGIC_DRIVER_NAME,														// V4L2 Driver Name
	.release = video_device_release,
	.tvnorms = SOMAGIC_NORMS,   														// Supported TV Standards
	.current_norm = SOMAGIC_DEFAULT_STD,										// Current TV Standard on startup
	.vfl_type = VFL_TYPE_GRABBER
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

