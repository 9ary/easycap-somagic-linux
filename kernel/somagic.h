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

#define SOMAGIC_SAA_0A_DEFAULT 0x80
#define SOMAGIC_SAA_0B_DEFAULT 0x40
#define SOMAGIC_SAA_0C_DEFAULT 0x40
#define SOMAGIC_SAA_0D_DEFAULT 0x00

#define SOMAGIC_URB_STD_TIMEOUT 500
#define SOMAGIC_URB_STD_REQUEST 0x01
#define SOMAGIC_URB_STD_INDEX 0x0000
#define SOMAGIC_URB_PREPARE_FOR_FIRMWARE_VALUE 0x0001
#define SOMAGIC_URB_FIRMWARE_PART_VALUE  0x0005
#define SOMAGIC_URB_SEND_ENDPOINT 0x00
#define SOMAGIC_URB_RECEIVE_ENDPOINT 0x80

#define SOMAGIC_USB_CTRL_SEND_EP 0x01
#define SOMAGIC_USB_STD_REQUEST 0x01

#define SOMAGIC_ACK_READY_FOR_FIRMWARE 0x0701

#define SOMAGIC_NORMS (V4L2_STD_PAL | V4L2_STD_NTSC | V4L2_STD_SECAM | V4L2_STD_PAL_M)
#define SOMAGIC_NUM_FRAMES 4 // Maximum number of frames an application can get

enum {
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

enum scan_state {
	SCAN_STATE_SCANNING,
	SCAN_STATE_LINES,
	SCAN_STATE_VBI
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

struct somagic_sbuf {
	char *data;
	struct urb *urb;	
};

struct somagic_frame {
	char *data;								// Pointer to somagic_video->fbuf[offset_of_this_frame]
	volatile int grabstate;		// State of grabbing

	int index;								// Frame index

	int scanstate;						
	int scanlength;						// Mapped to v4l2_buffer.bytesused
	int bytes_read;

	enum line_sync_state line_sync;
	int line;									// Current line
	u8 sav;										// Last read SAV 

	int sequence;							// Frame number since start of capture
	struct timeval timestamp;	// Time, when frame was captured

	struct list_head frame;		//

	// v4l2_format -- Don't think we need this
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

	int max_frame_size;
	int num_frames;	
	int fbuf_size;
	char *fbuf;													// V4L2 Videodev buffer area for frame data!
	spinlock_t queue_lock; 							// Spinlock for protecting mods on inqueue and outqueue
	wait_queue_head_t wait_frame;				// Processes waiting
	wait_queue_head_t wait_stream;			// Processes waiting

	struct list_head inqueue, outqueue;	// Input/Output Frame queue

	struct somagic_frame *cur_frame;		// Pointer to current frame
	struct somagic_frame frame[SOMAGIC_NUM_FRAMES];	// Frame buffer

	// Scratch space for ISOC Pipe
	unsigned char *scratch;
	int scratch_read_ptr;
	int scratch_write_ptr;

	struct somagic_sbuf sbuf[2];				// SOMAGIC_NUM_S_BUF

	int frame_num;											// For sequencing of frames sent to userspace

	// Debug
	int received_urbs;

};

struct usb_somagic {
	int initialized;

	struct usb_device *dev;
	struct somagic_audio audio;
	struct somagic_video video;
};

// Function declarations for somagic_video.c
int somagic_connect_video(struct usb_somagic *somagic);
void somagic_disconnect_video(struct usb_somagic *somagic);

//
// Function-declarations for somagic_dev.c
//
int somagic_dev_alloc_video_scratch(struct usb_somagic *somagic);
// Send saa7113 Setup code to device
int somagic_dev_init_video(struct usb_somagic *somagic, v4l2_std_id std);

// Send the 2 bytes to make the device ready for isoc video transfer
int somagic_dev_start_video_stream(struct usb_somagic *somagic, int start);

// Initiate/stop usb isoc transfers
int somagic_dev_init_video_isoc(struct usb_somagic *somagic);
void somagic_dev_stop_video_isoc(struct usb_somagic *somagic);

#endif /* SOMAGIC_H */


// VPO 0-7 = Output pins
// Controlled by I2C-Bus Register LCR2 - LCR24
// If I2C-Bus bit VIPB = 1, the high bits of digitized inputs are connected to these outputs?
// This is configured by I2C control signals MODE3 to MODE0
//
// After Power-on (reset sequence) a complete I2C-Bus transmission is required. (saa7113h - Table2 - Page 24)
//
// The Programming of Multi-Standard VBI - Slicing (Teletext etc.)
// I2C Bus 41H - 57H (LCR2[7:0] to LCR24[7:0]),
// 0x5B [2-0], 0x59 (HOFF10-HOFF0) and
// 0x5B [4] , 0x5A (VOFF8-VOFF0)
//
// 8Bit VPO Bus, dataformats (saa7113h - Table 4 - Page 26)
// Type 3  = Widescreen signaling bits (32 Bytes pr line - Might be less) 
// Type 6  = YUV 4:2:2 (Testline) (1440 Bytes pr line) Only available on lines with (VREF = 0)
// Type 7  = RAW (Oversampled CVBS Data) (Programmable Bytecount)
// Type 15 = YUV 4:2:2 (Active Video) (1440 Bytes pr line) (720Pixels pr line)
//
// SAV/EAV format (Start Acttive Video / End Active Video | VBI - Vertical Blanking)
// Bit 7 = 1
// Bit 6 = (F)ield Bit (Odd/Even)
// Bit 5 = (V)ertical blanking bit (1 = VBI , 0 = Active Video)
// Bit 4 = (H) 0 = in SAV , 1 = in EAV (0 during active data)
// Bit 3-0 = Reserved!
