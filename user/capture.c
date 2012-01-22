/*******************************************************************************
 * capture.c                                                                   *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * Initializes the Somagic EasyCAP DC60 registers and performs image capture.  *
 * *****************************************************************************
 *
 * Copyright 2011, 2012 Tony Brown, Jeffry Johnston, Michal Demin	
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

/*
 * Usage (run as root):
 * init
 * capture pal 2> /dev/null | mplayer - -vf screenshot -demuxer rawvideo -rawvideo "w=720:h=576:format=uyvy:fps=25"
 * capture ntsc 2> /dev/null | mplayer - -vf screenshot -demuxer rawvideo -rawvideo "ntsc:format=uyvy:fps=30000/1001"
 */

/* This file was originally generated with usbsnoop2libusb.pl from a usbsnoop log file. */
/* Latest version of the script should be in http://iki.fi/lindi/usb/usbsnoop2libusb.pl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <libusb-1.0/libusb.h>
#include <execinfo.h>
#include <unistd.h>

#define VENDOR 0x1c88
#define PRODUCT 0x003c
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* control the number of concurrent ISO transfers we have running */
static const int NUM_ISO_TRANSFERS = 4;

/* control the number of frames to generate. -1 = unlimited. Should be a command line option */
static const int FRAME_COUNT = -1;

int frames_generated = 0;
int stop_sending_requests = 0;
int pending_requests = 0;

struct libusb_device_handle *devh;

enum tv_standards {
	PAL,
	NTSC
};
int tv_standard = NTSC;

void release_usb_device(int ret)
{
	fprintf(stderr, "Emergency exit\n");
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
	int i;
	ssize_t count;
	count = libusb_get_device_list(NULL, &list);
	for (i = 0; i < count; i++) {
		struct libusb_device *item = list[i];
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

void print_bytes(unsigned char *bytes, int len)
{
	int i;
	if (len > 0) {
		for (i = 0; i < len; i++) {
			fprintf(stderr, "%02x ", (int)bytes[i]);
		}
		fprintf(stderr, "\"");
		for (i = 0; i < len; i++) {
			fprintf(stderr, "%c", isprint(bytes[i]) ? bytes[i] : '.');
		}
		fprintf(stderr, "\"");
	}
}

void print_bytes_only(char *bytes, int len)
{
	int i;
	if (len > 0) {
		for (i = 0; i < len; i++) {
			if (i % 32 == 0) {
				fprintf(stderr, "\n%04x\t ", i);
			}
			fprintf(stderr, "%02x ", (int)((unsigned char)bytes[i]));
			/* if ((i + 1) % 16 == 0) {	 fprintf(stderr, "\n"); } */
		}
	}
}

void trace()
{
	void *array[10];
	size_t size;

	/* get void*'s for all entries on the stack */
	size = backtrace(array, 10);

	/* print out all the frames */
	backtrace_symbols_fd(array, size, 1);
	exit(1);
}


/*
 * Write  a number of bytes from the iso transfer buffer to the appropriate line and field of the frame buffer.
 * Returns the number of bytes actually used from the buffer
 */
int write_buffer(unsigned char *data, unsigned char *end, int count, unsigned char *frame, int line, int field)
{
	int dowrite;
	int line_pos;
	int lines_per_field = (tv_standard == PAL ? 288 : 240);
	dowrite = MIN(end - data, count);

	line_pos = line * (720 * 2) * 2 + (field * 720 * 2) + ((720 * 2) - count);

	if (line < lines_per_field) {
		memcpy(line_pos + frame, data, dowrite);
	}
	return dowrite;
}


enum sync_state {
	HSYNC,
	/*SYNCFF,*/
	SYNCZ1,
	SYNCZ2,
	SYNCAV,
	VBLANK,
	VACTIVE,
	REMAINDER
};

enum sync_state state = HSYNC;
int line_remaining = 0;
int active_line_count = 0;
int vblank_found = 0;
int field = 0;

unsigned char frame[720 * 2 * 288 * 2];

void process_data(unsigned char* buffer, int length)
{
	unsigned char *next = buffer;
	unsigned char *end = buffer + length;
	int bs = 0; /* bad (lost) sync: 0=no, 1=yes */
	int hs = 0;
	int lines_per_field = (tv_standard == PAL ? 288 : 240);
	do {
		unsigned char nc = *next;
		/*
		 * Timing reference code (TRC):
		 *     [ff 00 00 SAV] [ff 00 00 EAV]
		 * Where SAV is 80 or c7, and EAV is 9d or da.
		 * A line of video will look like (1448 bytes total):
		 *     [ff 00 00 EAV] [ff 00 00 SAV] [1440 bytes of UYVY video] (repeat on next line)
		 */
		switch (state) {
			case HSYNC:
				hs++;
				if (nc == (unsigned char)0xff) {
					state = SYNCZ1;
					if (bs == 1) {
						fprintf(stderr, "resync after %d @%ld(%04lx)\n", hs, next - buffer, next - buffer);
					}
					bs = 0;
				} else if (bs != 1) {
					/*
					 * The 1st byte in the TRC must be 0xff. It
					 * wasn't, so sync was either lost or has not
					 * yet been regained. Sync is regained by
					 * ignoring bytes until the next 0xff.
					 */
					fprintf(stderr, "bad sync on line %d @%ld (%04lx)\n", active_line_count, next - buffer, next - buffer);
					/*
					 *				print_bytes_only(pbuffer, buffer_pos + 64);
					 *				print_bytes(pbuffer + buffer_pos, 8);
					 */
					bs = 1;
				}
				next++;
				break;
			case SYNCZ1:
				if (nc == (unsigned char)0x00) {
					state = SYNCZ2;
				} else {
					/*
					 * The 2nd byte in the TRC must be 0x00. It
					 * wasn't, so sync was lost.
					 */
					state = HSYNC;
				}
				next++;
				break;
			case SYNCZ2:
				if (nc == (unsigned char)0x00) {
					state = SYNCAV;
				} else {
					/*
					 * The 3rd byte in the TRC must be 0x00. It
					 * wasn't, so sync was lost.
					 */
					state = HSYNC;
				}
				next++;
				break;
			case SYNCAV:
				/*
				 * Found 0xff 0x00 0x00, now expecting SAV or EAV. Might
				 * also be the SDID (sliced data ID), 0x00.
				 */
				/* fprintf(stderr,"%02x", nc); */
				if (nc == (unsigned char)0x00) {
					/*
					 * SDID detected, so we still haven't found the
					 * active YUV data.
					 */
					state = HSYNC;
					next++;
					break;
				}
				
				/*
				 * H = Bit 4 (mask 0x10).
				 * 0: in SAV, 1: in EAV.
				 */
				if (nc & (unsigned char)0x10) {
					/* EAV (end of active data) */
					state = HSYNC;
				} else {
					/* SAV (start of active data) */
					/*
						* F (field bit) = Bit 6 (mask 0x40).
						* 0: first field, 1: 2nd field.
						*/
					field = (nc & (unsigned char)0x40) ? 1 : 0;
					/*
						* V (vertical blanking bit) = Bit 5 (mask 0x20).
						* 0: in VBI, 1: in active video.
						*/
					if (nc & (unsigned char)0x20) {
						/* VBI (vertical blank) */
						state = VBLANK;
						vblank_found++;
						if (active_line_count > (lines_per_field - 8)) {
							if (field == 0) {
								if (frames_generated < FRAME_COUNT || FRAME_COUNT == -1) {
									write(1, frame, 720 * 2 * lines_per_field * 2);
									frames_generated++;
								}
								if (frames_generated >= FRAME_COUNT && FRAME_COUNT != -1) {
									stop_sending_requests = 1;
								}
								
							}
							vblank_found = 0;
							/* fprintf(stderr, "lines: %d\n", active_line_count); */
						}
						active_line_count = 0;
					} else {
						/* Line is active video */
						state = VACTIVE;
					}
					line_remaining = 720 * 2;
				}
				next++;
				break;
			case VBLANK:
			case VACTIVE:
			case REMAINDER:
				/* fprintf(stderr,"line %d, rem=%d ,next=%08x, end=%08x ", active_line_count, line_remaining, next, end); */
				if (state == VBLANK || vblank_found < 20) {
					int skip = MIN(line_remaining, (end - next));
					/* fprintf(stderr,"skipped: %d\n", skip); */
					line_remaining -= skip;
					next += skip ;
					/* fprintf(stderr, "vblank_found=%d\n", vblank_found); */
				} else {
					int wrote = write_buffer(next, end, line_remaining, frame, active_line_count, field);
					/* fprintf(stderr,"wrote: %d\n", wrote); */
					line_remaining -= wrote;
					next += wrote;
					if (line_remaining <= 0) {
						active_line_count++;
					}
				}
				/* fprintf(stderr, "vblank_found: %d, line remaining: %d, line_count: %d\n", vblank_found, line_remaining, active_line_count); */
				if (line_remaining <= 0) {
					state = HSYNC;
				} else {
					/* fprintf(stderr, "\nOn line %d, line_remaining: %d(%04x). bp=%04x/%04x\n", active_line_count, line_remaining, line_remaining, buffer_pos, buffer_size); */
					state = REMAINDER;
					/* no more data in this buffer. exit loop */
					next = end;
				}
				break;
		} /* end switch */
	} while (next < end);
}

void gotdata(struct libusb_transfer *tfr)
{
	int ret;
	int num = tfr->num_iso_packets;
	int i;
	
	pending_requests--;
	
	for (i = 0; i < num; i++) {
		unsigned char *data = libusb_get_iso_packet_buffer_simple(tfr, i);
		int length = tfr->iso_packet_desc[i].actual_length;
		int pos = 0;
		while (pos < length) {
			/* within each packet of the transfer, the data is divided into blocks of 0x400 bytes
			 * beginning with [0xaa 0xaa 0x00 0x00].
			 * Check for this signature and process each block of data individually
			 */
			if (data[pos] == 0xaa && data[pos + 1] == 0xaa && data[pos + 2] == 0x00 && data[pos + 3] == 0x00) {
				/* process the received data, excluding the 4 marker bytes */
				process_data(data + 4 + pos, 0x400 - 4);
			} else {
				fprintf(stderr, "Unexpected block, expected [aa aa 00 00] found [%02x %02x %02x %02x] \n", data[pos], data[pos+1], data[pos+2], data[pos+3]);
			}
			pos += 0x400;
		}
	}
	
	if (!stop_sending_requests) {
		ret = libusb_submit_transfer(tfr);
		if (ret != 0) {
			fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
			exit(1);
		}
		pending_requests++;
	}
}

void usage(char * name)
{
	fprintf(stderr, "Usage: %s pal | ntsc\n", name);
	exit(1);
}

uint8_t read_reg_buf[8];
static int somagic_read_reg(uint16_t reg, uint8_t *val)
{
	int ret;
	memcpy(read_reg_buf, "\x0b\x00\x20\x82\x01\x30\x80\xFF", 0x0000008);

	read_reg_buf[5] = reg >> 8;
	read_reg_buf[6] = reg & 0xff;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, read_reg_buf, 0x0000008, 1000);
	if (ret != 8) {
		fprintf(stderr, "read_reg msg returned %d, bytes: ", ret);
		print_bytes(read_reg_buf, ret);
		fprintf(stderr, "\n");
	}

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, read_reg_buf, 0x000000d, 1000);
	if (ret != 0x0d) {
		fprintf(stderr, "read_reg control msg returned %d, bytes: ", ret);
		print_bytes(read_reg_buf, ret);
		fprintf(stderr, "\n");
	}
	if (val) {
		*val = read_reg_buf[7];
	}

	return ret;
}

static int somagic_write_reg(uint16_t reg, uint8_t val)
{
	int ret;
	uint8_t buf[8];

	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3a\x00", 0x0000008);
	buf[5] = reg >> 8;
	buf[6] = reg & 0xff;
	buf[7] = val;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	if (ret != 8) {
		fprintf(stderr, "write reg control msg returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}

	return ret;
}

uint8_t read_i2c_buf[27];
static int somagic_read_i2c(uint8_t dev_addr, uint8_t reg, uint8_t *val)
{
	int ret;

	memcpy(read_i2c_buf, "\x0b\x4a\x84\x00\x01\x10\x00\x00\x00\x00\x00\x00\x00", 0x000000d);

	read_i2c_buf[1] = dev_addr;
	read_i2c_buf[5] = reg;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, read_i2c_buf, 0x000000d, 1000);
	fprintf(stderr, "-> i2c_read msg returned %d, bytes: ", ret);
	print_bytes(read_i2c_buf, ret);
	fprintf(stderr, "\n");
	usleep(18 * 1000);

	memcpy(read_i2c_buf, "\x0b\x4a\xa0\x00\x01\x00\xff\xff\xff\xff\xff\xff\xff", 0x000000d);

	read_i2c_buf[1] = dev_addr;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, read_i2c_buf, 0x000000d, 1000);
	fprintf(stderr, "-> i2c_read msg returned %d, bytes: ", ret);
	print_bytes(read_i2c_buf, ret);
	fprintf(stderr, "\n");

	memset(read_i2c_buf, 0xff, 0x000000d);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, read_i2c_buf, 0x000000d, 1000);
	fprintf(stderr, "<- i2c_read msg returned %d, bytes: ", ret);
	print_bytes(read_i2c_buf, ret);
	fprintf(stderr, "\n");
	usleep(11 * 1000);

	*val = read_i2c_buf[5];

	return ret;
}

static int somagic_write_i2c(uint8_t dev_addr, uint8_t reg, uint8_t val)
{
	int ret;
	uint8_t buf[8];

	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x01\x08\xf4", 0x0000008);

	buf[1] = dev_addr;
	buf[5] = reg;
	buf[6] = val;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	if (ret != 8) {
		fprintf(stderr, "write_i2c returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}

	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	uint8_t status;
	struct libusb_device *dev;
	
	/* buffer for control messages */
	unsigned char buf[65535];
	
	/* buffers and transfer pointers for isochronous data */
	struct libusb_transfer *tfr[NUM_ISO_TRANSFERS];
	unsigned char isobuf[NUM_ISO_TRANSFERS][64 * 3072];

	int i = 0;

	if (argc != 2) {
		usage(argv[0]);
	}
	if (strcmp(argv[1], "pal") == 0) {
		tv_standard = PAL;
	} else if (strcmp(argv[1], "ntsc") == 0) {
		tv_standard = NTSC;
	} else {
		usage(argv[0]);
	}

	libusb_init(NULL);
	libusb_set_debug(NULL, 0);

	dev = find_device(VENDOR, PRODUCT);
	if (!dev) {
		fprintf(stderr, "USB device %04x:%04x was not found.\n", VENDOR, PRODUCT);
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
		fprintf(stderr, "claim failed with error %d\n", ret);
		exit(1);
	}
	
	ret = libusb_set_interface_alt_setting(devh, 0, 0);
	if (ret != 0) {
		fprintf(stderr, "set_interface_alt_setting failed with error %d\n", ret);
		exit(1);
	}

	ret = libusb_get_descriptor(devh, 0x0000001, 0x0000000, buf, 0x0000012);
	fprintf(stderr, "1 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000009);
	fprintf(stderr, "2 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000042);
	fprintf(stderr, "3 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");

	ret = libusb_release_interface(devh, 0);
	if (ret != 0) {
		fprintf(stderr, "failed to release interface before set_configuration: %d\n", ret);
	}
	ret = libusb_set_configuration(devh, 0x0000001);
	fprintf(stderr, "4 set configuration returned %d\n", ret);
	ret = libusb_claim_interface(devh, 0);
	if (ret != 0) {
		fprintf(stderr, "claim after set_configuration failed with error %d\n", ret);
	}
	ret = libusb_set_interface_alt_setting(devh, 0, 0);
	fprintf(stderr, "4 set alternate setting returned %d\n", ret);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x0000001, 0x0000000, buf, 0x0000002, 1000);
	fprintf(stderr, "5 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");

	somagic_write_reg(0x3a, 0x80);
	somagic_write_reg(0x3b, 0x00);

	/* reset audio chip? */
	somagic_write_reg(0x34, 0x01);
	somagic_write_reg(0x35, 0x00);

	somagic_read_reg(0x3080, &status);
	fprintf(stderr, "status is %02x\n", status);

	/* reset audio chip? */
	somagic_write_reg(0x34, 0x11);
	somagic_write_reg(0x35, 0x11);

	/* SAAxxx: toggle reset of SAAxxx */
	somagic_write_reg(0x3b, 0x80);

	/* SAAxxx: bring from reset */
	somagic_write_reg(0x3b, 0x00);

	somagic_write_i2c(0x4a, 0x01, 0x08);
	somagic_write_i2c(0x4a, 0x02, 0xc7);
	somagic_write_i2c(0x4a, 0x03, 0x33);
	somagic_write_i2c(0x4a, 0x04, 0x00);
	somagic_write_i2c(0x4a, 0x05, 0x00);
	somagic_write_i2c(0x4a, 0x06, 0xe9);
	somagic_write_i2c(0x4a, 0x07, 0x0d);

	/* Subaddress 08h: d8 or 98 = auto NTSC/PAL, 58 = force NTSC, 18 = force PAL */
	somagic_write_i2c(0x4a, 0x08, 0x98);

	somagic_write_i2c(0x4a, 0x09, 0x01);
	somagic_write_i2c(0x4a, 0x0a, 0x80);
	somagic_write_i2c(0x4a, 0x0b, 0x40);
	somagic_write_i2c(0x4a, 0x0c, 0x40);
	somagic_write_i2c(0x4a, 0x0d, 0x00);
	somagic_write_i2c(0x4a, 0x0e, 0x81);
	somagic_write_i2c(0x4a, 0x0f, 0x2a);
	somagic_write_i2c(0x4a, 0x10, 0x40);
	somagic_write_i2c(0x4a, 0x11, 0x0c);
	somagic_write_i2c(0x4a, 0x12, 0x01);
	somagic_write_i2c(0x4a, 0x13, 0x80);
	somagic_write_i2c(0x4a, 0x14, 0x00);
	somagic_write_i2c(0x4a, 0x15, 0x00);
	somagic_write_i2c(0x4a, 0x16, 0x00);
	somagic_write_i2c(0x4a, 0x17, 0x00);
	somagic_write_i2c(0x4a, 0x40, 0x02);
	somagic_write_i2c(0x4a, 0x58, 0x00);

	/* Subaddress 59h: HOFF - horizontal offset */
	somagic_write_i2c(0x4a, 0x59, 0x54);

	/* Subaddress 5Ah: VOFF - vertical offset */
	somagic_write_i2c(0x4a, 0x5a, 0x07);

	/* Subaddress 5Bh: HVOFF - horizontal and vertical offset bits */
	somagic_write_i2c(0x4a, 0x5b, 0x03);

	/* Subaddress 5Eh: SDID codes. 0 = sets SDID5 to SDID0 active */
	somagic_write_i2c(0x4a, 0x5e, 0x00);

	somagic_write_i2c(0x4a, 0x02, 0xc0);
	somagic_write_i2c(0x4a, 0x0e, 0x01);

	/* Subaddress 40h: 00 for PAL, 80 for NTSC */
	if (tv_standard == PAL) {
		somagic_write_i2c(0x4a, 0x40, 0x00);
	} else {
		somagic_write_i2c(0x4a, 0x40, 0x80);
	}

	somagic_read_i2c(0x4a, 0x10, &status);
	fprintf(stderr,"i2c_read(0x10) = %02x\n", status);

	somagic_read_i2c(0x4a, 0x02, &status);
	fprintf(stderr,"i2c_stat(0x02) = %02x\n", status);

	/* Subaddress 5Ah: 07 for PAL, 0a for NTSC */
	if (tv_standard == PAL) {
		somagic_write_i2c(0x4a, 0x5a, 0x07);
	} else {
		somagic_write_i2c(0x4a, 0x5a, 0x0a);
	}

	somagic_write_i2c(0x4a, 0x59, 0x54);
	somagic_write_i2c(0x4a, 0x5b, 0x83);
	somagic_write_i2c(0x4a, 0x10, 0x40);
	somagic_write_i2c(0x4a, 0x55, 0xff);
	somagic_write_i2c(0x4a, 0x41, 0x77);
	somagic_write_i2c(0x4a, 0x42, 0x77);
	somagic_write_i2c(0x4a, 0x43, 0x77);
	somagic_write_i2c(0x4a, 0x44, 0x77);
	somagic_write_i2c(0x4a, 0x45, 0x77);
	somagic_write_i2c(0x4a, 0x46, 0x77);
	somagic_write_i2c(0x4a, 0x47, 0x77);
	somagic_write_i2c(0x4a, 0x48, 0x77);
	somagic_write_i2c(0x4a, 0x49, 0x77);
	somagic_write_i2c(0x4a, 0x4a, 0x77);
	somagic_write_i2c(0x4a, 0x4b, 0x77);
	somagic_write_i2c(0x4a, 0x4c, 0x77);
	somagic_write_i2c(0x4a, 0x4d, 0x77);
	somagic_write_i2c(0x4a, 0x4e, 0x77);
	somagic_write_i2c(0x4a, 0x4f, 0x77);
	somagic_write_i2c(0x4a, 0x50, 0x77);
	somagic_write_i2c(0x4a, 0x51, 0x77);
	somagic_write_i2c(0x4a, 0x52, 0x77);
	somagic_write_i2c(0x4a, 0x53, 0x77);
	somagic_write_i2c(0x4a, 0x54, 0x77);
	somagic_write_i2c(0x4a, 0x0a, 0x80);
	somagic_write_i2c(0x4a, 0x0b, 0x40);
	somagic_write_i2c(0x4a, 0x0d, 0x00);
	somagic_write_i2c(0x4a, 0x0c, 0x40);
	somagic_write_i2c(0x4a, 0x09, 0x01);

	somagic_write_reg(0x1740, 0x40);

	somagic_read_reg(0x3080, &status);
	fprintf(stderr, "status is %02x\n", status);

	somagic_write_reg(0x1740, 0x00);
	usleep(250 * 1000);
	somagic_write_reg(0x1740, 0x00);

	somagic_read_reg(0x3080, &status);
	fprintf(stderr, "status is %02x\n", status);

	memcpy(buf, "\x01\x05", 0x0000002);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x0000001, 0x0000000, buf, 0x0000002, 1000);
	fprintf(stderr, "190 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 0x0000109);
	fprintf(stderr, "191 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_set_interface_alt_setting(devh, 0, 2);
	fprintf(stderr, "192 set alternate setting returned %d\n", ret);

	somagic_write_reg(0x1740, 0x00);
	usleep(30 * 1000);
	
	for (i = 0; i < NUM_ISO_TRANSFERS; i++)	{
		tfr[i] = libusb_alloc_transfer(64);
		if (tfr[i] == NULL) {
			fprintf(stderr, "Failed to allocate USB transfer #%d\n", i);
			return 1;
		}
		libusb_fill_iso_transfer(tfr[i], devh, 0x00000082, isobuf[i], 64 * 3072, 64, gotdata, NULL, 2000);
		libusb_set_iso_packet_lengths(tfr[i], 3072);
	}
	
	pending_requests = NUM_ISO_TRANSFERS;
	for (i = 0; i < NUM_ISO_TRANSFERS; i++) {
		ret = libusb_submit_transfer(tfr[i]);
		if (ret != 0) {
			fprintf(stderr, "libusb_submit_transfer failed with error %d for transfer %d\n", ret, i);
		exit(1);
		}
	}
		
	somagic_write_reg(0x1800, 0x0d);

	while (pending_requests > 0) {
		libusb_handle_events(NULL);
	}
	
	for (i = 0; i < NUM_ISO_TRANSFERS; i++) {
		libusb_free_transfer(tfr[i]);
	}

	ret = libusb_release_interface(devh, 0);
	if (ret != 0) {
		perror("Failed to release interface");
		return 1;
	}
	libusb_close(devh);
	libusb_exit(NULL);
	return 0;
}
