/*******************************************************************************
 * init.c                                                                      *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * This userspace program will only upload the firmware for the Somagic chip,  *
 * and reconnect the usb-dongle with new product id: 1c88:003c.                *
 * *****************************************************************************
 *
 * Copyright 2011 Tony Brown, Jeffry Johnston
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

/* This file was originally generated with usbsnoop2libusb.pl from a usbsnoop log file. */
/* Latest version of the script should be in http://iki.fi/lindi/usb/usbsnoop2libusb.pl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <usb.h>

#define SOMAGIC_FIRMWARE_PATH "/lib/firmware/somagic_firmware.bin"
#define SOMAGIC_FIRMWARE_LENGTH 7502
#define VENDOR 0x1c88
#define ORIGINAL_PRODUCT 0x0007
#define NEW_PRODUCT 0x003c

struct usb_dev_handle *devh;

void release_usb_device(int dummy)
{
	int ret;

	ret = usb_release_interface(devh, 0);
	if (!ret) {
		perror("Failed to release interface");
	}
	usb_close(devh);
	if (!ret) {
		perror("Failed to close interface");
	}
	exit(1);
}

/*
void list_devices()
{
	struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;
		for (dev = bus->devices; dev; dev = dev->next) {
			printf("0x%04x 0x%04x\n", dev->descriptor.idVendor, dev->descriptor.idProduct);
		}
	}
}
*/

struct usb_device *find_device(int vendor, int product)
{
	struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;
		
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == vendor && dev->descriptor.idProduct == product)
				return dev;
		}
	}
	return NULL;
}

/*
void print_bytes(char *bytes, int len)
{
	int i;
	if (len > 0) {
		for (i = 0; i < len; i++) {
			printf("%02x ", (int)((unsigned char)bytes[i]));
		}
		printf("\"");
		for (i = 0; i < len; i++) {
			printf("%c", isprint(bytes[i]) ? bytes[i] : '.');
		}
		printf("\"");
	}
}
*/

int main(int argc, char **argv)
{
	int ret;
	struct usb_device *dev;
	char buf[65535];
	char *endptr;
	char firmware[SOMAGIC_FIRMWARE_LENGTH];
	FILE *infile;
	int i;
	/*int j;*/

	/* Read firmware file */
	infile = fopen(SOMAGIC_FIRMWARE_PATH, "r");
	if (infile == NULL) {
		perror("Error opening firmware file '" SOMAGIC_FIRMWARE_PATH "'");
		return 1;
	}
	if ((fseek(infile, 0, SEEK_END) == -1) || (ftell(infile) == -1)) {
		perror("Error determining firmware file '" SOMAGIC_FIRMWARE_PATH "' size");
		return 1;
	}
	if (ftell(infile) != SOMAGIC_FIRMWARE_LENGTH) {
		fprintf(stderr, "Firmware file '" SOMAGIC_FIRMWARE_PATH "' was not the expected size of %i bytes\n", SOMAGIC_FIRMWARE_LENGTH);
		return 1;
	}
	if ((fseek(infile, 0, SEEK_SET) == -1)) {
		perror("Error determining firmware file '" SOMAGIC_FIRMWARE_PATH "' size");
		return 1;
	}
	if (fread(&firmware, 1, SOMAGIC_FIRMWARE_LENGTH, infile) < SOMAGIC_FIRMWARE_LENGTH) {
		if (ferror(infile)) {
			perror("Error reading firmware file '" SOMAGIC_FIRMWARE_PATH "'");
		} else {
			fprintf(stderr, "Firmware file '" SOMAGIC_FIRMWARE_PATH "' was not the expected size of %i bytes\n", SOMAGIC_FIRMWARE_LENGTH);
		}
		return 1;
	}
	if (fclose(infile) != 0) {
		perror("Error closing firmware file '" SOMAGIC_FIRMWARE_PATH "'");
		return 1;
	}

	usb_init();
	usb_set_debug(0); /* was 255 -JLJ- */
	usb_find_busses();

	usb_find_devices();
	/* list_devices(); */
	dev = find_device(VENDOR, ORIGINAL_PRODUCT);
	if (!dev) {
		fprintf(stderr, "USB device %04x:%04x was not found.\n"
			"Either the device is not attached or a previous initialization was successful.\n", VENDOR, ORIGINAL_PRODUCT);
		return 1;
	}

	devh = usb_open(dev);
	if (!devh) {
		perror("Failed to open USB device");
		return 1;
	}
	
	signal(SIGTERM, release_usb_device);

	ret = usb_get_driver_np(devh, 0, buf, sizeof(buf));
	/* printf("usb_get_driver_np returned %d\n", ret); */
	if (ret == 0) {
		printf("Interface 0 already claimed by driver \"%s\", attempting to detach it.\n", buf);
		ret = usb_detach_kernel_driver_np(devh, 0);
		/*printf("usb_detach_kernel_driver_np returned %d\n", ret); */
	}

	ret = usb_claim_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to claim device interface");
		return 1;
	}
	
	ret = usb_set_altinterface(devh, 0);
	if (ret != 0) {
		perror("Failed to set active alternate setting for interface");
		return 1;
	}

	ret = usb_get_descriptor(devh, 0x0000001, 0x0000000, buf, 0x0000012);
	/*
	printf("1 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	*/
	ret = usb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000009);
	/*
	printf("2 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	*/
	ret = usb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000022);
	/*
	printf("3 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	*/
	ret = usb_release_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to release interface (before set_configuration)");
		return 1;
	}
	ret = usb_set_configuration(devh, 0x0000001);
	if (ret != 0) {
		perror("Failed to set active device configuration");
		return 1;
	}
	ret = usb_claim_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to claim device interface (after set_configuration)");
		return 1;
	}
	ret = usb_set_altinterface(devh, 0);
	if (ret != 0) {
		perror("Failed to set active alternate setting for interface (after set_configuration)");
		return 1;
	}

	usleep(1 * 1000);
	ret = usb_control_msg(devh, USB_TYPE_VENDOR + USB_RECIP_DEVICE + USB_ENDPOINT_IN, 0x0000001, 0x0000001, 0x0000000, buf, 0x0000002, 1000);
	/*
	printf("5 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	*/

	for (i = 0/*, j = 6*/; i < SOMAGIC_FIRMWARE_LENGTH; i += 0x000003e/*, ++j*/) {
		memcpy(buf, "\x05\xff", 0x0000002);
		memcpy(buf + 0x0000002, firmware + i, 0x000003e);
		ret = usb_control_msg(devh, USB_TYPE_VENDOR + USB_RECIP_DEVICE, 0x0000001, 0x0000005, 0x0000000, buf, 0x0000040, 1000);
		/*
		printf("%i control msg returned %d, bytes: ", j, ret); 
		print_bytes(buf, ret);
		printf("\n");
		*/
		usleep(1 * 1000);
	}

	memcpy(buf, "\x07\x00", 0x0000002);
	ret = usb_control_msg(devh, USB_TYPE_VENDOR + USB_RECIP_DEVICE, 0x0000001, 0x0000007, 0x0000000, buf, 0x0000002, 1000);
	/*
	printf("127 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	*/
	
	ret = usb_close(devh);
	if (ret != 0) {
		perror("Failed to close USB device");
		return 1;
	}

	/* Verify that the new device ID is found */
	usleep(500 * 1000);
	usb_find_devices();
	/* list_devices(); */
	dev = find_device(VENDOR, NEW_PRODUCT);
	if (!dev) {
		fprintf(stderr, "USB device %04x:%04x was not found. Initialization failed.\n", VENDOR, NEW_PRODUCT);
		return 1;
	}

	return 0;
}
