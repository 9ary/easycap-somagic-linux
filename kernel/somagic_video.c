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

/*****************************************************************************/
/*                                                                           */
/*            Scratch Buffer                                                 */
/*                                                                           */
/*            Ring-buffer used to store the bytes received from              */
/*            in the isochronous transfers while doing capture.              */
/*							                                                             */
/*            The ring-buffer is read when the driver processes the data,    */
/*            and the data is then copied to the frame-buffers               */
/*            that we shift between kernelspace & userspace                  */
/*                                                                           */
/*****************************************************************************/

static int scratch_len(struct usb_somagic *somagic)
{
	int len = somagic->video.scratch_write_ptr - somagic->video.scratch_read_ptr;

	if (len < 0) {
		len += SOMAGIC_SCRATCH_BUF_SIZE;
	}

	return len;
}

/*
 * scratch_free()
 *
 * Returns the free space left in buffer
 *
 * NOT USED, UNCOMMENT IF NEEDED!
static int scratch_free(struct usb_somagic *somagic)
{
	int free = somagic->scratch_read_ptr - somagic->scratch_write_ptr;
	if (free <= 0) {
		free += SOMAGIC_SCRATCH_BUF_SIZE;
	}

	if (free) {
		// At least one byte in the buffer must be left blank,
		// otherwise ther is no chance to differ between full and empty
		free -= 1;
	}

	return free;
}
*/

/*
 * somagic_video_put
 *
 * WARNING: This function is called inside an interrupt and must not sleep
 */
void somagic_video_put(struct usb_somagic *somagic,
												unsigned char *data, int len)
{
	int len_part;

	if (!(somagic->streaming_flags & SOMAGIC_STREAMING_CAPTURE_VIDEO)) {
		return;
	}

	if (somagic->video.scratch_write_ptr + len < SOMAGIC_SCRATCH_BUF_SIZE) {
		memcpy(somagic->video.scratch + somagic->video.scratch_write_ptr,
						data, len);
		somagic->video.scratch_write_ptr += len;
	} else {
		len_part = SOMAGIC_SCRATCH_BUF_SIZE - somagic->video.scratch_write_ptr;
		memcpy(somagic->video.scratch + somagic->video.scratch_write_ptr,
						data, len_part);

		if (len == len_part) {
			somagic->video.scratch_write_ptr = 0;
		} else {
			memcpy(somagic->video.scratch, data + len_part, len - len_part);
			somagic->video.scratch_write_ptr = len - len_part;
		}
	}
}

static int scratch_get_custom(struct usb_somagic *somagic, int *ptr,
                              unsigned char *data, int len)
{
	int len_part;

	if (*ptr + len < SOMAGIC_SCRATCH_BUF_SIZE) {
		memcpy(data, somagic->video.scratch + *ptr, len);
	  *ptr += len;
	} else {
		len_part = SOMAGIC_SCRATCH_BUF_SIZE - *ptr;
		memcpy(data, somagic->video.scratch + *ptr, len_part);

		if (len == len_part) {
			*ptr = 0;
		} else {
			memcpy(data + len_part, somagic->video.scratch, len - len_part);
			*ptr = len - len_part;
		}
	}

	return len;
}

static inline void scratch_create_custom_pointer(struct usb_somagic *somagic,
                                                 int *ptr, int offset)
{
	*ptr = (somagic->video.scratch_read_ptr + offset) % SOMAGIC_SCRATCH_BUF_SIZE;
} 

static inline int scratch_get(struct usb_somagic *somagic,
											 unsigned char *data, int len)
{
	return scratch_get_custom(somagic, &(somagic->video.scratch_read_ptr),
														data, len);
}

static void scratch_reset(struct usb_somagic *somagic)
{
	somagic->video.scratch_read_ptr = 0;
	somagic->video.scratch_write_ptr = 0;
}

/*
 * allocaate_scratch_buffer()
 *
 * Allocate memory for the scratch - ring buffer
 */
static int allocate_scratch_buffer(struct usb_somagic *somagic)
{
	somagic->video.scratch = vmalloc_32(SOMAGIC_SCRATCH_BUF_SIZE);
	scratch_reset(somagic);

	if (somagic->video.scratch == NULL) {
		dev_err(&somagic->dev->dev,
						"%s: unable to allocate %d bytes for scratch\n",
						__func__, SOMAGIC_SCRATCH_BUF_SIZE);
		return -ENOMEM;
	}

	return 0;
}

/*
 * free_scratch_buffer()
 *
 * Free the scratch - ring buffer
 */
static void free_scratch_buffer(struct usb_somagic *somagic)
{
	if (somagic->video.scratch == NULL) {
		return;
	}
	vfree(somagic->video.scratch);
	somagic->video.scratch = NULL;
}

/*****************************************************************************/
/*                                                                           */
/*            Frame Buffers                                                  */
/*                                                                           */
/*            The frames are passed between kernelspace & userspace          */
/*            while the capture is running.                                  */
/*                                                                           */
/*            All the buffers is stored in one big chunk of memory.          */
/*            Each somagic_frame struct has a pointer to a different         */
/*            offset in this memory                                          */
/*                                                                           */
/*            The passing of the somagic_frame structs                       */
/*            is handled by                                                  */
/*            vidioc_dqbuf() and vidioc_qbuf()                               */
/*                                                                           */
/*****************************************************************************/
static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem) {
		return NULL;
	}

	memset(mem, 0, size);
	adr = (unsigned long)mem;
	while ((long) size > 0) {
		SetPageReserved(vmalloc_to_page((void*)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem) {
		return;
	}

	size = PAGE_ALIGN(size);
	adr = (unsigned long) mem;

	while((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vfree(mem);
}

/*
 * Allocate Buffer for somagic->video.frame_buf
 * This is the buffer that will hold the frame data received from the device,
 * when we have stripped of the TRC header & footer of each line.
 *
 * Return the number of frames we managed to allocate!

 * This function will be called from userspace
 * by a V4L2 API Call (vidioc_reqbufs).
 *
 * We must try to allocate the requested frames,
 * but if we don't have the memory we decrease by 
 * one frame and try again.
 */
static int alloc_frame_buffer(struct usb_somagic *somagic,
                                   int number_of_frames)
{
	int i;

	somagic->video.max_frame_size = PAGE_ALIGN(720 * 2 * 627 * 2);
	somagic->video.num_frames = number_of_frames;

	while (somagic->video.num_frames > 0) {
		somagic->video.frame_buf_size = somagic->video.num_frames *
				somagic->video.max_frame_size;

		somagic->video.frame_buf = rvmalloc(somagic->video.frame_buf_size);
		if (somagic->video.frame_buf) {
			// Success, we managed to allocate a frame buffer
			break;
		}
		somagic->video.num_frames--;
	}

	// Initialize locks and waitqueue
	spin_lock_init(&somagic->video.queue_lock);
	init_waitqueue_head(&somagic->video.wait_frame);
	init_waitqueue_head(&somagic->video.wait_stream);

	// Setup the frames
	for (i = 0; i < somagic->video.num_frames; i++) {
		somagic->video.frame[i].index = i;	
		somagic->video.frame[i].grabstate = FRAME_STATE_UNUSED;
		somagic->video.frame[i].data = somagic->video.frame_buf + 
			(i * somagic->video.max_frame_size);
	}

	return somagic->video.num_frames;
}

static void free_frame_buffer(struct usb_somagic *somagic)
{
	if (somagic->video.frame_buf != NULL) {
		rvfree(somagic->video.frame_buf, somagic->video.frame_buf_size);
		somagic->video.frame_buf = NULL;

		somagic->video.num_frames = 0;
	}
}

static void reset_frame_buffer(struct usb_somagic *somagic)
{
	int i;
	INIT_LIST_HEAD(&(somagic->video.inqueue));
	INIT_LIST_HEAD(&(somagic->video.outqueue));

	for (i = 0; i < somagic->video.num_frames; i++) {
		somagic->video.frame[i].grabstate = FRAME_STATE_UNUSED;
		somagic->video.frame[i].bytes_read = 0;	
	}
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
	vi->std = SOMAGIC_NORMS;
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
	struct usb_somagic *somagic = video_drvdata(file);

	*input = (unsigned int)somagic->video.cur_input;
	return 0;
}

/*
 * vidioc_s_input
 *
 * Set video input interface
 */
static int vidioc_s_input(struct file *file, void *priv, unsigned int input)
{
	struct usb_somagic *somagic = video_drvdata(file);
	return somagic_dev_video_set_input(somagic, input);
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct usb_somagic *somagic = video_drvdata(file);
	*id = somagic->video.cur_std;
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct usb_somagic *somagic = video_drvdata(file);
	return somagic_dev_video_set_std(somagic, *id);
}

/*
 * vidioc_querystd()
 *
 * A request to the device to check what TV-Norm it's sensing on it's input.
 * I'm not sure how to implement this,
 * so we just return the list of supported norms.
 */
static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	*a = SOMAGIC_NORMS;
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
	
	u32 id = ctrl->id;
	
	if (ctrl->id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		printk(KERN_INFO "somagic::%s: V4L2_CTRL_FLAG_NEXT_CTRL "\
                     "not implemented yet!\n", __func__);
		return -EINVAL;
	}

	memset(ctrl, 0, sizeof(ctrl));
	ctrl->id = id;
	ctrl->type = V4L2_CTRL_TYPE_INTEGER;
	// ctrl->flags = V4L2_CTRL_FLAG_SLIDER;
	ctrl->minimum = (s8)-128;
	ctrl->maximum = (s8)127;
	ctrl->step = 1;
	
	if (ctrl->id == V4L2_CID_BRIGHTNESS) {
		strlcpy(ctrl->name, "Brightness", sizeof(ctrl->name));
		ctrl->default_value = (u8)SOMAGIC_DEFAULT_BRIGHTNESS;
		ctrl->minimum = (u8)0;
		ctrl->maximum = (u8)0xff;
	} else if (ctrl->id == V4L2_CID_CONTRAST) {
		strlcpy(ctrl->name, "Contrast", sizeof(ctrl->name));
		ctrl->default_value = (s8)SOMAGIC_DEFAULT_CONTRAST;
	} else if (ctrl->id == V4L2_CID_SATURATION) {
		strlcpy(ctrl->name, "Saturation", sizeof(ctrl->name));
		ctrl->default_value = (s8)SOMAGIC_DEFAULT_SATURATION;
	} else if (ctrl->id == V4L2_CID_HUE) {
		strlcpy(ctrl->name, "Hue",  sizeof(ctrl->name));
		ctrl->default_value = (s8)SOMAGIC_DEFAULT_HUE;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
							struct v4l2_control *ctrl)
{
	struct usb_somagic *somagic = video_drvdata(file);

	if (ctrl->id == V4L2_CID_BRIGHTNESS) {
		ctrl->value = somagic->video.cur_brightness;
	} else if (ctrl->id == V4L2_CID_CONTRAST) {
		ctrl->value = somagic->video.cur_contrast;
	} else if (ctrl->id == V4L2_CID_SATURATION) {
		ctrl->value = somagic->video.cur_saturation;
	} else if (ctrl->id == V4L2_CID_HUE) {
		ctrl->value = somagic->video.cur_hue;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
							struct v4l2_control *ctrl)
{
	struct usb_somagic *somagic = video_drvdata(file);

	if (ctrl->id == V4L2_CID_BRIGHTNESS) {
		somagic_dev_video_set_brightness(somagic, ctrl->value);
	} else if (ctrl->id == V4L2_CID_CONTRAST) {
		somagic_dev_video_set_contrast(somagic, ctrl->value);
	} else if (ctrl->id == V4L2_CID_SATURATION) {
		somagic_dev_video_set_saturation(somagic, ctrl->value);
	} else if (ctrl->id == V4L2_CID_HUE) {
		somagic_dev_video_set_hue(somagic, ctrl->value);
	} else {
		return -EINVAL;
	}
	return 0;
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

	free_frame_buffer(somagic);
	reset_frame_buffer(somagic);

	// Allocate the frames data-buffers
	// and return the number of frames we have available
	vr->count = alloc_frame_buffer(somagic, vr->count);

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
	frame->length = 0;
	frame->line = 0;
	frame->col = 0;

	vb->flags &= ~V4L2_BUF_FLAG_DONE;

	spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
	list_add_tail(&frame->list_index, &somagic->video.inqueue);	
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
										 struct somagic_frame, list_index);
	list_del(somagic->video.outqueue.next);
	spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);
	f->grabstate = FRAME_STATE_UNUSED;

/*
	printk(KERN_ERR "somagic::%s: About to pass a buffer to userspace: "\
									"scanlenght=%d", __func__, f->length);
*/

	vb->memory = V4L2_MEMORY_MMAP;
	vb->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE;
	vb->index = f->index;
	vb->sequence = f->sequence;
	vb->timestamp = f->timestamp;
	vb->field = V4L2_FIELD_INTERLACED;
	vb->bytesused = f->length;

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct usb_somagic *somagic = video_drvdata(file);
	unsigned long lock_flags;

	somagic->video.cur_frame = NULL;
	scratch_reset(somagic);
	spin_lock_irqsave(&somagic->streaming_flags_lock, lock_flags);
	somagic->streaming_flags |= SOMAGIC_STREAMING_CAPTURE_VIDEO;
	spin_unlock_irqrestore(&somagic->streaming_flags_lock, lock_flags);
	somagic_start_stream(somagic);

	return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type type)
{
	struct usb_somagic *somagic = video_drvdata(file);
	unsigned long lock_flags;

	printk(KERN_ERR "somagic:: %s Called\n", __func__);
	spin_lock_irqsave(&somagic->streaming_flags_lock, lock_flags);
	somagic->streaming_flags &= ~SOMAGIC_STREAMING_CAPTURE_VIDEO;
	spin_unlock_irqrestore(&somagic->streaming_flags_lock, lock_flags);
	somagic_stop_stream(somagic);

	somagic->video.framecounter = 0;
	somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;

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
	unsigned long lock_flags;
	struct usb_somagic *somagic = video_drvdata(file);
	somagic->video.open_instances--;


	if (!somagic->video.open_instances) {

		somagic_stop_stream(somagic);
		free_frame_buffer(somagic);
		somagic->video.cur_frame = NULL;

		spin_lock_irqsave(&somagic->streaming_flags_lock, lock_flags);
		somagic->streaming_flags &= ~SOMAGIC_STREAMING_CAPTURE_VIDEO;
		spin_unlock_irqrestore(&somagic->streaming_flags_lock, lock_flags);
		somagic_stop_stream(somagic);

		somagic->video.framecounter = 0;
		somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;

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
		free_frame_buffer(somagic);
		reset_frame_buffer(somagic);
		alloc_frame_buffer(somagic, SOMAGIC_NUM_FRAMES);
		printk(KERN_INFO "somagic::%s: Allocated frames!\n", __func__);
	}

	somagic->video.cur_frame = NULL;
	scratch_reset(somagic);
	spin_lock_irqsave(&somagic->streaming_flags_lock, lock_flags);
	somagic->streaming_flags |= SOMAGIC_STREAMING_CAPTURE_VIDEO;
	spin_unlock_irqrestore(&somagic->streaming_flags_lock, lock_flags);
	somagic_start_stream(somagic);

	// Enqueue Videoframes
	for (i = 0; i < somagic->video.num_frames; i++) {
		frame = &somagic->video.frame[i];

		if (frame->grabstate == FRAME_STATE_UNUSED) {
			// Mark frame as ready and enqueue the frame!
			frame->grabstate = FRAME_STATE_READY;
			frame->length = 0;
			frame->line = 0;
			frame->bytes_read = 0;

			spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
			list_add_tail(&frame->list_index, &somagic->video.inqueue);
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
													struct somagic_frame, list_index);
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

	if ((count + frame->bytes_read) > (unsigned long)frame->length) {
		count = frame->length - frame->bytes_read;
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
	printk(KERN_INFO "somagic::%s: frmx=%d, bytes_read=%d, length=%d",
				 __func__, frame->index, frame->bytes_read, frame->length);
*/

	if (frame->bytes_read >= frame->length) {
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
	.vidioc_g_std         = vidioc_g_std,									// Get Current TV Standard
	.vidioc_s_std         = vidioc_s_std,									// Set Current TV Standard
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
	.vfl_type = VFL_TYPE_GRABBER
};

/*****************************************************************************/
/*                                                                           */
/*            Video-parsing                                                  */
/*                                                                           */
/*****************************************************************************/
static void find_sync(struct usb_somagic *somagic)
{
	int look_ahead;
	u16 check;
	u8 c;
	u8 trc[3];

	u8 cur_vbi, cur_field;

	while(1) {
		if (scratch_len(somagic) < 1) {
			break;
		}

		scratch_get(somagic, &c, 1);
		if (c != 0xff) {
			continue;
		}

		if (scratch_len(somagic) < sizeof(trc)) {
			break;
		}

		scratch_create_custom_pointer(somagic, &look_ahead, 0);
		scratch_get_custom(somagic, &look_ahead, (unsigned char *)&check, sizeof(check));
		if (check != 0x0000) {
			continue;
		}

		// We have found [0xff 0x00 0x00]. Now find SAV/EAV
		scratch_get(somagic, trc, sizeof(trc));
		if (trc[2] == 0x00 || (trc[2] & 0x10) == 0x10) { // This is EAV (or SDID)
			continue;
		}

		cur_vbi = (trc[2] & 0x20) >> 5;
		cur_field = (trc[2] & 0x40) >> 6;

		if (somagic->video.cur_sync_state == SYNC_STATE_SEARCHING) {
			somagic->video.prev_field = cur_field;
			somagic->video.cur_sync_state = SYNC_STATE_UNSTABLE;
			continue;
		}
		
		if (cur_field == 0 && somagic->video.prev_field == 1) {
			somagic->video.cur_sync_state = SYNC_STATE_STABLE;
			return;
		}

		somagic->video.prev_field = cur_field;
	}
}

/* One PAL frame is 905000 Bytes, including TRC - (625 Lines).
 * An ODD Field is 4517756 Bytes, including TRC - (312 Lines). (288 Active video lines + 24 Lines of VBI)
 * An EVEN Field is 453224 Bytes, including TRC - (313 Lines). (288 Actice video lines + 25 Lines of VBI)
 *
 * VLC allocates: 831488 pr frame!
 *
 * mplayer expects one PAL frame to be: 829440
 * That is 576 (288 * 2) lines of 1440 (720 * 2)Bytes
 *
 */
static u8 parse_lines(struct usb_somagic *somagic)
{
	struct somagic_frame *frame;
	u8 c;
	int look_ahead;
	u8 check[8];

	frame = somagic->video.cur_frame;

	while(scratch_len(somagic) >= 1448) {
		scratch_create_custom_pointer(somagic, &look_ahead, 1440);
		scratch_get_custom(somagic, &look_ahead, check, 8);
		if (check[0] == 0xff && check[1] == 0x00 && check[2] == 0x00) {
			int line_pos = (2 * frame->line + frame->field) * (720 * 2) + frame->col;
			scratch_get(somagic, frame->data + line_pos, 1440);
			frame->length += 1440;

			/*
       * Just grab the TRC including EAV of this line, and handle it here!
       * check is already holding this info when we reach this part.
       * But we need to increment the regular scratch_read_ptr,
       * so we just grab the code again.
       *
       * Notice: We only read 4 bytes,
       * the last 4 bytes of check is containing data from the last call
       * to scratch_get_custom
			 */
			scratch_get(somagic, check, 4);

			if ((check[3] & 0x10) == 0x10)	{ // Double check that this actually is EAV
					
				frame->line++;
				frame->col = 0;
				if (frame->line > 313) {
					printk(KERN_WARNING "somagic::%s: SYNC Error, got line number %d\n", __func__, frame->line);						
					frame->line = 313;
				}

				// Now we check that the scratch containes the SAV of the next line
				if (check[4] == 0xff && check[5] == 0x00 && check[6] == 0x00) {
					/* SAV
 					 * 
 					 * F (Field bit) = Bit 6 (mask 0x40)
 					 * 0: Odd Field;
 					 * 1: Even Field;
 					 *
 					 * V (Vertical blanking bit) = Bit 5 (mask 0x20)
 					 * 0: in VBI
 					 * 1: in Active video
 					 */
					u8 field_edge;
					u8 blank_edge;

					// Again, we do this to increment the scratch_read_ptr!
					scratch_get(somagic, check + 4, 4);
					c = check[7];

					field_edge = frame->field;
					blank_edge = frame->blank;

					frame->field = (c & 0x40) >> 6;
					frame->blank = (c & 0x20) >> 5;

					field_edge = frame->field ^ field_edge;
					blank_edge = frame->blank ^ blank_edge;

					if (frame->field == 0 && field_edge) {
						if (frame->length < somagic->video.frame_size) {
							// This frame is not a full frame, something went wrong!
							if (printk_ratelimit())	{
								printk(KERN_INFO "somagic::%s: Got partial video, "\
                       "resetting sync state!\n", __func__);
							}
							somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;
						}
						return 1;
					}

					if (frame->blank == 0 && blank_edge) {
						frame->line = 0;
						frame->col = 0;
					}
					// We have sync, so we try to read next line!
					continue;	
				}
			} // Data is not followed by EAV
		} else {// We dont have FF 00 00 at 1440
			if (printk_ratelimit()) {
				printk(KERN_INFO "somagic::%s: Lost sync on line %d, "\
  	                     "swapping out current frame & resetting sync state!\n", __func__, frame->line);
			}
			somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;
			return 1;
		}
	}
	return 0;
}

static enum parse_state parse_data(struct usb_somagic *somagic)
{
	struct somagic_frame *frame;
	frame = somagic->video.cur_frame;

	while(1) {
		if (somagic->video.cur_sync_state != SYNC_STATE_STABLE) {
			find_sync(somagic);
			if (somagic->video.cur_sync_state != SYNC_STATE_STABLE) {
				return PARSE_STATE_OUT;
			}	
			frame->col = 0;
			frame->length = 0;
			frame->line = 0;
			frame->field = 0;
			frame->blank = 1;
		}

		if (parse_lines(somagic)) {
			return PARSE_STATE_NEXT_FRAME;
		} else {
			return PARSE_STATE_OUT;
		}
	}
	return PARSE_STATE_CONTINUE;
}

/*
 * process_video
 *
 * This tasklet is run when the isocronous interrupt has returned.
 * There should be new video data in the scratch-buffer now.
 *
 */
static void process_video(unsigned long somagic_addr)
{
	enum parse_state state;
	struct somagic_frame **f;
	unsigned long lock_flags;
	struct usb_somagic *somagic = (struct usb_somagic *)somagic_addr;

	if (!(somagic->streaming_flags & SOMAGIC_STREAMING_CAPTURE_VIDEO)) {
		return;
	}

	// We check if we have a v4l2_framebuffer to fill!
	f = &somagic->video.cur_frame;
	if (scratch_len(somagic) > 0x800 && !list_empty(&(somagic->video.inqueue))) {

		//printk(KERN_INFO "somagic::%s: Parsing Data", __func__);

		if (!(*f)) { // cur_frame == NULL
			(*f) = list_entry(somagic->video.inqueue.next,
												struct somagic_frame, list_index);
			(*f)->length = 0;
		}
	
		state = parse_data(somagic);

		if (state == PARSE_STATE_NEXT_FRAME) {

			if ((*f)->length > somagic->video.frame_size) {
				// This should never occur, don't know if we need to check this here?
				(*f)->length = somagic->video.frame_size;
			}

			(*f)->grabstate = FRAME_STATE_DONE;
			do_gettimeofday(&((*f)->timestamp));
			(*f)->sequence = somagic->video.framecounter;

			spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
			list_move_tail(&((*f)->list_index), &somagic->video.outqueue);
			somagic->video.cur_frame = NULL;
			spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);

			somagic->video.framecounter++;

			// Wake up threads waiting for frames in outqueue
			if (waitqueue_active(&somagic->video.wait_frame)) {
				wake_up_interruptible(&somagic->video.wait_frame);
			}
		}
	}
}


/*****************************************************************************/
/*                                                                           */
/*            Video 4 Linux API  -  Init / Exit                              */
/*            Called when we plug/unplug the device                          */
/*                                                                           */
/*****************************************************************************/

static struct video_device *vdev_init(struct usb_somagic *somagic,
        									             struct video_device *vdev_template,
                                       char *name, v4l2_std_id norm)
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
	vdev->current_norm = norm;
	return vdev;
}

int somagic_v4l2_init(struct usb_somagic *somagic /*, bool default_ntsc*/)
{
	int rc;

	//v4l2_std_id default_norm = (default_ntsc) ? V4L2_STD_NTSC : V4L2_STD_PAL;
	mutex_init(&somagic->video.v4l2_lock);
	
	tasklet_init(&(somagic->video.process_video), process_video, (unsigned long)somagic);

	rc = allocate_scratch_buffer(somagic);
	if (rc != 0) {
		printk(KERN_ERR "somagic::%s: Could not allocate scratch buffer!\n",
						__func__);
		goto err_exit;
	}

  if (v4l2_device_register(&somagic->dev->dev, &somagic->video.v4l2_dev)) {
		dev_err(&somagic->dev->dev, "Somagic[%d]: video_register_device() failed\n",
						somagic->video.nr);
    goto err_exit;
  }

  somagic->video.vdev = vdev_init(somagic, &somagic_video_template,
                                  SOMAGIC_DRIVER_NAME, V4L2_STD_PAL);
	if (somagic->video.vdev == NULL) {
		goto err_exit;
	}

	// All setup done, we can register the v4l2 device.
	if (video_register_device(somagic->video.vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		goto err_exit;
	}

	printk(KERN_INFO "Somagic[%d]: registered Somagic Video device %s [v4l2]\n",
					somagic->video.nr, video_device_node_name(somagic->video.vdev));

	return 0;

	err_exit:
    if (somagic->video.vdev) {
      if (video_is_registered(somagic->video.vdev)) {
        video_unregister_device(somagic->video.vdev);
      } else {
        video_device_release(somagic->video.vdev);
      }
    }
    return -1;
}

void somagic_v4l2_exit(struct usb_somagic *somagic)
{
	mutex_lock(&somagic->video.v4l2_lock);
	v4l2_device_disconnect(&somagic->video.v4l2_dev);
	usb_put_dev(somagic->dev);
	mutex_unlock(&somagic->video.v4l2_lock);

	if (somagic->video.vdev) {
		if (video_is_registered(somagic->video.vdev)) {
			video_unregister_device(somagic->video.vdev);
		} else {
			video_device_release(somagic->video.vdev);
		}
		somagic->video.vdev = NULL;
	}	
	v4l2_device_unregister(&somagic->video.v4l2_dev);

	tasklet_kill(&(somagic->video.process_video));
	free_scratch_buffer(somagic);
}

