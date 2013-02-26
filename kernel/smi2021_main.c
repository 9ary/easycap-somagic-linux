/*******************************************************************************
 * smi2021_main.c                                                              *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCAP                                            *
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

#include "smi2021.h"

#define VENDOR_ID 			0x1c88
#define BOOTLOADER_PRODUCT_ID		0x0007
#define DC60_PRODUCT_ID			0x003c

static unsigned int imput;
module_param(imput, int, 0644);
MODULE_PARM_DESC(input, "Set default input");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne--a.t--gmail.com>");
MODULE_DESCRIPTION("SMI2021 - EasyCap");
MODULE_VERSION(SMI2021_DRIVER_VERSION);


struct usb_device_id smi2021_usb_device_id_table[] = {
	{ USB_DEVICE(VENDOR_ID, BOOTLOADER_PRODUCT_ID) },
	{ USB_DEVICE(VENDOR_ID, DC60_PRODUCT_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, smi2021_usb_device_id_table);

static unsigned short saa7113_addrs[] = {
	0x4a,
	I2C_CLIENT_END
};

/******************************************************************************/
/*                                                                            */
/*          Write to saa7113                                                  */
/*                                                                            */
/******************************************************************************/

inline int transfer_usb_ctrl(struct smi2021_dev *dev, struct smi2021_usb_ctrl data)
{
	return usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0x00), 0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x0b, 0x00,
			&data, sizeof(struct smi2021_usb_ctrl), 1000);
	
}

int smi2021_write_reg(struct smi2021_dev *dev, u8 addr, u16 reg, u8 val)
{
	int rc;
	struct smi2021_usb_ctrl data;

	data.head = 0x0b;
	data.data_size = 0x01;
	data.addr = addr;
	if (addr) {
		struct smi2021_i2c_data d = {
			.reg = reg,
			.val = val,
			.reserved = 0,
		};
		memcpy((void *)data.data, (void *)&d, 4);

		data.bm_data_type = 0xc0;
		data.bm_data_offset = 0x01;
	} else {
		struct smi2021_reg_data d = {
			.reg = __cpu_to_be16(reg),
			.val = val,
			.reserved = 0,
		};
		memcpy((void *)data.data, (void *)&d, 4);

		data.bm_data_type = 0x00;
		data.bm_data_offset = 0x82;
	}

	rc = transfer_usb_ctrl(dev, data);
	if (rc < 0) {
		smi2021_warn("write failed on register 0x%x, errno: %d\n",
			reg, rc);
		return rc;
	}

	return 0;
}

int smi2021_read_reg(struct smi2021_dev *dev, u8 addr, u16 reg, u8 *val)
{
	int rc;
	u8 rcv_data[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct smi2021_usb_ctrl data;
	struct smi2021_i2c_data d = {
		.reg = reg,
		.val = 0x00,
		.reserved = 0,
	};

	data.head = 0x0b;
	data.addr = addr;
	data.bm_data_type = 0x84;	/* 1000 0100 */
	data.bm_data_offset = 0x00;
	data.data_size = 0x01;
	memcpy((void *)data.data, (void *)&d, 4);

	*val = 0;

	rc = transfer_usb_ctrl(dev, data);
	if (rc < 0) {
		smi2021_warn("1st pass failing to read reg 0x%x, usb-errno: %d \n",
			reg, rc);
		return rc;
	}

	data.bm_data_type = 0xa0;	/* 1010 0000 */
	rc = transfer_usb_ctrl(dev, data);
	if (rc < 0) {
		smi2021_warn("2nd pass failing to read reg 0x%x, usb-errno: %d\n",
			reg, rc);
		return rc;
	}
	
	rc = usb_control_msg(dev->udev, 
		usb_rcvctrlpipe(dev->udev, 0x80), 0x01,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		0x0b, 0x00, rcv_data, 13, 1000);
	if (rc < 0) {
		smi2021_warn("Failed to read reg 0x%x, usb-errno: %d\n",
			reg, rc);
		return rc;
	}

	/*
	printk(KERN_INFO "smi2021::%s: Reg 0x%x returned\n"
		"\t0x%x 0x%x 0x%x 0x%x 0x%x --- 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		__func__, reg, rcv_data[0], rcv_data[1], rcv_data[2],
		rcv_data[3], rcv_data[4], rcv_data[5],
		rcv_data[6], rcv_data[7], rcv_data[8],
		rcv_data[9], rcv_data[10], rcv_data[11],
		rcv_data[12]);
	*/
	*val = rcv_data[5];
	return 0;
		
}

static void smi2021_reset_device(struct smi2021_dev *dev)
{
	smi2021_write_reg(dev, 0, 0x3a, 0x80);
	smi2021_write_reg(dev, 0, 0x3b, 0x80);
	smi2021_write_reg(dev, 0, 0x3b, 0x00);
}

static void release_v4l2_dev(struct v4l2_device *v4l2_dev)
{
	struct smi2021_dev *dev = container_of(v4l2_dev, struct smi2021_dev,
								v4l2_dev);
	smi2021_dbg("Releasing all resources\n");

	smi2021_i2c_unregister(dev);

	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
}

#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))

static int smi2021_scan_usb(struct usb_interface *intf, struct usb_device *udev)
{
	int i,e,ifnum,sizedescr, size;
	const struct usb_endpoint_descriptor *desc;
	ifnum = intf->altsetting[0].desc.bInterfaceNumber;

	for (i = 0; i < intf->num_altsetting; i++) {
		for (e = 0; e < intf->altsetting[i].desc.bNumEndpoints; e++) {
			desc = &intf->altsetting[i].endpoint[e].desc;
			sizedescr = le16_to_cpu(desc->wMaxPacketSize);
			size = sizedescr & 0x7ff;

			if (udev->speed == USB_SPEED_HIGH) {
				size = size * hb_mult(sizedescr);
			}

			printk(KERN_INFO "ep: %d, size: %d\n", e, size);
		}
	}
	return 0;
}

/******************************************************************************/
/*                                                                            */
/*          DEVICE  -  PROBE   &   DISCONNECT                                 */
/*                                                                            */
/******************************************************************************/
static int __devinit smi2021_usb_probe(struct usb_interface *intf,
						const struct usb_device_id *devid)
{
	int rc = 0;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct smi2021_dev *dev;

	smi2021_dbg("Probing for %04x:%04x\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct));

	if (udev == (struct usb_device *)NULL) {
		smi2021_err("device is NULL\n");
		return -EFAULT;
	}

  if (udev->descriptor.idProduct == BOOTLOADER_PRODUCT_ID) {
		smi2021_run_bootloader(udev);
		return 0;
	}
	
  if (udev->descriptor.idProduct != DC60_PRODUCT_ID) {
		return -ENODEV;
	}

	smi2021_scan_usb(intf, udev);

	dev = kzalloc(sizeof(struct smi2021_dev), GFP_KERNEL);
	if (dev == NULL) {
		return -ENOMEM;
	}

	dev->udev = udev;
	dev->dev = &intf->dev;
	usb_set_intfdata(intf, dev);

	/* Initialize videobuf2 stuff */
	rc = smi2021_vb2_setup(dev);
	if (rc < 0) {
		goto free_err;
	}

	spin_lock_init(&dev->buf_lock);
	mutex_init(&dev->v4l2_lock);
	mutex_init(&dev->vb_queue_lock);

	rc = v4l2_ctrl_handler_init(&dev->ctrl_handler, 0);
	if (rc) {
		smi2021_err("v4l2_ctrl_handler_init failed with: %d\n", rc);
		goto free_err;
	}

	dev->v4l2_dev.release = release_v4l2_dev;
	dev->v4l2_dev.ctrl_handler = &dev->ctrl_handler;
	rc = v4l2_device_register(dev->dev, &dev->v4l2_dev);
	if (rc) {
		smi2021_err("v4l2_device_register failed with %d\n", rc);
		goto free_ctrl;
	}

	smi2021_reset_device(dev);

	rc = smi2021_i2c_register(dev);
	if (rc < 0) {
		goto unreg_v4l2;
	}

	dev->sd_saa7113 = v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
		"saa7113", 0, saa7113_addrs);

	smi2021_dbg("Driver version %s successfully loaded\n",
			SMI2021_DRIVER_VERSION);

	v4l2_device_call_all(&dev->v4l2_dev, 0, core, reset, 0);
	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_stream, 0);

	rc = smi2021_snd_register(dev);
	if (rc < 0) {
		goto unreg_i2c;
	}

	rc = smi2021_video_register(dev);
	if (rc < 0) {
		goto unreg_snd;
	}

	return 0;

unreg_snd:
	smi2021_snd_unregister(dev);
unreg_i2c:
	smi2021_i2c_unregister(dev);
unreg_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);	
free_ctrl:
	v4l2_ctrl_handler_free(&dev->ctrl_handler);	
free_err:
	kfree(dev);

	return rc;
}

static void __devexit smi2021_usb_disconnect(struct usb_interface *intf)
{
	
	struct usb_device *udev = interface_to_usbdev(intf);
	struct smi2021_dev *dev;
 
	if (udev->descriptor.idProduct == BOOTLOADER_PRODUCT_ID) {
		return;
	}

	smi2021_dbg("Going for release!\n");

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	mutex_lock(&dev->vb_queue_lock);
	mutex_lock(&dev->v4l2_lock);

 	smi2021_uninit_isoc(dev);
 	/*stk1160_ac97_unrgister(dev)*/
 	smi2021_clear_queue(dev);

	video_unregister_device(&dev->vdev);
	v4l2_device_disconnect(&dev->v4l2_dev);

	/* This way current users can detect device is gone */
	dev->udev = NULL;

	mutex_unlock(&dev->v4l2_lock);
	mutex_unlock(&dev->vb_queue_lock);

	smi2021_snd_unregister(dev);

	/*
	 * This calls release_v4l2_dev if it's the last reference.
	 * Otherwise, the release is postponed until there are no users left.
	 */
	v4l2_device_put(&dev->v4l2_dev);
}

/******************************************************************************/
/*                                                                            */
/*            MODULE  -  INIT  &  EXIT                                        */
/*                                                                            */
/******************************************************************************/

struct usb_driver smi2021_usb_driver = {
	.name = "smi2021",
	.id_table = smi2021_usb_device_id_table,
	.probe = smi2021_usb_probe,
	.disconnect = smi2021_usb_disconnect
};

module_usb_driver(smi2021_usb_driver);

