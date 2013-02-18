/*******************************************************************************
 * smi2021_bootloader.c                                                        *
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
#include <linux/firmware.h>

#define SMI2021_FIRMWARE		"smi2021_firmware.bin"

#define FIRMWARE_CHUNK_SIZE		64
#define FIRMWARE_CHUNK_DATA_SIZE	62
#define FIRMWARE_CHUNK_HEADER_SIZE	2
#define FIRMWARE_CHUNK_HEADER		{ 0x05, 0xff }	/* %1111 1111 0000 0101 */
#define FIRMWARE_CHUNK_URB_VALUE	0x0005		/* %0000 0000 0000 0101 */

#define ACK_READY_URB_VALUE		0x0001		/* %0000 0000 0000 0001 */
#define ACK_READY_0			0x01		/* %0000 0001 */
#define ACK_READY_1			0x07		/* %0000 0111 */

#define SENDING_COMPLETE_URB_VALUE	0x0007		/* %0000 0000 0000 0111 */
#define SENDING_COMPLETE_0		0x07		/* %0000 0111 */
#define SENDING_COMPLETE_1		0x00		/* %0000 0000 */

void smi2021_run_bootloader(struct usb_device *dev)
{
	int rc, i, e;
	u8 firmware_ack[2];
	u8 firmware_chunk[FIRMWARE_CHUNK_SIZE] = FIRMWARE_CHUNK_HEADER;
	const u8 *dptr;
	const struct firmware * firmware = (const struct firmware *)NULL;

	rc = request_firmware(&firmware, SMI2021_FIRMWARE,
				&dev->dev);
	if (rc) {
		smi2021_err("request_firmware failed with: %d\n", rc);
		return;
	}

	if (firmware == (const struct firmware *)NULL) {
		smi2021_err("firmware is NULL");
		return;
	}

	if (firmware->size % FIRMWARE_CHUNK_DATA_SIZE) {
		smi2021_err("firmware has wrong size\n");
		return;
	}

	/* Prepare device for firmware upload */
	rc = usb_control_msg(dev,
				usb_rcvctrlpipe(dev, 0x80),
				0x01, (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				ACK_READY_URB_VALUE, 0x0000,
				(void *)firmware_ack, 2, 1000);

	if (firmware_ack[0] != ACK_READY_0 || firmware_ack[1] != ACK_READY_1 ) {
		smi2021_err("could not upload firmware");
		return;
	}

	dptr = firmware->data;

	for (i = 0; i < firmware->size / FIRMWARE_CHUNK_DATA_SIZE; i++) {
		for (e = FIRMWARE_CHUNK_HEADER_SIZE;
					e < FIRMWARE_CHUNK_SIZE; e++) {
			firmware_chunk[e] = *dptr;
			dptr++;
		}
		rc = usb_control_msg(dev, 
				usb_sndctrlpipe(dev,	0x00),
				0x01, (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				FIRMWARE_CHUNK_URB_VALUE, 0x0000,
				(void *)firmware_chunk,
				FIRMWARE_CHUNK_SIZE,
				1000);

		if (rc < 0) {
			smi2021_err("failed to uploading part of firmware");
			return;
		}
	}

	firmware_ack[0] = SENDING_COMPLETE_0;
	firmware_ack[1] = SENDING_COMPLETE_1;

	/* Done with firmware upload */
	rc = usb_control_msg(dev, 
				usb_sndctrlpipe(dev, 0x00),
				0x01, (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				SENDING_COMPLETE_URB_VALUE, 0x0000,
				(void *)firmware_ack, 2,
				1000);
	
	smi2021_dbg("firmware upload succeded\n");
	return;
}
