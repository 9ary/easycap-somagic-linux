/*******************************************************************************
 * somagic-init.c                                                              *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60 and Somagic EasyCAP 002                 *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * This user space program will upload the firmware for the Somagic chip, and  *
 * reconnect the USB dongle with new product id: 1c88:003c or 1c88:003e.       *
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
#include <ctype.h>
#include <errno.h>
#include <gcrypt.h>
#include <getopt.h>
#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM_NAME "somagic-init"
#define VERSION "1.0"
#define SOMAGIC_FIRMWARE_PATH "/lib/firmware/somagic_firmware.bin"
static const unsigned char SOMAGIC_FIRMWARE_CRC32[2][4] = {
	{'\x34', '\x89', '\xf7', '\x7b'}, 
	{'\x9d', '\x91', '\x8a', '\x92'}
};
#define VENDOR 0x1c88
#define ORIGINAL_PRODUCT 0x0007
static const int NEW_PRODUCT[2] = {
	0x003c,
	0x003e
};

struct libusb_device_handle *devh;

#ifdef DEBUG
void list_devices()
{
	struct libusb_device **list;
	struct libusb_device_descriptor descriptor;
	struct libusb_device *item;
	int i;
	ssize_t count;
	count = libusb_get_device_list(NULL, &list);
	for (i = 0; i < count; i++) {
		item = list[i];
		libusb_get_device_descriptor(item, &descriptor);
		printf("0x%04x:0x%04x\n", descriptor.idVendor, descriptor.idProduct);
		libusb_unref_device(item);
	}
	libusb_free_device_list(list, 0);
}

void print_bytes(unsigned char *bytes, int len)
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
#endif

void release_usb_device(int ret)
{
	ret = libusb_release_interface(devh, 0);
	if (!ret) {
		perror("Failed to release interface");
	}
	libusb_close(devh);
	libusb_exit(NULL);
	exit(1);
}

struct libusb_device *find_device(int vendor, int product)
{
	struct libusb_device **list;
	struct libusb_device *dev = NULL;
	struct libusb_device_descriptor descriptor;
	struct libusb_device *item;
	int i;
	ssize_t count;
	count = libusb_get_device_list(NULL, &list);
	for (i = 0; i < count; i++) {
		item = list[i];
		libusb_get_device_descriptor(item, &descriptor);
		if (descriptor.idVendor == vendor && descriptor.idProduct == product) {
			dev = item;
		} else {
			libusb_unref_device(item);
		}
	}
	libusb_free_device_list(list, 0);
	return dev;
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
	fprintf(stderr, "  -f, --firmware=FILENAME  Use firmware file FILENAME\n");
	fprintf(stderr, "                           (default: "SOMAGIC_FIRMWARE_PATH")\n");
	fprintf(stderr, "      --help               Display usage\n");
	fprintf(stderr, "      --version            Display version information\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example (run as root):\n");
	fprintf(stderr, "# Initialize device (if not using kernel module)\n");
	fprintf(stderr, PROGRAM_NAME"\n");
}

int main(int argc, char **argv)
{
	int ret;
	struct libusb_device *dev;
	unsigned char buf[65535];
	char *firmware;
	FILE *infile;
	int i;
	#ifdef DEBUG
	int j;
	#endif
	char *firmware_path = SOMAGIC_FIRMWARE_PATH;

	/* Parsing */
	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{"help", 0, 0, 0},    /* index 0 */
		{"version", 0, 0, 0}, /* index 1 */
		{"firmware", 1, 0, 'f'},
		{0, 0, 0, 0}
	};
	int firmware_length;
	unsigned char digest[4];
	int p;

	/* Parse command line arguments */
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
	firmware_length = ftell(infile);
	firmware = malloc(firmware_length);
	if (firmware == NULL) {
		fprintf(stderr, "%s: Error allocating '%i' bytes of memory for firmware file '%s': %s\n", argv[0], firmware_length, firmware_path, strerror(errno));
		return 1;
	}
	if ((fseek(infile, 0, SEEK_SET) == -1)) {
		fprintf(stderr, "%s: Error seeking firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}
	if (fread(firmware, 1, firmware_length, infile) < (unsigned)firmware_length) {
		fprintf(stderr, "%s: Error reading firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}
	if (fclose(infile) != 0) {
		fprintf(stderr, "%s: Error closing firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
		return 1;
	}

	/* Identify firmware */
	gcry_md_hash_buffer(GCRY_MD_CRC32, digest, firmware, firmware_length);
	for (p = 0; p < 2; p++) {
		if (memcmp(digest, SOMAGIC_FIRMWARE_CRC32[p], 4) == 0) {
			break;
		}
	}
	if (p >= 2) {
		fprintf(stderr, "Firmware file '%s' was not recognized\n", firmware_path);
		return 1;
	}

	libusb_init(NULL);
	#ifdef DEBUG
	libusb_set_debug(NULL, 255); 
	list_devices(); 
	#else
	libusb_set_debug(NULL, 0); 
	#endif

	dev = find_device(VENDOR, ORIGINAL_PRODUCT);
	if (!dev) {
		dev = find_device(VENDOR, NEW_PRODUCT[p]);
		if (dev) {
	                fprintf(stderr, "USB device already initialized\n");
		} else {
	                fprintf(stderr, "USB device %04x:%04x was not found. Is the device attached?\n", VENDOR, ORIGINAL_PRODUCT);
		}
                return 1;
	}

	ret = libusb_open(dev, &devh);
	if (!devh) {
		perror("Failed to open USB device");
		return 1;
	}
	libusb_unref_device(dev);
	
	signal(SIGTERM, release_usb_device);
	ret = libusb_claim_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to claim device interface");
		return 1;
	}
	
	ret = libusb_set_interface_alt_setting(devh, 0, 0);
	if (ret != 0) {
		perror("Failed to set active alternate setting for interface");
		return 1;
	}

	ret = libusb_get_descriptor(devh, 0x0000001, 0x0000000, buf, 0x0000012);
	#ifdef DEBUG
	printf("1 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000009);
	#ifdef DEBUG
	printf("2 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000022);
	#ifdef DEBUG
	printf("3 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
	ret = libusb_release_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to release interface (before set_configuration)");
		return 1;
	}
	ret = libusb_set_configuration(devh, 0x0000001);
	if (ret != 0) {
		perror("Failed to set active device configuration");
		return 1;
	}
	ret = libusb_claim_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to claim device interface (after set_configuration)");
		return 1;
	}
	ret = libusb_set_interface_alt_setting(devh, 0, 0);
	if (ret != 0) {
		perror("Failed to set active alternate setting for interface (after set_configuration)");
		return 1;
	}

	usleep(1 * 1000);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x0000001, 0x0000000, buf, 2, 1000);
	#ifdef DEBUG
	printf("5 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif

	#ifdef DEBUG
	for (i = 0, j = 6; i < firmware_length; i += 62, ++j) {
	#else
	for (i = 0; i < firmware_length; i += 62) {
	#endif
		memcpy(buf, "\x05\xff", 2);
		memcpy(buf + 2, firmware + i, 62);
		ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x0000005, 0x0000000, buf, 64, 1000);
		#ifdef DEBUG
		printf("%i control msg returned %d, bytes: ", j, ret);
		print_bytes(buf, ret);
		printf("\n");
		#endif
		usleep(1 * 1000);
	}

	memcpy(buf, "\x07\x00", 2);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x0000007, 0x0000000, buf, 2, 1000);
	#ifdef DEBUG
	printf("127 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	printf("\n");
	#endif
	
	libusb_close(devh);
	libusb_exit(NULL);

	return 0;
}
