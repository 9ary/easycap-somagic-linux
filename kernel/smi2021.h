/*******************************************************************************
 * smi2021.h                                                                   *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCap                                            *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This file is part of SMI2021
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
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#ifndef SMI2021_H
#define SMI2021_H 

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-chip-ident.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/saa7115.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#define SMI2021_DRIVER_VERSION "0.1"

/* For ISOC */
#define SMI2021_MAX_PKT_SIZE 	3072
#define SMI2021_NUM_PACKETS 	64
#define SMI2021_NUM_BUFS 	16
#define SMI2021_ISOC_EP 	0x82

#define SMI2021_BYTES_PER_LINE	1440
#define SMI2021_PAL_LINES	576
#define SMI2021_NTSC_LINES	486

#define SMI2021_TRC_EAV 	0x10
#define SMI2021_TRC_VBI 	0x20
#define SMI2021_TRC_FIELD_2 	0x40
#define SMI2021_TRC		0x80

#define DEBUG
#ifdef DEBUG
#define smi2021_dbg(fmt, args...)		\
	printk(KERN_DEBUG "smi2021::%s: " fmt,	\
		__func__, ##args)		
#else
#define smi2021_dbg(fmt, args...)
#endif

#define smi2021_info(fmt, args...)		\
	pr_info("smi2021::%s: " fmt,		\
		__func__, ##args)

#define smi2021_warn(fmt, args...)		\
	pr_warn("smi2021::%s: " fmt,		\
		__func__, ##args)

#define smi2021_err(fmt, args...)		\
	pr_err("smi2021::%s: " fmt,		\
		__func__, ##args)

struct smi2021_i2c_data {
	u8 reg;
	u8 val;
	u16 reserved;
};

struct smi2021_reg_data {
	u16 reg;
	u8 val;
	u8 reserved;	
};

struct smi2021_usb_ctrl {
	u8 head;
	u8 addr;
	u8 bm_data_type;
	u8 bm_data_offset;
	u8 data_size;
	u8 data[4];
};

enum smi2021_sync {
	HSYNC,
	SYNCZ1,
	SYNCZ2,
	TRC
};

/* Buffer for one video frame */
struct smi2021_buffer {
	/* Common vb2 stuff, must be first */
	struct vb2_buffer 		vb;
	struct list_head 		list;

	void 				*mem;
	unsigned int 			length;

	bool 				second_field;
	bool				in_blank;
	unsigned int			pos;

	u16				trc_av;
};

struct smi2021_isoc_ctl {
	int max_pkt_size;
	int num_bufs;
	struct urb **urb;
	char **transfer_buffer;
	struct smi2021_buffer *buf;
};


struct smi2021_fmt {
	char				*name;
	u32				fourcc;
	int				depth;
};

struct smi2021_dev {
	struct v4l2_device		v4l2_dev;
	struct video_device 		vdev;
	struct v4l2_ctrl_handler 	ctrl_handler;

	struct v4l2_subdev 		*sd_saa7113;
	
	struct usb_device 		*udev;
	struct device *			dev;

	/* Capture buffer queue */
	struct vb2_queue 		vb_vidq;

	/* ISOC control struct */
	struct list_head 		avail_bufs;
	struct smi2021_isoc_ctl		isoc_ctl;

	int				width;		/* frame width */
	int				height;		/* frame height */
	unsigned int 			ctl_input;	/* selected input */
	v4l2_std_id			norm;		/* current norm */
	struct smi2021_fmt 		*fmt;		/* selected format */
	unsigned int			buf_count;	/* for buffers */

	/* i2c i/o */
	struct i2c_adapter 		i2c_adap;
	struct i2c_client 		i2c_client;

	struct mutex 			v4l2_lock;
	struct mutex 			vb_queue_lock;
	spinlock_t 			buf_lock;

	enum smi2021_sync		sync_state;

	/* audio */
	struct snd_card			*snd_card;
	struct snd_pcm			*snd_pcm;
	struct snd_pcm_substream	*pcm_substream;
	int				pcm_dma_offset;
	int				pcm_dma_write_ptr;
	unsigned int			pcm_packets;
	bool				snd_elapsed_periode;
};

/* Provided by smi2021_bootloader.c */
void smi2021_run_bootloader(struct usb_device *smi2021_device);

/* Provided by smi2021_main.c */
int smi2021_write_reg(struct smi2021_dev *dev, u8 addr, u16 reg, u8 val);
int smi2021_read_reg(struct smi2021_dev *dev, u8 addr, u16 reg, u8 *val);

/* Provided by smi2021_v4l2.c */
int smi2021_vb2_setup(struct smi2021_dev *dev);
int smi2021_video_register(struct smi2021_dev *dev); 
void smi2021_clear_queue(struct smi2021_dev *dev);

/* Provided by smi2021_video.c */
int smi2021_alloc_isoc(struct smi2021_dev *dev);
void smi2021_free_isoc(struct smi2021_dev *dev);
void smi2021_cancel_isoc(struct smi2021_dev *dev);
void smi2021_uninit_isoc(struct smi2021_dev *dev);

/* Provided by smi2021_i2c.c */
int smi2021_i2c_register(struct smi2021_dev *dev);
int smi2021_i2c_unregister(struct smi2021_dev *dev);

/* Provided by smi2021_audio.c */
int smi2021_snd_register(struct smi2021_dev *dev);
void smi2021_snd_unregister(struct smi2021_dev *dev);
void smi2021_audio(struct smi2021_dev *dev, u8 *data, int len);
#endif /* SMI2021_H */
