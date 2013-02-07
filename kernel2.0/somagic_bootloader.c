/*******************************************************************************
 * somagic_bootloader.c                                                        *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * This driver will upload the firmware for the Somagic chip, and reconnect    *
 * the USB dongle with new product id: 1c88:003c.                              *
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

#include "somagic.h"
#include <linux/firmware.h>

#define SOMAGIC_FIRMWARE		"somagic_firmware.bin"

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

void somagic_run_bootloader(struct usb_device *somagic_device)
{
	int rc, i, e;
	u8 firmware_ack[2];
	u8 firmware_chunk[FIRMWARE_CHUNK_SIZE] = FIRMWARE_CHUNK_HEADER;
	const u8 *dptr;
	const struct firmware * firmware = (const struct firmware *)NULL;

	rc = request_firmware(&firmware, SOMAGIC_FIRMWARE,
				&somagic_device->dev);
	if (rc) {
		somagic_err("request_firmware failed with: %d\n", rc);
		return;
	}

	if (firmware == (const struct firmware *)NULL) {
		somagic_err("firmware is NULL");
		return;
	}

	if (firmware->size % FIRMWARE_CHUNK_DATA_SIZE) {
		somagic_err("firmware has wrong size\n");
		return;
	}

	/* Prepare device for firmware upload */
	rc = usb_control_msg(somagic_device,
				usb_rcvctrlpipe(somagic_device, 0x80),
				0x01, (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				ACK_READY_URB_VALUE, 0x0000,
				(void *)firmware_ack, 2, 1000);

	if (firmware_ack[0] != ACK_READY_0 || firmware_ack[1] != ACK_READY_1 ) {
		somagic_err("could not upload firmware");
		return;
	}

	dptr = firmware->data;

	for (i = 0; i < firmware->size / FIRMWARE_CHUNK_DATA_SIZE; i++) {
		for (e = FIRMWARE_CHUNK_HEADER_SIZE;
					e < FIRMWARE_CHUNK_SIZE; e++) {
			firmware_chunk[e] = *dptr;
			dptr++;
		}
		rc = usb_control_msg(somagic_device, 
				usb_sndctrlpipe(somagic_device,	0x00),
				0x01, (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				FIRMWARE_CHUNK_URB_VALUE, 0x0000,
				(void *)firmware_chunk,
				FIRMWARE_CHUNK_SIZE,
				1000);

		if (rc < 0) {
			somagic_err("failed to uploading part of firmware");
			return;
		}
	}

	firmware_ack[0] = SENDING_COMPLETE_0;
	firmware_ack[1] = SENDING_COMPLETE_1;

	/* Done with firmware upload */
	rc = usb_control_msg(somagic_device, 
				usb_sndctrlpipe(somagic_device, 0x00),
				0x01, (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
				SENDING_COMPLETE_URB_VALUE, 0x0000,
				(void *)firmware_ack, 2,
				1000);
	
	somagic_dbg("firmware upload succeded\n");
	return;
}
