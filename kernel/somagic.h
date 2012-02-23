/*******************************************************************************
 * somagic_main.c                                                              *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * This driver will only upload the firmware for the Somagic chip,             *
 * and reconnect the usb-dongle with new product id: 1c88:003c.                *
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

#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/types.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

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

#define SOMAGIC_DEFAULT_BRIGHTNESS 0x80 // 128 // u8
#define SOMAGIC_DEFAULT_CONTRAST 0x47   // 71  // s8
#define SOMAGIC_DEFAULT_SATURATION 0x40 // 64  // s8
#define SOMAGIC_DEFAULT_HUE 0x00        // 0   // s8 

#define SOMAGIC_URB_STD_TIMEOUT 1000
#define SOMAGIC_URB_STD_REQUEST 0x01
#define SOMAGIC_URB_STD_INDEX 0x0000
#define SOMAGIC_URB_PREPARE_FOR_FIRMWARE_VALUE 0x0001
#define SOMAGIC_URB_FIRMWARE_PART_VALUE  0x0005
#define SOMAGIC_URB_SEND_ENDPOINT 0x00
#define SOMAGIC_URB_RECEIVE_ENDPOINT 0x80

#define SOMAGIC_USB_CTRL_SEND_EP 0x01
#define SOMAGIC_USB_STD_REQUEST 0x01

#define SOMAGIC_ACK_READY_FOR_FIRMWARE 0x0701

#define SOMAGIC_NORMS (V4L2_STD_PAL | V4L2_STD_NTSC) // | V4L2_STD_SECAM | V4L2_STD_PAL_M)
#define SOMAGIC_NUM_FRAMES 4 // Maximum number of frames an application can get

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
	FRAME_STATE_GRABBING,			// ???
	FRAME_STATE_UNUSED,				// Frame is mapped to userspace
	FRAME_STATE_READY,				// Frame in Ingoing Queue
	FRAME_STATE_DONE, 				// Frame in Outgoing Queue
	FRAME_STATE_ERROR					// An error occured
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
struct somagic_sbuf {
	char *data;
	struct urb *urb;	
};

struct somagic_frame {
	char *data;								// Pointer to somagic_video->fbuf[offset_of_this_frame]
	volatile int grabstate;		// State of grabbing

	int index;								// Frame index

	int scanlength;						// The number of bytes this frame contains (one PAL frame should be (2 * 288 * 2 * 720)  Bytes)
	int bytes_read;						// Count the bytes read out of this buffer from userspace

	// Used by parser!
	enum line_sync_state line_sync;
	u16 line;
	u16 col;
	u8 field;
	u8 blank;

	int sequence;							// Frame number since start of capture
	struct timeval timestamp;	// Time, when frame was captured

	struct list_head frame;		// For frame inqueue/outqueue
};

struct somagic_audio {
};

struct somagic_video {
	struct v4l2_device v4l2_dev;
	struct video_device *vdev;				// This is the actual V4L2 Device

	struct mutex v4l2_lock;						
	unsigned int nr;

	//struct urb *ctrl_urb;
	//unsigned char ctrl_urb_buffer[8];
	//struct usb_ctrlrequest ctrl_urb_setup;

	unsigned int open_instances;
	u8 setup_sent;
	u8 streaming;

	volatile enum sync_state cur_sync_state;
	volatile u8 prev_field;	

	int max_frame_size;
	int num_frames;	
	int fbuf_size;
	char *fbuf;													// V4L2 Videodev buffer area for frame data! // This is allocated in somagic_video.c!
	spinlock_t queue_lock; 							// Spinlock for protecting mods on inqueue and outqueue
	wait_queue_head_t wait_frame;				// Processes waiting
	wait_queue_head_t wait_stream;			// Processes waiting

	struct list_head inqueue, outqueue;	// Input/Output Frame queue

	struct somagic_frame *cur_frame;		// Pointer to current frame
	struct somagic_frame frame[SOMAGIC_NUM_FRAMES];	// Frame buffer

	struct somagic_frame *cur_read_frame; // Used by somagic_v4l2_read (somagic_video.c)

	// Scratch space for ISOC Pipe
	unsigned char *scratch;
	int scratch_read_ptr;
	int scratch_write_ptr;

	struct somagic_sbuf sbuf[2];				// SOMAGIC_NUM_S_BUF

	int framecounter;										// For sequencing of frames sent to userspace

	// Debug - Info that can be retrieved from by sysfs calls!
	int received_urbs;

	// PAL/NTSC toggle handling
	v4l2_std_id cur_std;								// Current Video standard NTSC/PAL
	u16 field_lines;											// Lines per field NTSC:240 PAL:288
	int frame_size;											// Size of one completed frame

	// Input selection
	enum somagic_inputs cur_input;

	// Controls
	s8 cur_brightness;
	s8 cur_contrast;
	s8 cur_saturation;
	s8 cur_hue;
};

struct usb_somagic {
	int initialized;

	struct usb_device *dev;
	struct somagic_audio audio;
	struct somagic_video video;
};

// Function declarations for somagic_video.c
int somagic_connect_video(struct usb_somagic *somagic, bool default_ntsc);
void somagic_disconnect_video(struct usb_somagic *somagic);

//
// Function-declarations for somagic_dev.c
//
int somagic_dev_video_alloc_scratch(struct usb_somagic *somagic);
void somagic_dev_video_free_scratch(struct usb_somagic *somagic);

int somagic_dev_video_alloc_isoc(struct usb_somagic *somagic);
void somagic_dev_video_free_isoc(struct usb_somagic *somagic);

int somagic_dev_video_alloc_frames(struct usb_somagic *somagic,
                                   int number_of_frames);
void somagic_dev_video_free_frames(struct usb_somagic *somagic);
void somagic_dev_video_empty_framequeues(struct usb_somagic *somagic);

// Send saa7113 Setup code to device
int somagic_dev_init_video(struct usb_somagic *somagic, v4l2_std_id id);

int somagic_dev_video_set_std(struct usb_somagic *somagic, v4l2_std_id id);
int somagic_dev_video_set_input(struct usb_somagic *somagic, unsigned int input);

int somagic_dev_video_start_stream(struct usb_somagic *somagic);
void somagic_dev_video_stop_stream(struct usb_somagic *somagic);

void somagic_dev_video_set_brightness(struct usb_somagic *somagic, s32 value);
void somagic_dev_video_set_contrast(struct usb_somagic *somagic, s32 value);
void somagic_dev_video_set_saturation(struct usb_somagic *somagic, s32 value);
void somagic_dev_video_set_hue(struct usb_somagic *somagic, s32 value);
#endif /* SOMAGIC_H */

