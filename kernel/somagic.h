/*******************************************************************************
 * somagic.h                                                                   *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * TODO description                                                            *
 * *****************************************************************************
 *
 * Copyright 2011, 2012 Jon Arne Jørgensen
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

#ifndef SOMAGIC_H
#define SOMAGIC_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

#include <linux/i2c.h>
#include <linux/version.h>
/* TODO: Do we need this include? */
#include <linux/workqueue.h>

#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/types.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/control.h>

#define SOMAGIC_USB_VENDOR_ID 0x1c88
#define SOMAGIC_USB_BOOTLOADER_PRODUCT_ID 0x0007
#define SOMAGIC_USB_PRODUCT_ID 0x003c

#define SOMAGIC_DRIVER_NAME "SMI Grabber DEV"
#define SOMAGIC_DRIVER_VERSION "0.1"
#define SOMAGIC_DRIVER_DESCRIPTION "Driver for EasyCAP DC60, with Somagic SMI2021CBE chipset"

#define SOMAGIC_FIRMWARE "somagic_firmware.bin"

#define SOMAGIC_DATAPART_HEADER_HI 0xff
#define SOMAGIC_DATAPART_HEADER_LO 0x05
#define SOMAGIC_DATAPART_HEADER_SIZE 2
#define SOMAGIC_DATAPART_SIZE 64

#define SOMAGIC_DEFAULT_BRIGHTNESS 0x80			/* 128, u8 */
#define SOMAGIC_DEFAULT_CONTRAST 0x47			/* 71 , s8 */
#define SOMAGIC_DEFAULT_SATURATION 0x40			/* 64 , s8 */
#define SOMAGIC_DEFAULT_HUE 0x00			/* 0  , s8 */

#define SOMAGIC_URB_STD_TIMEOUT 1000
#define SOMAGIC_URB_STD_REQUEST 0x01
#define SOMAGIC_URB_STD_INDEX 0x0000
#define SOMAGIC_URB_PREPARE_FOR_FIRMWARE_VALUE 0x0001
#define SOMAGIC_URB_FIRMWARE_PART_VALUE  0x0005
#define SOMAGIC_URB_SEND_ENDPOINT 0x00
#define SOMAGIC_URB_RECEIVE_ENDPOINT 0x80
#define SOMAGIC_NUM_ISOC_BUFFERS 2

#define SOMAGIC_USB_CTRL_SEND_EP 0x01
#define SOMAGIC_USB_STD_REQUEST 0x01

#define SOMAGIC_ACK_READY_FOR_FIRMWARE 0x0701

#define SOMAGIC_NORMS (V4L2_STD_PAL | V4L2_STD_NTSC) // | V4L2_STD_SECAM | V4L2_STD_PAL_M) 
#define SOMAGIC_NUM_FRAMES 4				/* Maximum number of frames an application can get */

#define SOMAGIC_SCRATCH_BUF_SIZE 0x20000 // 128kB

#define SOMAGIC_LINE_WIDTH 720
#define SOMAGIC_STD_FIELD_LINES_PAL 288
#define SOMAGIC_STD_FIELD_LINES_NTSC 240
#define SOMAGIC_BYTES_PER_LINE 1440

/* V4L2 Device Inputs */
enum somagic_inputs {
	INPUT_CVBS,
	INPUT_SVIDEO,
	INPUT_MANY
};

enum {
	FRAME_STATE_GRABBING,				/* ??? */
	FRAME_STATE_UNUSED,				/* Frame is mapped to user space */
	FRAME_STATE_READY,				/* Frame in Ingoing Queue */
	FRAME_STATE_DONE,				/* Frame in Outgoing Queue */
	FRAME_STATE_ERROR				/* An error occured */
};

enum parse_state {
	PARSE_STATE_OUT,
	PARSE_STATE_CONTINUE,
	PARSE_STATE_NEXT_FRAME,
	PARSE_STATE_END_OF_PARSE
};

enum line_sync_state {
	HSYNC,
	SYNCZ1,
	SYNCZ2,
	SYNCAV
};

enum sync_state {
	SYNC_STATE_SEARCHING,
	SYNC_STATE_UNSTABLE,
	SYNC_STATE_STABLE
};

/* USB - Isochronous Buffer */
struct somagic_isoc_buffer {
	char *data;
	struct urb *urb;	
};

struct somagic_frame {
	char *data;                   /* Video data buffer */
	int length;                   /* Size of buffer */
	int index;                    /* Frame index */

	struct list_head list_index;  /* linked_list index */

	int bytes_read;               /* Bytes read from this buffer in user space */
	int sequence;                 /* Sequence number of frame, for user space  */
	struct timeval timestamp;     /* Time, when frame was captured */

	volatile int grabstate;       /* State of grabbing */

	/* Used by parser */
	enum line_sync_state line_sync;
	u16 line;
	u16 col;
	u8 field;
	u8 blank;

};

struct somagic_audio {
	struct snd_card *card;

	struct snd_pcm_substream *pcm_substream;
	int dma_write_ptr;

	struct tasklet_struct process_audio;

	int users;					/* Open counter */
	u8 elapsed_periode;

	unsigned long time;
};

struct somagic_video {
	struct v4l2_device v4l2_dev;
	struct video_device *vdev;            /* This is the actual V4L2 Device */

	struct mutex v4l2_lock;						
	unsigned int nr;                      /* Dev number */

	/* Scratch-space for storing raw SAA7113 Data */
	unsigned char *scratch;
	int scratch_read_ptr;
	int scratch_write_ptr;

	unsigned int open_instances;
	u8 setup_sent;

	volatile enum sync_state cur_sync_state;
	volatile u8 prev_field;	

	/* v4l2 Frame buffer handling */
	spinlock_t queue_lock;                /* Protecting inqueue and outqueue */
	struct list_head inqueue, outqueue;   /* Frame lists */
	int max_frame_size;
	int num_frames;
	int frame_buf_size;
	char *frame_buf;                      /* Main video buffer */
	wait_queue_head_t wait_frame;         /* Waiting for completion of frame */
	wait_queue_head_t wait_stream;        /* Processes waiting */

	struct tasklet_struct process_video;

	struct somagic_frame *cur_frame;      /* Pointer to frame beeing filled */
	struct somagic_frame frame[SOMAGIC_NUM_FRAMES];

	/* Pointer to frame beeing read by v4l2_read */
	struct somagic_frame *cur_read_frame;

	int framecounter;				/* For sequencing of frames sent to user space */

	/* PAL/NTSC toggle handling */
	v4l2_std_id cur_std;		/* Current Video standard NTSC/PAL */
	u16 field_lines;				/* Lines per field NTSC:240 PAL:288 */
	int frame_size;					/* Size of one completed frame */

	/* Input selection */
	enum somagic_inputs cur_input;

	/* Controls */
	u8 cur_brightness;
	s8 cur_contrast;
	s8 cur_saturation;
	s8 cur_hue;
};

#define SOMAGIC_STREAMING_STARTED 0x01
#define SOMAGIC_STREAMING_CAPTURE_VIDEO 0x10
#define SOMAGIC_STREAMING_CAPTURE_AUDIO 0x20
#define SOMAGIC_STREAMING_CAPTURE_MASK 0xf0

struct usb_somagic {
	struct usb_device *dev;
	struct somagic_isoc_buffer isoc_buf[SOMAGIC_NUM_ISOC_BUFFERS];

	/*
	struct urb *ctrl_urb;
	unsigned char ctrl_urb_buffer[8];
	struct usb_ctrlrequest ctrl_urb_setup;
	*/

	/* Debug - Info that can be retrieved from by sysfs calls */
	int received_urbs;
	struct timeval prev_timestamp;

	spinlock_t streaming_flags_lock;
	u8 streaming_flags;

	struct somagic_audio audio;
	struct somagic_video video;
};

/* Function declarations for somagic_audio.c */
int somagic_alsa_init(struct usb_somagic *somagic);
void somagic_alsa_exit(struct usb_somagic *somagic);
void somagic_audio_put(struct usb_somagic *somagic, u8 *data, int size);

// Function declarations for somagic_video.c
int somagic_v4l2_init(struct usb_somagic *somagic /*, bool default_ntsc*/);
void somagic_v4l2_exit(struct usb_somagic *somagic);
void somagic_video_put(struct usb_somagic *somagic, u8 *data, int size);


// Function declarations for somagic_dev.c
int somagic_dev_init(struct usb_interface *intf);
void somagic_dev_exit(struct usb_interface *intf);

int somagic_start_stream(struct usb_somagic *somagic);
void somagic_stop_stream(struct usb_somagic *somagic);

int somagic_dev_video_set_std(struct usb_somagic *somagic, v4l2_std_id id);
int somagic_dev_video_set_input(struct usb_somagic *somagic, 
							unsigned int input);

void somagic_dev_video_set_brightness(struct usb_somagic *somagic, s32 value);
void somagic_dev_video_set_contrast(struct usb_somagic *somagic, s32 value);
void somagic_dev_video_set_saturation(struct usb_somagic *somagic, s32 value);
void somagic_dev_video_set_hue(struct usb_somagic *somagic, s32 value);
#endif /* SOMAGIC_H */

