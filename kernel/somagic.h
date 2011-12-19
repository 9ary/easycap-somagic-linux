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

#define USB_SOMAGIC_VENDOR_ID 0x1c88
#define USB_SOMAGIC_BOOTLOADER_PRODUCT_ID 0x0007
#define USB_SOMAGIC_PRODUCT_ID 0x003c

#define SOMAGIC_DRIVER_VERSION "0.1"
#define SOMAGIC_DRIVER_DESCRIPTION "Driver for Easycap DC60, with Somagic SMI2021CBE chipset"

#define SOMAGIC_FIRMWARE "somagic_firmware.bin"

#define SOMAGIC_DATAPART_HEADER_HI 0xff
#define SOMAGIC_DATAPART_HEADER_LO 0x05
#define SOMAGIC_DATAPART_HEADER_SIZE 2
#define SOMAGIC_DATAPART_SIZE 64

#define SOMAGIC_URB_STD_TIMEOUT 500
#define SOMAGIC_URB_STD_REQUEST 0x01
#define SOMAGIC_URB_STD_INDEX 0x0000
#define SOMAGIC_URB_PREPARE_FOR_FIRMWARE_VALUE 0x0001
#define SOMAGIC_URB_FIRMWARE_PART_VALUE  0x0005
#define SOMAGIC_URB_SEND_ENDPOINT 0x00
#define SOMAGIC_URB_RECEIVE_ENDPOINT 0x80

#define SOMAGIC_ACK_READY_FOR_FIRMWARE 0x0701

#endif /* SOMAGIC_H */
