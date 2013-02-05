#ifndef SOMAGIC_H
#define SOMAGIC_H 

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
/*
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/moduleparam.h>

#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/delay.h>

#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <linux/version.h>
*/

#define SOMAGIC_DRIVER_VERSION "0.1"

/* For ISOC */
#define SOMAGIC_MAX_PKT_SIZE 	3072
#define SOMAGIC_NUM_PACKETS 	64
#define SOMAGIC_NUM_BUFS 	16
#define SOMAGIC_ISOC_EP 	0x82

#define SOMAGIC_TRC_EAV 	0x10
#define SOMAGIC_TRC_VBI 	0x20
#define SOMAGIC_TRC_FIELD_2 	0x40
#define SOMAGIC_TRC		0x80

#define DEBUG
#ifdef DEBUG
#define somagic_dbg(fmt, args...)		\
	printk(KERN_DEBUG "somagic::%s: " fmt,	\
		__func__, ##args)		
#else
#define somagic_dbg(fmt, args...)
#endif

#define somagic_info(fmt, args...)		\
	pr_info("somagic::%s: " fmt,		\
		__func__, ##args)

#define somagic_warn(fmt, args...)		\
	pr_warn("somagic::%s: " fmt,		\
		__func__, ##args)

#define somagic_err(fmt, args...)		\
	pr_err("somagic::%s: " fmt,		\
		__func__, ##args)

struct somagic_i2c_data {
	u8 reg;
	u8 val;
	u16 reserved;
};

struct somagic_smi_data {
	u16 reg;
	u8 val;
	u8 reserved;	
};

struct somagic_usb_ctrl {
	u8 head;
	u8 addr;
	u8 bm_data_type;
	u8 bm_data_offset;
	u8 data_size;
	u8 data[4];
};

/* Buffer for one video frame */
struct somagic_buffer {
	/* Common vb2 stuff, must be first */
	struct vb2_buffer 		vb;
	struct list_head 		list;

	void 				*mem;
	unsigned int 			length;

	bool 				second_field;
	bool				in_blank;
	unsigned int			pos;

};

struct somagic_isoc_ctl {
	int max_pkt_size;
	int num_bufs;
	struct urb **urb;
	char **transfer_buffer;
	struct somagic_buffer *buf;
};


struct somagic_fmt {
	char				*name;
	u32				fourcc;
	int				depth;
};

struct somagic_dev {
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
	struct somagic_isoc_ctl		isoc_ctl;

	int				width;		/* current frm width */
	int				height;		/* current frm height */
	unsigned int 			ctl_input;	/* selected input */
	v4l2_std_id			norm;		/* current norm */
	struct somagic_fmt 		*fmt;		/* selected format */
	unsigned int			buf_count;	/* for buffers */

	/* i2c i/o */
	struct i2c_adapter 		i2c_adap;
	struct i2c_client 		i2c_client;

	struct mutex 			v4l2_lock;
	struct mutex 			vb_queue_lock;
	spinlock_t 			buf_lock;
};

/* Provided by somagic_bootloader.c */
void somagic_run_bootloader(struct usb_device *somagic_device);

/* Provided by somagic_main.c */
int somagic_write_reg(struct somagic_dev *dev, u8 addr, u16 reg, u8 val);
int somagic_read_reg(struct somagic_dev *dev, u8 addr, u16 reg, u8 *val);

/* Provided by somagic_v4l2.c */
int somagic_vb2_setup(struct somagic_dev *dev);
int somagic_video_register(struct somagic_dev *dev); 
void somagic_clear_queue(struct somagic_dev *dev);

/* Provided by somagic_video.c */
int somagic_alloc_isoc(struct somagic_dev *dev);
void somagic_free_isoc(struct somagic_dev *dev);
void somagic_cancel_isoc(struct somagic_dev *dev);
void somagic_uninit_isoc(struct somagic_dev *dev);

/* Provided by somagic_i2c.c */
int somagic_i2c_register(struct somagic_dev *dev);
int somagic_i2c_unregister(struct somagic_dev *dev);

#endif /* SOMAGIC_H */
