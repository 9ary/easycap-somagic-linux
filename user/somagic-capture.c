/*******************************************************************************
 * somagic-capture.c                                                           *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60, EzCAP USB 2.0, and Somagic EasyCAP002  *
 * USB ID 1c88:003c, 1c88:003e, or 1c88:003f                                   *
 *                                                                             *
 * Initializes the Somagic EasyCAP registers and performs video capture.       *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Tony Brown, Michal Demin, Jeffry Johnston, Jon Arne Jørgensen
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
#ifdef DEBUG
#include <execinfo.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PROGRAM_NAME "somagic-capture"
#define VERSION "1.1"
#define VENDOR 0x1c88
#define PRODUCT_COUNT 4
static const int PRODUCT[PRODUCT_COUNT] = {
	0x003c,
	0x003d,
	0x003e,
	0x003f
};
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static char * program_path;

static int frames_generated = 0;
static int stop_sending_requests = 0;
static int pending_requests = 0;
static int lines_per_field;

static struct libusb_device_handle *devh;

enum tv_standards {
	NTSC,         /* 525/60 */
	PAL_60,       /* 525/60 */
	NTSC_60,      /* 525/60 */
	PAL_M,        /* 525/60 */
	PAL,          /* 625/50 */
	NTSC_50,      /* 525/50 (different) */
	PAL_COMBO_N,  /* 625/50 */
	NTSC_N,       /* 625/50 */
	SECAM,        /* 625/50 */
};

/* Input types */
#define	CVBS   0      /* DC60: "CVBS", 002: "2" */
#define	SVIDEO 7      /* DC60: "S-VIDEO" */

/* CVBS inputs */
#define	VIDEO1 2
#define	VIDEO2 3
#define	VIDEO3 0
#define	VIDEO4 1

/* Options */
/* Control the number of frames to generate: -1 = unlimited (default) */
static int frame_count = -1;

/* Television standard (see tv_standards) */
static int tv_standard = PAL;

/* Input type select (see Input types) */
static int input_type = CVBS;

/* CVBS input select */
static int cvbs_input = VIDEO3;

/* Luminance mode (CVBS only): 0 = 4.1 MHz, 1 = 3.8 MHz, 2 = 2.6 MHz, 3 = 2.9 MHz */
static int luminance_mode = 0;

/* Luminance prefilter: 0 = bypassed, 1 = active */
static int luminance_prefilter = 0;

/* Hue phase in degrees: -128 to 127 (-180 to 178.59375), increments of 1.40625 degrees */
static uint8_t hue = 0;

/* Chrominance saturation: -128 to 127 (1.984375 to -2.000000), increments of 0.015625 */
static uint8_t saturation = 64;

/* Luminance contrast: -128 to 127 (1.984375 to -2.000000), increments of 0.015625 */
static uint8_t contrast = 71;

/* Luminance brightness: 0 to 255 */
static uint8_t brightness = 128;

/* Luminance aperture factor: 0 = 0, 1 = 0.25, 2 = 0.5, 3 = 1.0 */
static int luminance_aperture = 1;

/* Video sync and processing algorithm: 1 (Tony Brown), 2 (Michal Demin) */
static int sync_algorithm = 2;

/* Video output file descriptor: 1 = stdout (default) */
static int video_fd = 1;

/* Control the number of concurrent ISO transfers we have running */
static int num_iso_transfers = 4;

/* Test-only mode (no capture): 0 = capture, 1 = test-only */
static int test_only = 0;

static void release_usb_device(int ret)
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

static struct libusb_device *find_device(int vendor, int product)
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

static void print_bytes(unsigned char *bytes, int len)
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

#ifdef DEBUG
static void print_bytes_only(char *bytes, int len)
{
	int i;
	if (len > 0) {
		for (i = 0; i < len; i++) {
			if (i % 32 == 0) {
				fprintf(stderr, "\n%04x\t ", i);
			}
			fprintf(stderr, "%02x ", (int)((unsigned char)bytes[i]));
		}
	}
}
#endif

#ifdef DEBUG
static void trace()
{
	void *array[10];
	size_t size;

	/* get void*'s for all entries on the stack */
	size = backtrace(array, 10);

	/* print out all the frames */
	backtrace_symbols_fd(array, size, 1);
	exit(1);
}
#endif

/*
 * Write a number of bytes from the iso transfer buffer to the appropriate line and field of the frame buffer.
 * Returns the number of bytes actually used from the buffer
 */
static int write_buffer(unsigned char *data, unsigned char *end, int count, unsigned char *frame, int line, int field)
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
	SYNCZ1,
	SYNCZ2,
	SYNCAV,
	VBLANK,
	VACTIVE,
	REMAINDER
};

struct alg1_video_state_t {
	int line_remaining;
	int active_line_count;
	int vblank_found;
	int field;

	enum sync_state state;

	unsigned char frame[720 * 2 * 288 * 2];
};

static struct alg1_video_state_t alg1_vs = { .line_remaining = 0, .active_line_count = 0, .vblank_found = 0, .field = 0, .state = HSYNC, .frame = { 0 } };

static void alg1_process(struct alg1_video_state_t *vs, unsigned char *buffer, int length)
{
	unsigned char *next = buffer;
	unsigned char *end = buffer + length;
	int bs = 0; /* bad (lost) sync: 0=no, 1=yes */
	int hs = 0;
	int lines_per_field = (tv_standard == PAL ? 288 : 240);
	unsigned char nc;
	int skip;
	int wrote;
	do {
		nc = *next;
		/*
		 * Timing reference code (TRC):
		 *     [ff 00 00 SAV] [ff 00 00 EAV]
		 * Where SAV is 80 or c7, and EAV is 9d or da.
		 * A line of video will look like (1448 bytes total):
		 *     [ff 00 00 EAV] [ff 00 00 SAV] [1440 bytes of UYVY video] (repeat on next line)
		 */
		switch (vs->state) {
			case HSYNC:
				hs++;
				if (nc == (unsigned char)0xff) {
					vs->state = SYNCZ1;
					if (bs == 1) {
						fprintf(stderr, "resync after %d @%td(%04tx)\n", hs, next - buffer, next - buffer);
					}
					bs = 0;
				} else if (bs != 1) {
					/*
					 * The 1st byte in the TRC must be 0xff. It
					 * wasn't, so sync was either lost or has not
					 * yet been regained. Sync is regained by
					 * ignoring bytes until the next 0xff.
					 */
					fprintf(stderr, "bad sync on line %d @%td (%04tx)\n", vs->active_line_count, next - buffer, next - buffer);
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
					vs->state = SYNCZ2;
				} else {
					/*
					 * The 2nd byte in the TRC must be 0x00. It
					 * wasn't, so sync was lost.
					 */
					vs->state = HSYNC;
				}
				next++;
				break;
			case SYNCZ2:
				if (nc == (unsigned char)0x00) {
					vs->state = SYNCAV;
				} else {
					/*
					 * The 3rd byte in the TRC must be 0x00. It
					 * wasn't, so sync was lost.
					 */
					vs->state = HSYNC;
				}
				next++;
				break;
			case SYNCAV:
				/*
				 * Found 0xff 0x00 0x00, now expecting SAV or EAV. Might
				 * also be the SDID (sliced data ID), 0x00.
				 */
				if (nc == (unsigned char)0x00) {
					/*
					 * SDID detected, so we still haven't found the
					 * active YUV data.
					 */
					vs->state = HSYNC;
					next++;
					break;
				}

				/*
				 * H = Bit 4 (mask 0x10).
				 * 0: in SAV, 1: in EAV.
				 */
				if (nc & (unsigned char)0x10) {
					/* EAV (end of active data) */
					vs->state = HSYNC;
				} else {
					/* SAV (start of active data) */
					/*
						* F (field bit) = Bit 6 (mask 0x40).
						* 0: first field, 1: 2nd field.
						*/
					vs->field = (nc & (unsigned char)0x40) ? 1 : 0;
					/*
						* V (vertical blanking bit) = Bit 5 (mask 0x20).
						* 1: in VBI, 0: in active video.
						*/
					if (nc & (unsigned char)0x20) {
						/* VBI (vertical blank) */
						vs->state = VBLANK;
						vs->vblank_found++;
						if (vs->active_line_count > (lines_per_field - 8)) {
							if (vs->field == 0) {
								if (frames_generated < frame_count || frame_count == -1) {
									write(video_fd, vs->frame, 720 * 2 * lines_per_field * 2);
									frames_generated++;
								}
								if (frames_generated >= frame_count && frame_count != -1) {
									stop_sending_requests = 1;
								}
							}
							vs->vblank_found = 0;
						}
						vs->active_line_count = 0;
					} else {
						/* Line is active video */
						vs->state = VACTIVE;
					}
					vs->line_remaining = 720 * 2;
				}
				next++;
				break;
			case VBLANK:
			case VACTIVE:
			case REMAINDER:
				if (vs->state == VBLANK || vs->vblank_found < 20) {
					skip = MIN(vs->line_remaining, (end - next));
					vs->line_remaining -= skip;
					next += skip ;
				} else {
					wrote = write_buffer(next, end, vs->line_remaining, vs->frame, vs->active_line_count, vs->field);
					vs->line_remaining -= wrote;
					next += wrote;
					if (vs->line_remaining <= 0) {
						vs->active_line_count++;
					}
				}
				if (vs->line_remaining <= 0) {
					vs->state = HSYNC;
				} else {
					vs->state = REMAINDER;
					/* no more data in this buffer. exit loop */
					next = end;
				}
				break;
		} /* end switch */
	} while (next < end);
}

struct alg2_video_state_t {
	uint16_t line;
	uint16_t col;

	enum sync_state state;

	uint8_t field;
	uint8_t blank;

	unsigned char frame[720 * 2 * 627 * 2];
};

static struct alg2_video_state_t alg2_vs = { .line = 0, .col = 0, .state = HSYNC, .field = 0, .blank = 0, .frame = { 0 } };

static void alg2_put_data(struct alg2_video_state_t *vs, uint8_t c)
{
	int line_pos;

	line_pos = (2 * vs->line + vs->field) * (720 * 2) + vs->col;
	vs->col++;

	/* sanity check */
	if (vs->col > 720 * 2)
		vs->col = 720 * 2;

	vs->frame[line_pos] = c;
}

static void alg2_process(struct alg2_video_state_t *vs, uint8_t c)
{
	/*
	 * Timing reference code (TRC):
	 *     [ff 00 00 SAV] [ff 00 00 EAV]
	 * A line of video will look like (1448 bytes total):
	 *     [ff 00 00 EAV] [ff 00 00 SAV] [1440 bytes of UYVY video] (repeat on next line)
	 */
	if (vs->state == HSYNC) {
		if (c == 0xff) {
			/* The 1st byte in the TRC must be 0xff. */
			vs->state++;
		} else {
			alg2_put_data(vs, c);
		}
	} else if (vs->state == SYNCZ1) {
		if (c == 0x00) {
			vs->state++;
		} else {
			/*
			 * The 2nd byte in the TRC must be 0x00. It
			 * wasn't, so sync was lost.
			 */
			vs->state = HSYNC;

			alg2_put_data(vs, 0xff);
			alg2_put_data(vs, c);
		}
	} else if (vs->state == SYNCZ2) {
		if (c == 0x00) {
			vs->state++;
		} else {
			/*
			 * The 3rd byte in the TRC must be 0x00. It
			 * wasn't, so sync was lost.
			 */
			vs->state = HSYNC;

			alg2_put_data(vs, 0xff);
			alg2_put_data(vs, 0x00);
			alg2_put_data(vs, c);
		}
	} else if (vs->state == SYNCAV) {
		/*
		 * Found 0xff 0x00 0x00, now expecting SAV or EAV. Might
		 * also be the SDID (sliced data ID), 0x00.
		 */
		vs->state = HSYNC;
		if (c == 0x00) {
			/*
			 * SDID (sliced data ID) detected, so active YUV data
			 * still hasn't been found.
			 */
			return;
		}

		/*
		 * H = Bit 4 (mask 0x10).
		 * 0: in SAV, 1: in EAV.
		 */
		if (c & 0x10) {
			/* EAV (end of active data) */
			if (!vs->blank) {
				vs->line++;
				vs->col = 0;
				if (vs->line > 625) vs->line = 625; /* sanity check */
			}
		} else {
			int field_edge;
			int blank_edge;

			/* SAV (start of active data) */
			/*
			 * F (field bit) = Bit 6 (mask 0x40).
			 * 0: first field, 1: 2nd field.
			 *
			 * V (vertical blanking bit) = Bit 5 (mask 0x20).
			 * 0: in VBI, 1: in active video.
			 */
			field_edge = vs->field;
			blank_edge = vs->blank;

			vs->field = (c & 0x40) ? 1 : 0;
			vs->blank = (c & 0x20) ? 1 : 0;

			field_edge = vs->field ^ field_edge;
			blank_edge = vs->blank ^ blank_edge;

			if (vs->field == 0 && field_edge) {
				if (frames_generated < frame_count || frame_count == -1) {
					write(video_fd, vs->frame, 720 * 2 * lines_per_field * 2);
					frames_generated++;
				}
				if (frames_generated >= frame_count && frame_count != -1) {
					stop_sending_requests = 1;
				}
			}

			if (vs->blank == 0 && blank_edge) {
				vs->line = 0;
				vs->col = 0;
			}
		}
	}
}

static void gotdata(struct libusb_transfer *tfr)
{
	int ret;
	int num = tfr->num_iso_packets;
	int i;
	unsigned char *data;
	int length;
	int pos;
	int k;

	pending_requests--;

	for (i = 0; i < num; i++) {
		data = libusb_get_iso_packet_buffer_simple(tfr, i);
		length = tfr->iso_packet_desc[i].actual_length;
		pos = 0;

		while (pos < length) {
			/*
			 * Within each packet of the transfer, the video data is divided
			 * into blocks of 0x400 bytes beginning with [0xaa 0xaa 0x00 0x00].
			 * Check for this signature and process each block of data individually.
			 */
			if (data[pos] == 0xaa && data[pos + 1] == 0xaa && data[pos + 2] == 0x00 && data[pos + 3] == 0x00) {
				/* Process received video data, excluding the 4 marker bytes */
				switch (sync_algorithm) {
				case 1:
					alg1_process(&alg1_vs, data + 4 + pos, 0x400 - 4);
					break;
				case 2:
					for (k = 4; k < 0x400; k++) {
						alg2_process(&alg2_vs, data[k + pos]);
					}
					break;
				}
			} else {
				fprintf(stderr, "Unexpected block, expected [aa aa 00 00] found [%02x %02x %02x %02x]\n", data[pos], data[pos + 1], data[pos + 2], data[pos + 3]);
			}
			pos += 0x400;
		}
	}

	if (!stop_sending_requests) {
		ret = libusb_submit_transfer(tfr);
		if (ret) {
			fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
			exit(1);
		}
		pending_requests++;
	}
}

static int somagic_write_reg(uint16_t reg, uint8_t val)
{
	int ret;
	uint8_t buf[8];

	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3a\x00", 8);
	buf[5] = reg >> 8;
	buf[6] = reg & 0xff;
	buf[7] = val;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 8, 1000);
	if (ret != 8) {
		fprintf(stderr, "write reg control msg returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}

	return ret;
}

static int somagic_write_i2c(uint8_t dev_addr, uint8_t reg, uint8_t val)
{
	int ret;
	uint8_t buf[8];

	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x01\x08\xf4", 8);

	buf[1] = dev_addr;
	buf[5] = reg;
	buf[6] = val;

	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 8, 1000);
	if (ret != 8) {
		fprintf(stderr, "write_i2c returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}

	return ret;
}

static int somagic_capture()
{
	int ret;
	int i = 0;

	/* buffers and transfer pointers for isochronous data */
	struct libusb_transfer **tfr;
	unsigned char (*isobuf)[64 * 3072];

	/* Allocate memory for tfr and isobuf */
	tfr = malloc(num_iso_transfers * sizeof *tfr);
	if (tfr == NULL) {
		perror("Failed to allocate memory for tfr");
		return 1;
	}
	isobuf = malloc(num_iso_transfers * sizeof *isobuf);
	if (isobuf == NULL) {
		perror("Failed to allocate memory for isobuf");
		return 1;
	}

	if (!test_only) {
		for (i = 0; i < num_iso_transfers; i++)	{
			tfr[i] = libusb_alloc_transfer(64);
			if (tfr[i] == NULL) {
				fprintf(stderr, "%s: Failed to allocate USB transfer #%d: %s\n", program_path, i, strerror(errno));
				return 1;
			}
			libusb_fill_iso_transfer(tfr[i], devh, 0x00000082, isobuf[i], 64 * 3072, 64, gotdata, NULL, 2000);
			libusb_set_iso_packet_lengths(tfr[i], 3072);
		}

		pending_requests = num_iso_transfers;
		for (i = 0; i < num_iso_transfers; i++) {
			ret = libusb_submit_transfer(tfr[i]);
			if (ret) {
				fprintf(stderr, "%s: Failed to submit request #%d for transfer: %s\n", program_path, i, strerror(errno));
				return 1;
			}
		}

		somagic_write_reg(0x1800, 0x0d);

		while (pending_requests > 0) {
			libusb_handle_events(NULL);
		}

		for (i = 0; i < num_iso_transfers; i++) {
			libusb_free_transfer(tfr[i]);
		}
	}

	ret = libusb_release_interface(devh, 0);
	if (ret) {
		perror("Failed to release interface");
		return 1;
	}
	libusb_close(devh);
	libusb_exit(NULL);

	/* Close video output file */
	if (video_fd != 1) {
		ret = close(video_fd);
		if (ret) {
			perror("Failed to close video output file");
			return 1;
		}
	}

	return 0;
}

static int somagic_init()
{
	int p;
	int ret;
	struct libusb_device *dev;
	uint8_t work;

	/* buffer for control messages */
	unsigned char buf[65535];

	libusb_init(NULL);
	libusb_set_debug(NULL, 0);

	for (p = 0; p < PRODUCT_COUNT; p++) {
		dev = find_device(VENDOR, PRODUCT[p]);
		if (dev) {
			break;
		}
	}
	if (p >= PRODUCT_COUNT) {
		for (p = 0; p < PRODUCT_COUNT; p++) {
			fprintf(stderr, "USB device %04x:%04x was not found.\n", VENDOR, PRODUCT[p]);
		}
		fprintf(stderr, "Has device initialization been performed?\n");
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
	if (ret) {
		perror("Failed to claim device interface");
		if (ret == LIBUSB_ERROR_BUSY) {
			fprintf(stderr, "Is "PROGRAM_NAME" already running?\n");
		}
		return 1;
	}

	ret = libusb_set_interface_alt_setting(devh, 0, 0);
	if (ret) {
		perror("Failed to set active alternate setting for interface");
		return 1;
	}

	ret = libusb_get_descriptor(devh, 0x0000001, 0x0000000, buf, 18);
	if (ret != 18) {
		fprintf(stderr, "1 get descriptor returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 9);
	if (ret != 9) {
		fprintf(stderr, "2 get descriptor returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 66);
	/*
	fprintf(stderr, "3 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	*/

	ret = libusb_release_interface(devh, 0);
	if (ret) {
		perror("Failed to release interface (before set_configuration)");
		return 1;
	}
	ret = libusb_set_configuration(devh, 0x0000001);
	if (ret) {
		perror("Failed to set active device configuration");
		return 1;
	}
	ret = libusb_claim_interface(devh, 0);
	if (ret) {
		perror("Failed to claim device interface (after set_configuration)");
		return 1;
	}
	ret = libusb_set_interface_alt_setting(devh, 0, 0);
	if (ret) {
		perror("Failed to set active alternate setting for interface (after set_configuration)");
		return 1;
	}
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x0000001, 0x0000000, buf, 2, 1000);
	if (ret != 2) {
		fprintf(stderr, "5 control msg returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}

	/*
	 * AVR Documentation @ http://www.avr-asm-tutorial.net/avr_en/beginner/PDETAIL.html#IOPORTS
	 *
 	 * Reg 0x3a should be DDRA.
 	 * (DDRA = PortA Data Direction Register)
 	 * By setting this to 0x80, we set PIN7 to output.
 	 *
 	 * I assume that this PIN is connected to the RESET pin of the
 	 * SAA7XXX & CS5340.
 	 *
 	 * If we leave this PIN in HIGH, or don't set it to OUTPUT
 	 * we can not receive Stereo Audio from the CS5340.
 	 *
 	 * Reg 0x3b should be PORTA.
 	 * (PortA = PortA Data Register)
 	 * By setting this to 0x00, we pull Pin7 LOW
 	 */
	somagic_write_reg(0x3a, 0x80);
	somagic_write_reg(0x3b, 0x00);

	/*
 	 * Reg 0x34 should be DDRC
 	 * Reg 0x35 should be PORTC.
 	 *
 	 * This PORT seems to only be used in the Model002!
 	 */
	somagic_write_reg(0x34, 0x01);
	somagic_write_reg(0x35, 0x00);
	somagic_write_reg(0x34, 0x11);
	somagic_write_reg(0x35, 0x11);

	/* SAAxxx: toggle RESET (PIN7) */
	somagic_write_reg(0x3b, 0x80);
	somagic_write_reg(0x3b, 0x00);

	/* Subaddress 0x01, Horizontal Increment delay */
	/* Recommended position */
	somagic_write_i2c(0x4a, 0x01, 0x08);

	/* Subaddress 0x02, Analog input control 1 */
	/* Analog function select FUSE = Amplifier plus anti-alias filter bypassed */
	/* Update hysteresis for 9-bit gain = Off */
	if (input_type == CVBS) {
		work = 0xc0 | cvbs_input;
	} else {
		work = 0xc0 | input_type;
	}
	somagic_write_i2c(0x4a, 0x02, work);

	/* Subaddress 0x03, Analog input control 2 */
	if (input_type != SVIDEO) {
		/* Static gain control channel 1 (GAI18), sign bit of gain control = 1 */
		/* Static gain control channel 2 (GAI28), sign bit of gain control = 1 */
		/* Gain control fix (GAFIX) = Automatic gain controlled by MODE3 to MODE0 */
		/* Automatic gain control integration (HOLDG) = AGC active */
		/* White peak off (WPOFF) = White peak off */
		/* AGC hold during vertical blanking period (VBSL) = Long vertical blanking (AGC disabled from start of pre-equalization pulses until start of active video (line 22 for 60 Hz, line 24 for 50 Hz) */
		/* Normal clamping if decoder is in unlocked state */
		somagic_write_i2c(0x4a, 0x03, 0x33);
	} else {
		/* Static gain control channel 1 (GAI18), sign bit of gain control = 1 */
		/* Static gain control channel 2 (GAI28), sign bit of gain control = 0 */
		/* Gain control fix (GAFIX) = Automatic gain controlled by MODE3 to MODE0 */
		/* Automatic gain control integration (HOLDG) = AGC active */
		/* White peak off (WPOFF) = White peak off */
		/* AGC hold during vertical blanking period (VBSL) = Long vertical blanking (AGC disabled from start of pre-equalization pulses until start of active video (line 22 for 60 Hz, line 24 for 50 Hz) */
		/* Normal clamping if decoder is in unlocked state */
		somagic_write_i2c(0x4a, 0x03, 0x31);
	}

	/* Subaddress 0x04, Gain control analog/Analog input control 3 (AICO3); static gain control channel 1 GAI1 */
	/* Gain (dB) = -3 (Note: Dependent on subaddress 0x03 GAI18 value) */
	somagic_write_i2c(0x4a, 0x04, 0x00);

	/* Subaddress 0x05, Gain control analog/Analog input control 4 (AICO4); static gain control channel 2 GAI2 */
	/* Gain (dB) = -3 (Note: Dependent on subaddress 0x03 GAI28 value) */
	somagic_write_i2c(0x4a, 0x05, 0x00);

	/* Subaddress 0x06, Horizontal sync start/begin */
	/* Delay time (step size = 8/LLC) = Recommended value for raw data type */
	somagic_write_i2c(0x4a, 0x06, 0xe9);

	/* Subaddress 0x07, Horizontal sync stop */
	/* Delay time (step size = 8/LLC) = Recommended value for raw data type */
	somagic_write_i2c(0x4a, 0x07, 0x0d);

	/* Subaddress 0x08, Sync control */
	/* Automatic field detection (AUFD) = Automatic field detection */
	/* Field selection (FSEL) = 50 Hz, 625 lines (Note: Ignored due to automatic field detection) */
	/* Forced ODD/EVEN toggle FOET = ODD/EVEN signal toggles only with interlaced source */
	/* Horizontal time constant selection = Fast locking mode (recommended setting) */
	/* Horizontal PLL (HPLL) = PLL closed */
	/* Vertical noise reduction (VNOI) = Normal mode (recommended setting) */
	somagic_write_i2c(0x4a, 0x08, 0x98);

	/* Subaddress 0x09, Luminance control */
	/* Update time interval for analog AGC value (UPTCV) = Horizontal update (once per line) */
	/* Vertical blanking luminance bypass (VBLB) = Active luminance processing */
	/* Chrominance trap bypass (BYPS) = Chrominance trap active; default for CVBS mode */
	work = ((luminance_prefilter & 0x01) << 6) | ((luminance_mode & 0x03) << 4) | (luminance_aperture & 0x03);
	if (input_type == SVIDEO) {
		/* Chrominance trap bypass (BYPS) = Chrominance trap bypassed; default for S-video mode */
		work |= 0x80;
	}
	somagic_write_i2c(0x4a, 0x09, work);

	/* Subaddress 0x0a, Luminance brightness control */
	/* Offset = 128 (ITU level) */
	somagic_write_i2c(0x4a, 0x0a, brightness);

	/* Subaddress 0x0b, Luminance contrast control */
	/* Gain = 1.0 */
	somagic_write_i2c(0x4a, 0x0b, contrast);

	/* Subaddress 0x0c, Chrominance saturation control */
	somagic_write_i2c(0x4a, 0x0c, saturation);

	/* Subaddress 0x0d, Chrominance hue control */
	somagic_write_i2c(0x4a, 0x0d, hue);

	/* Subaddress 0x0e, Chrominance control */
	/* Chrominance bandwidth (CHBW0 and CHBW1) = Nominal bandwidth (800 kHz) */
	/* Fast color time constant (FCTC) = Nominal time constant */
	/* Disable chrominance comb filter (DCCF) = Chrominance comb filter on (during lines determined by VREF = 1) */
	/* Clear DTO (CDTO) = Disabled */
	switch (tv_standard) {
	case PAL:
	case NTSC:
		work = 0x01;
		break;
	case NTSC_50:
	case PAL_60:
		work = 0x11;
		break;
	case PAL_COMBO_N:
	case NTSC_60:
		work = 0x21;
		break;
	case NTSC_N:
	case PAL_M:
		work = 0x31;
		break;
	case SECAM:
		work = 0x50;
		break;
	}
	somagic_write_i2c(0x4a, 0x0e, work);

	/* Subaddress 0x0f, Chrominance gain control */
	/* Chrominance gain value = ??? (Note: only meaningful if ACGF is off) */
	/* Automatic chrominance gain control ACGC = On */
	somagic_write_i2c(0x4a, 0x0f, 0x2a);

	/* Subaddress 0x10, Format/delay control */
	/* Output format selection (OFTS0 and OFTS1), V-flag generation in SAV/EAV-codes = V-flag in SAV/EAV is generated by VREF */
	/* Fine position of HS (HDEL0 and HDEL1) (steps in 2/LLC) = 0 */
	/* VREF pulse position and length (VRLN) = see Table 46 in SAA7113H documentation */
	/* Luminance delay compensation (steps in 2/LLC) = 0 */
	somagic_write_i2c(0x4a, 0x10, 0x40);

	/* Subaddress 0x11, Output control 1 */
	/* General purpose switch [available on pin RTS1, if control bits RTSE13 to RTSE10 (subaddress 0x12) is set to 0010] = LOW */
	/* CM99 compatibility to SAA7199 (CM99) = Default value */
	/* General purpose switch [available on pin RTS0, if control bits RTSE03 to RTSE00 (subaddress 0x12) is set to 0010] = LOW */
	/* Selection of horizontal lock indicator for RTS0 and RTS1 outputs = Standard horizontal lock indicator (low-passed) */
	/* Output enable YUV data (OEYC) = Output VPO-bus active or controlled by RTS1 */
	/* Output enable real-time (OERT) = RTS0, RTCO active, RTS1 active, if RTSE13 to RTSE10 = 0000 */
	/* YUV decoder bypassed (VIPB) = Processed data to VPO output */
	/* Color on (COLO) = Automatic color killer */
	somagic_write_i2c(0x4a, 0x11, 0x0c);

	/* Subaddress 0x12, RTS0 output control/Output control 2 */
	/* RTS1 output control = 3-state, pin RTS1 is used as DOT input */
	/* RTS0 output control = VIPB (subaddress 0x11, bit 1) = 0: reserved */
	somagic_write_i2c(0x4a, 0x12, 0x01);

	/* Subaddress 0x13, Output control 3 */
	if (input_type != SVIDEO) {
		/* Analog-to-digital converter output bits on VPO7 to VPO0 in bypass mode (VIPB = 1, used for test purposes) (ADLSB) = AD7 to AD0 (LSBs) on VPO7 to VPO0 */
		/* Selection bit for status byte functionality (OLDSB) = Default status information */
		/* Field ID polarity if selected on RTS1 or RTS0 outputs if RTSE1 and RTSE0 (subaddress 0x12) are set to 1111 = Default */
		/* Analog test select (AOSL) = AOUT connected to internal test point 1 */
		somagic_write_i2c(0x4a, 0x13, 0x80);
	} else {
		/* Analog-to-digital converter output bits on VPO7 to VPO0 in bypass mode (VIPB = 1, used for test purposes) (ADLSB) = AD8 to AD1 (MSBs) on VPO7 to VPO0 */
		/* Selection bit for status byte functionality (OLDSB) = Default status information */
		/* Field ID polarity if selected on RTS1 or RTS0 outputs if RTSE1 and RTSE0 (subaddress 0x12) are set to 1111 = Default */
		/* Analog test select (AOSL) = AOUT connected to internal test point 1 */
		somagic_write_i2c(0x4a, 0x13, 0x00);
	}

	/* Subaddress 0x15, Start of VGATE pulse (01-transition) and polarity change of FID pulse/V_GATE1_START */
	/* Note: Dependency on subaddress 0x17 value */
	/* Frame line counting = If 50Hz: 1st = 2, 2nd = 315. If 60Hz: 1st = 5, 2nd = 268. */
	somagic_write_i2c(0x4a, 0x15, 0x00);

	/* Subaddress 0x16, Stop of VGATE pulse (10-transition)/V_GATE1_STOP */
	/* Note: Dependency on subaddress 0x17 value */
	/* Frame line counting = If 50Hz: 1st = 2, 2nd = 315. If 60Hz: 1st = 5, 2nd = 268. */
	somagic_write_i2c(0x4a, 0x16, 0x00);

	/* Subaddress 0x17, VGATE MSBs/V_GATE1_MSB */
	/* VSTA8, MSB VGATE start = 0 */
	/* VSTO8, MSB VGATE stop = 0 */
	somagic_write_i2c(0x4a, 0x17, 0x00);

	/* Subaddress 0x40, AC1 */
	if (tv_standard == NTSC || tv_standard == PAL_60 || tv_standard == NTSC_60 || tv_standard == PAL_M) {
		/* Data slicer clock selection, Amplitude searching = 13.5 MHz (default) */
		/* Amplitude searching = Amplitude searching active (default) */
		/* Framing code error = One framing code error allowed */
		/* Hamming check = Hamming check for 2 bytes after framing code, dependent on data type (default) */
		/* Field size select = 60 Hz field rate */
		somagic_write_i2c(0x4a, 0x40, 0x82);
	} else {
		/* Data slicer clock selection, Amplitude searching = 13.5 MHz (default) */
		/* Amplitude searching = Amplitude searching active (default) */
		/* Framing code error = One framing code error allowed */
		/* Hamming check = Hamming check for 2 bytes after framing code, dependent on data type (default) */
		/* Field size select = 50 Hz field rate */
		somagic_write_i2c(0x4a, 0x40, 0x02);
	}

	if (input_type != SVIDEO) {
		/* LCR register 2 to 24 = Intercast, oversampled CVBS data */
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
		/* LCR register 2 to 24 = Active video, video component signal, active video region (default) */
		somagic_write_i2c(0x4a, 0x55, 0xff);
	}

	/* Subaddress 0x58, Framing code for programmable data types/FC */
	/* Slicer set, Programmable framing code = ??? */
	somagic_write_i2c(0x4a, 0x58, 0x00);

	/* Subaddress 0x59, Horizontal offset/HOFF */
	/* Slicer set, Horizontal offset = Recommended value */
	somagic_write_i2c(0x4a, 0x59, 0x54);

	/* Subaddress 0x5a: Vertical offset/VOFF */
	if (tv_standard == PAL || tv_standard == PAL_COMBO_N || tv_standard == NTSC_N || tv_standard == SECAM) {
		/* Slicer set, Vertical offset = Value for 625 lines input */
		somagic_write_i2c(0x4a, 0x5a, 0x07);
		lines_per_field = 288;
	} else {
		/* Slicer set, Vertical offset = Value for 525 lines input */
		somagic_write_i2c(0x4a, 0x5a, 0x0a);
		lines_per_field = 240;
	}

	/* Subaddress 0x5b, Field offset, MSBs for vertical and horizontal offsets/HVOFF */
	/* Slicer set, Field offset = Invert field indicator (even/odd; default) */
	somagic_write_i2c(0x4a, 0x5b, 0x83);

	/* Subaddress 0x5e, SDID codes */
	/* Slicer set, SDID codes = SDID5 to SDID0 = 0x00 (default) */
	somagic_write_i2c(0x4a, 0x5e, 0x00);

	somagic_write_reg(0x1740, 0x40);

	somagic_write_reg(0x1740, 0x00);
	usleep(250 * 1000);
	somagic_write_reg(0x1740, 0x00);

	memcpy(buf, "\x01\x05", 2);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x0000001, 0x0000000, buf, 2, 1000);
	if (ret != 2) {
		fprintf(stderr, "190 control msg returned %d, bytes: ", ret);
		print_bytes(buf, ret);
		fprintf(stderr, "\n");
	}
	ret = libusb_get_descriptor(devh, 0x0000002, 0x0000000, buf, 265);
	/*
	fprintf(stderr, "191 get descriptor returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	*/

	ret = libusb_set_interface_alt_setting(devh, 0, 2);
	if (ret != 0) {
		perror("Failed to activate alternate setting for interface");
		return 1;
	}

	/* Disable sound - If this line is removed, we start to receive data with the header [0xaa 0xaa 0x00 0x01] */
	somagic_write_reg(0x1740, 0x00);
	usleep(30 * 1000);

	return 0;
}

static void version()
{
	fprintf(stderr, PROGRAM_NAME" "VERSION"\n");
	fprintf(stderr, "Copyright 2011-2013 Tony Brown, Michal Demin, Jeffry Johnston,\n");
	fprintf(stderr, "                     Jon Arne Jørgensen\n");
	fprintf(stderr, "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n");
	fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
	fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n");
}

static void usage()
{
        /*               00000000011111111112222222222333333333344444444445555555555666666666677777777778 */
        /*               12345678901234567890123456789012345678901234567890123456789012345678901234567890 */
	fprintf(stderr, "Usage: "PROGRAM_NAME" [options]\n");
	fprintf(stderr, "  -B, --brightness=VALUE     Luminance brightness control,\n");
	fprintf(stderr, "                             0 to 255 (default: 128)\n");
	fprintf(stderr, "                             Value  Brightness\n");
	fprintf(stderr, "                               255  Bright\n");
	fprintf(stderr, "                               149  NTSC-J\n");
	fprintf(stderr, "                               128  ITU level (default)\n");
	fprintf(stderr, "                                 0  Dark\n");
	fprintf(stderr, "  -C, --contrast=VALUE       Luminance contrast control,\n");
	fprintf(stderr, "                             -128 to 127 (default: 71)\n");
	fprintf(stderr, "                             Value  Contrast\n");
	fprintf(stderr, "                               127   1.984375\n");
	fprintf(stderr, "                                72   1.125000 (NTSC-J)\n");
	fprintf(stderr, "                                71   1.109375 (ITU level, default)\n");
	fprintf(stderr, "                                64   1.000000\n");
	fprintf(stderr, "                                 1   0.015625\n");
	fprintf(stderr, "                                 0   0.000000 (luminance off)\n");
	fprintf(stderr, "                               -64  -1.000000 (inverse)\n");
	fprintf(stderr, "                              -128  -2.000000 (inverse)\n");
	fprintf(stderr, "  -c, --cvbs                 Use CVBS (composite) input on the EasyCAP DC60\n");
	fprintf(stderr, "                             and EzCAP USB 2.0, numbered inputs on the\n");
	fprintf(stderr, "                             EasyCAP002 (default)\n");
 	fprintf(stderr, "  -i, --cvbs-input=VALUE     Select CVBS (composite) input to use, 1 to 4,\n");
	fprintf(stderr, "                             EasyCAP002 only (default: 3)\n");
	fprintf(stderr, "  -f, --frames=COUNT         Number of frames to generate,\n");
	fprintf(stderr, "                             -1 for unlimited (default: -1)\n");
	fprintf(stderr, "  -H, --hue=VALUE            Hue phase in degrees, -128 to 127 (default: 0),\n");
	fprintf(stderr, "                             Value  Phase\n");
	fprintf(stderr, "                              -128  -180.00000\n");
	fprintf(stderr, "                                 0     0.00000\n");
	fprintf(stderr, "                                 1     1.40635\n");
	fprintf(stderr, "                               127   178.59375\n");
	fprintf(stderr, "      --iso-transfers=COUNT  Number of concurrent iso transfers (default: 4)\n");
	fprintf(stderr, "      --lum-aperture=MODE    Luminance aperture factor (default: 1)\n");
	fprintf(stderr, "                             Mode  Aperture Factor\n");
	fprintf(stderr, "                                0  0.00\n");
	fprintf(stderr, "                                1  0.25 (default)\n");
	fprintf(stderr, "                                2  0.50\n");
	fprintf(stderr, "                                3  1.00\n");
	fprintf(stderr, "      --lum-prefilter        Activate luminance prefilter (default: bypassed)\n");
	fprintf(stderr, "      --luminance=MODE       CVBS luminance mode (default: 0)\n");
	fprintf(stderr, "                             Mode  Center Frequency\n");
	fprintf(stderr, "                                0  4.1 MHz (default)\n");
	fprintf(stderr, "                                1  3.8 MHz\n");
	fprintf(stderr, "                                2  2.6 MHz\n");
	fprintf(stderr, "                                3  2.9 MHz\n");
	fprintf(stderr, "  -n, --ntsc                 NTSC-M (North America) / NTSC-J (Japan)\n");
	fprintf(stderr, "                                               [525 lines, 29.97 Hz]\n");
	fprintf(stderr, "      --ntsc-4.43-50         NTSC-4.43 50Hz    [525 lines, 25 Hz]\n");
	fprintf(stderr, "      --ntsc-4.43-60         NTSC-4.43 60Hz    [525 lines, 29.97 Hz]\n");
	fprintf(stderr, "      --ntsc-n               NTSC-N            [625 lines, 25 Hz]\n");
	fprintf(stderr, "  -p, --pal                  PAL-B/G/H/I/N     [625 lines, 25 Hz] (default)\n");
	fprintf(stderr, "      --pal-4.43             PAL-4.43 / PAL 60 [525 lines, 29.97 Hz]\n");
	fprintf(stderr, "      --pal-m                PAL-M (Brazil)    [525 lines, 29.97 Hz]\n");
	fprintf(stderr, "      --pal-combination-n    PAL Combination-N [625 lines, 25 Hz]\n");
	fprintf(stderr, "  -S, --saturation=VALUE     Chrominance saturation control,\n");
	fprintf(stderr, "                             -128 to 127 (default: 64)\n");
	fprintf(stderr, "                             Value  Saturation\n");
	fprintf(stderr, "                               127   1.984375\n");
	fprintf(stderr, "                                64   1.000000 (ITU level, default)\n");
	fprintf(stderr, "                                 1   0.015625\n");
	fprintf(stderr, "                                 0   0.000000 (color off)\n");
	fprintf(stderr, "                               -64  -1.000000 (inverse)\n");
	fprintf(stderr, "                              -128  -2.000000 (inverse)\n");
	fprintf(stderr, "  -s, --s-video              Use S-VIDEO input, EasyCAP DC60 and EzCAP USB 2.0\n");
	fprintf(stderr, "                             only\n");
	fprintf(stderr, "      --secam                SECAM             [625 lines, 25 Hz]\n");
	fprintf(stderr, "      --sync=VALUE           Sync algorithm (default: 2)\n");
	fprintf(stderr, "                             Value  Algorithm\n");
	fprintf(stderr, "                                 1  TB\n");
	fprintf(stderr, "                                 2  MD (default)\n");
	fprintf(stderr, "      --test-only            Perform capture setup, but do not capture\n");
	fprintf(stderr, "      --vo=FILENAME          Raw UYVY video output file (or pipe) filename\n");
	fprintf(stderr, "                             (default is standard output)\n");
	fprintf(stderr, "      --help                 Display usage\n");
	fprintf(stderr, "      --version              Display version information\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Examples (run as root):\n");
	fprintf(stderr, "# PAL, CVBS/composite:\n");
	fprintf(stderr, PROGRAM_NAME" | mplayer -vf yadif,screenshot -demuxer rawvideo -rawvideo \"pal:format=uyvy:fps=25\" -aspect 4:3 -\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "# NTSC, S-VIDEO\n");
	fprintf(stderr, PROGRAM_NAME" -n -s | mplayer -vf yadif,screenshot -demuxer rawvideo -rawvideo \"ntsc:format=uyvy:fps=30000/1001\" -aspect 4:3 -\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "# NTSC, CVBS/composite, increased sharpness:\n");
	fprintf(stderr, PROGRAM_NAME" -n --luminance=2 --lum-aperture=3 | mplayer -vf yadif,screenshot -demuxer rawvideo -rawvideo \"ntsc:format=uyvy:fps=30000/1001\" -aspect 4:3 -\n");
}

static int parse_cmdline(int argc, char **argv) {
	int c;
	int i = 0;
	int option_index = 0;
	static struct option long_options[] = {
		{"help", 0, 0, 0},              /* index 0  */
		{"iso-transfers", 1, 0, 0},     /* index 1  */
		{"lum-aperture", 1, 0, 0},      /* index 2  */
		{"lum-prefilter", 0, 0, 0},     /* index 3  */
		{"luminance", 1, 0, 0},         /* index 4  */
		{"ntsc-4.43-50", 0, 0, 0},      /* index 5  */
		{"ntsc-4.43-60", 0, 0, 0},      /* index 6  */
		{"ntsc-n", 0, 0, 0},            /* index 7  */
		{"pal-4.43", 0, 0, 0},          /* index 8  */
		{"pal-m", 0, 0, 0},             /* index 9  */
		{"pal-combination-n", 0, 0, 0}, /* index 10 */
		{"secam", 0, 0, 0},             /* index 11 */
		{"sync", 1, 0, 0},              /* index 12 */
		{"test-only", 0, 0, 0},         /* index 13 */
		{"version", 0, 0, 0},           /* index 14 */
		{"vo", 1, 0, 0},                /* index 15 */
		{"brightness", 1, 0, 'B'},
		{"cvbs", 0, 0, 'c'},
		{"cvbs-input", 1, 0, 'i'},
		{"contrast", 1, 0, 'C'},
		{"frame-count", 1, 0, 'f'},
		{"hue", 1, 0, 'H'},
		{"ntsc", 0, 0, 'n'},
		{"pal", 0, 0, 'p'},
		{"s-video", 0, 0, 's'},
		{"saturation", 1, 0, 'S'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long(argc, argv, "B:cC:f:H:i:npsS:", long_options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 0:
			switch (option_index) {
			case 0: /* --help */
				usage();
				exit(0);
			case 1: /* --iso-transfers */
				num_iso_transfers = atoi(optarg);
				if (num_iso_transfers < 1) {
					fprintf(stderr, "Invalid iso transfers count '%i', must be at least 1\n", num_iso_transfers);
					return 1;
				}
				break;
			case 2: /* --lum-aperture */
				luminance_aperture = atoi(optarg);
				if (luminance_aperture < 0 || luminance_aperture > 3) {
					fprintf(stderr, "Invalid luminance aperture '%i', must be from 0 to 3\n", luminance_mode);
					return 1;
				}
				break;
			case 3: /* --lum-prefilter */
				luminance_prefilter = 1;
				break;
			case 4: /* --luminance */
				luminance_mode = atoi(optarg);
				if (luminance_mode < 0 || luminance_mode > 3) {
					fprintf(stderr, "Invalid luminance mode '%i', must be from 0 to 3\n", luminance_mode);
					return 1;
				}
				break;
			case 5: /* --ntsc-4.43-50 */
				tv_standard = NTSC_50;
				break;
			case 6: /* --ntsc-4.43-60 */
				tv_standard = NTSC_60;
				break;
			case 7: /* --ntsc-n */
				tv_standard = NTSC_N;
				break;
			case 8: /* --pal-4.43 */
				tv_standard = PAL_60;
				break;
			case 9: /* --pal-m */
				tv_standard = PAL_M;
				break;
			case 10: /* --pal-combination-n */
				tv_standard = PAL_COMBO_N;
				break;
			case 11: /* --secam */
				tv_standard = SECAM;
				break;
			case 12: /* --sync */
				sync_algorithm = atoi(optarg);
				if (sync_algorithm < 1 || sync_algorithm > 2) {
					fprintf(stderr, "Invalid sync algorithm '%i', must be from 1 to 2\n", sync_algorithm);
					return 1;
				}
				break;
			case 13: /* --test-only */
				test_only = 1;
				break;
			case 14: /* --version */
				version();
				exit(0);
			case 15: /* --vo */
				video_fd = open(optarg, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
				if (video_fd == -1) {
					fprintf(stderr, "%s: Failed to open video output file '%s': %s\n", program_path, optarg, strerror(errno));
					return 1;
				}
				break;
			default:
				usage();
				return 1;
			}
			break;
		case 'B':
			i = atoi(optarg);
			if (i < 0 || i > 255) {
				fprintf(stderr, "Invalid brightness value '%i', must be from 0 to 255\n", i);
				return 1;
			}
			brightness = i;
			break;
		case 'c':
			input_type = CVBS;
			break;
		case 'C':
			i = atoi(optarg);
			if (i < -128 || i > 127) {
				fprintf(stderr, "Invalid contrast value '%i', must be from -128 to 127\n", i);
				return 1;
			}
			contrast = (int8_t)i;
			break;
		case 'f':
			frame_count = atoi(optarg);
			break;
		case 'H':
			i = atoi(optarg);
			if (i < -128 || i > 127) {
				fprintf(stderr, "Invalid hue phase '%i', must be from -128 to 127\n", i);
				return 1;
			}
			hue = (int8_t)i;
			break;
		case 'i':
			i = atoi(optarg);
			switch (i) {
			case 1:
				cvbs_input = VIDEO1;
				break;
			case 2:
				cvbs_input = VIDEO2;
				break;
			case 3:
				cvbs_input = VIDEO3;
				break;
			case 4:
				cvbs_input = VIDEO4;
				break;
			default:
				fprintf(stderr, "Invalid CVBS input '%i', must be from 1 to 4\n", i);
				return 1;
			}
			break;
		case 'n':
			tv_standard = NTSC;
			break;
		case 'p':
			tv_standard = PAL;
			break;
		case 's':
			input_type = SVIDEO;
			break;
		case 'S':
			i = atoi(optarg);
			if (i < -128 || i > 127) {
				fprintf(stderr, "Invalid saturation value '%i', must be from -128 to 127\n", i);
				return 1;
			}
			saturation = (int8_t)i;
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
	if (input_type == SVIDEO && luminance_mode != 0) {
		fprintf(stderr, "Luminance mode must be 0 for S-VIDEO\n");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	program_path = malloc(strlen(argv[0]) + 1);
	if (program_path == NULL) {
		perror("Failed to allocate memory for program_path");
		return 1;
	}
	strcpy(program_path, argv[0]);

	/* Parse command line arguments */
	ret = parse_cmdline(argc, argv);
	if (ret) {
		return ret;
	}

	/* Initialize somagic registers */
	ret = somagic_init();
	if (ret) {
		return ret;
	}

	/* Perform capture */
	ret = somagic_capture();
	if (ret) {
		return ret;
	}

	return 0;
}
