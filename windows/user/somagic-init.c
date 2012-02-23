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
 * Copyright 2011, 2012 Tony Brown, Jeffry Johnston
 *
 * This file is part of somagic_easycap
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
#include <getopt.h>
#include <errno.h>
#include <unistd.h>

#define PROGRAM_NAME "somagic-init"
#define VERSION "1.0"
#define SOMAGIC_FIRMWARE_PATH "/lib/firmware/somagic_firmware.bin"
#define SOMAGIC_FIRMWARE_LENGTH 7502
#define VENDOR 0x1c88
#define ORIGINAL_PRODUCT 0x0007
#define NEW_PRODUCT 0x003c

struct usb_dev_handle *devh;

#ifdef DEBUG
void list_devices()
{
	struct usb_bus *bus;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		struct usb_device *dev;
		for (dev = bus->devices; dev; dev = dev->next) {
			printf("0x%04x:0x%04x\n", dev->descriptor.idVendor, dev->descriptor.idProduct);
		}
	}
}

void print_bytes(char *bytes, int len)
{
	int i;
	if (len > 0) {
		for (i = 0; i < len; i++) {
			printf("%02x ", (int)((unsigned char)bytes[i]));
		}
		printf("\"");
		for (i = 0; i < len; i++) {
			printf("%c", isprint((int)bytes[i]) ? bytes[i] : '.');
		}
		printf("\"");
	}
}
#endif

void release_usb_device(int ret)
{
	ret = usb_release_interface(devh, 0);
	if (!ret) {
		perror("Failed to release interface");
	}
	ret = usb_close(devh);
	if (!ret) {
		perror("Failed to close interface");
	}
	exit(1);
}

struct usb_device *find_device(int vendor, int product)
{
	struct usb_bus *bus;
	struct usb_device *dev;

	for (bus = usb_get_busses(); bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == vendor && dev->descriptor.idProduct == product)
				return dev;
		}
	}
	return NULL;
}

void version()
{
	fprintf(stderr, PROGRAM_NAME" "VERSION"\n");
	fprintf(stderr, "Copyright 2011, 2012 Tony Brown, Jeffry Johnston\n");
	fprintf(stderr, "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n");
	fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
	fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n");
}

void usage()
{
	fprintf(stderr, "Usage: "PROGRAM_NAME" [options]\n");
	fprintf(stderr, "  -f, --firmware=FILE   Use firmware file FILE\n");
	fprintf(stderr, "                        (default: "SOMAGIC_FIRMWARE_PATH")\n");
	fprintf(stderr, "      --help            Display usage\n");
	fprintf(stderr, "      --version         Display version information\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example (run as root):\n");
	fprintf(stderr, "# Initialize device (if not using kernel module)\n");
	fprintf(stderr, PROGRAM_NAME"\n");
}

int main(int argc, char **argv)
{
	int ret;
	struct usb_device *dev;
	char buf[65535];
	char firmware[SOMAGIC_FIRMWARE_LENGTH];
	FILE *infile;
	int i;
	#ifdef DEBUG
	int j;
	#endif
	char * firmware_path = SOMAGIC_FIRMWARE_PATH;

	/* parsing */
	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{"help", 0, 0, 0}, /* index 0 */
		{"version", 0, 0, 0}, /* index 1 */
		{"firmware", 1, 0, 'f'},
		{0, 0, 0, 0}
	};

	/* parse command line arguments */
	while (1) {
		c = getopt_long(argc, argv, "f:", long_options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 0:
			switch (option_index) {
			case 0: /* --help */
				usage();
				return 0;
			case 1: /* --version */
				version();
				return 0;
			default:
				usage();
				return 1;
			}
			break;
		case 'f':
			firmware_path = optarg;
			break;
		default:
			usage();
			return 1;
		}
	}
	if (optind < argc) {
		usage();
		return 1;
	}

	/* Read firmware file */
	infile = fopen(firmware_path, "r");
	if (infile == NULL) {
		fprintf(stderr, "%s: Error opening firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}
	if ((fseek(infile, 0, SEEK_END) == -1) || (ftell(infile) == -1)) {
		fprintf(stderr, "%s: Error determining firmware file '%s' size: %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}
	if (ftell(infile) != SOMAGIC_FIRMWARE_LENGTH) {
		fprintf(stderr, "Firmware file '%s' was not the expected size of %i bytes\n", firmware_path, SOMAGIC_FIRMWARE_LENGTH);
		return 1;
	}
	if ((fseek(infile, 0, SEEK_SET) == -1)) {
		fprintf(stderr, "%s: Error determining firmware file '%s' size: %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}
	if (fread(&firmware, 1, SOMAGIC_FIRMWARE_LENGTH, infile) < SOMAGIC_FIRMWARE_LENGTH) {
		if (ferror(infile)) {
			fprintf(stderr, "%s: Error reading firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
			return 1;
		}
		fprintf(stderr, "Firmware file '%s' was not the expected size of %i bytes\n", firmware_path, SOMAGIC_FIRMWARE_LENGTH);
		return 1;
	}
	if (fclose(infile) != 0) {
		fprintf(stderr, "%s: Error closing firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}

	usb_init();
	#ifdef DEBUG
	usb_set_debug(255);
	#else
	usb_set_debug(0);
	#endif

	usb_find_busses();
	usb_find_devices();
	#ifdef DEBUG
	list_devices();
	#endif
	dev = find_device(VENDOR, ORIGINAL_PRODUCT);
	if (!dev) {
		dev = find_device(VENDOR, NEW_PRODUCT);
		if (dev) {
			fprintf(stderr, "USB device already initialized.\n");
		} else {
			fprintf(stderr, "USB device %04x:%04x was not found. Is the device attached?\n", VENDOR, ORIGINAL_PRODUCT);
		}
		return 1;
	}

	devh = usb_open(dev);
	if (!devh) {
		perror("Failed to open USB device");
		return 1;
	}
       
	signal(SIGTERM, release_usb_device);

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
	#ifdef DEBUG
	printf("1 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
	ret = usb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000009);
	#ifdef DEBUG
	printf("2 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
	ret = usb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000022);
	#ifdef DEBUG
	printf("3 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
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
	ret = usb_control_msg(devh, USB_TYPE_VENDOR + USB_RECIP_DEVICE + USB_ENDPOINT_IN, 0x0000001, 0x0000001, 0x0000000, buf, 2, 1000);
	#ifdef DEBUG
	printf("5 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif

	#ifdef DEBUG
	for (i = 0, j = 6; i < SOMAGIC_FIRMWARE_LENGTH; i += 62, ++j) {
	#else
	for (i = 0; i < SOMAGIC_FIRMWARE_LENGTH; i += 62) {
	#endif
		memcpy(buf, "\x05\xff", 2);
		memcpy(buf + 2, firmware + i, 62);
		ret = usb_control_msg(devh, USB_TYPE_VENDOR + USB_RECIP_DEVICE, 0x0000001, 0x0000005, 0x0000000, buf, 64, 1000);
		#ifdef DEBUG
		printf("%i control msg returned %d, bytes: ", j, ret);
		print_bytes(buf, ret);
		printf("\n");
		#endif
		usleep(1 * 1000);
	}

	memcpy(buf, "\x07\x00", 2);
	ret = usb_control_msg(devh, USB_TYPE_VENDOR + USB_RECIP_DEVICE, 0x0000001, 0x0000007, 0x0000000, buf, 2, 1000);
	#ifdef DEBUG
	printf("127 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
       
	ret = usb_close(devh);
	if (ret != 0) {
		perror("Failed to close USB device");
		return 1;
	}

	return 0;
}
