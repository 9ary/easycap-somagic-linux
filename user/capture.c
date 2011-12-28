/*******************************************************************************
 * capture.c                                                                   *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * Initializes the Somagic EasyCAP DC60 registers and performs image capture.  *
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

/*
 * Usage:
 * sudo init
 * sudo capture pal 2> /dev/null | mplayer - -vf screenshot -demuxer rawvideo -rawvideo "w=720:h=576:format=uyvy:fps=25"
 * sudo capture ntsc 2> /dev/null | mplayer - -vf screenshot -demuxer rawvideo -rawvideo "ntsc:format=uyvy:fps=30000/1001"
 */

/* This file was originally generated with usbsnoop2libusb.pl from a usbsnoop log file. */
/* Latest version of the script should be in http://iki.fi/lindi/usb/usbsnoop2libusb.pl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <ctype.h>
#include <libusb-1.0/libusb.h>
#include <execinfo.h>
#include <unistd.h>

#define VENDOR 0x1c88
#define PRODUCT 0x003c
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

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

const int BYTES_PER_LINE = 1448;
int lineleft = 1448;
int vbi = 0;
int buffer_pos = 0;
int buffer_size;
int next_boundary = 0;
unsigned char *pbuffer = NULL;

void check_fence()
{
	if (buffer_pos >= buffer_size) {
		return;
	}
	if (buffer_pos % 0x400 == 0) {
		/* on 1024 byte boundaries */
		/* fprintf(stderr, "boundary at %04x ", buffer_pos); */
		next_boundary = buffer_pos + 0x400;
		if (pbuffer[buffer_pos] == (unsigned char)0xaa) {
			/* the byte is 0xaa, skip this marker */
			buffer_pos += 4;
			/* fprintf(stderr, " is video\n"); */
		} else {
			/* otherwise */
			/*
			fprintf(stderr, "\ndiscard block. bytes:");
			print_bytes_only(pbuffer + buffer_pos + 0x3f0, 32);
			fprintf(stderr, "\n");
			*/
			buffer_pos += 0x400;

			/* skip the whole 1024 byte block */
			next_boundary = buffer_pos + 0x400;
			check_fence();
			/*
			buffer_pos += 4;
			fprintf(stderr, " is discard\n");
			*/

			/* Need to check for successive skipped blocks? */
		}
	}
}

void init_buffer(unsigned char *buffer, int size)
{
	/* fprintf(stderr, "buffer init, size %d (%04x)\n", size, size); */
	pbuffer = buffer;
	buffer_pos = 0;
	next_boundary = buffer_pos + 0x400;
	buffer_size = size;
	check_fence();
}


int get_buffer_char()
{
	unsigned char c;
	if (buffer_pos >= buffer_size) {
		return -1;
	}
	c = pbuffer[buffer_pos++];
	check_fence();
	return c;
}

int write_buffer(int count, unsigned char *frame, int line, int field)
{
	int written = 0;
	int dowrite;
	int line_pos;
	int wrote;
	int lines_per_field = (tv_standard == PAL ? 288 : 240);
	while (buffer_pos < buffer_size && count > 0) {
		dowrite = MIN(next_boundary - buffer_pos, count);
		/*
		fprintf(stderr, "WROTE %d(%04x)  ", dowrite, dowrite);
		int wrote = write(2, pbuffer + buffer_pos, dowrite);
		*/
		line_pos = line * (720 * 2) * 2 + (field * 720 * 2) + ((720 * 2) - count);
		/* fprintf(stderr, "write: line=%d, field=%d, count=%d, off=%d\n", line, field, count, line_pos); */
		if (line < lines_per_field) {
			memcpy(line_pos + frame, pbuffer + buffer_pos, dowrite);
		}
		wrote = dowrite;
		buffer_pos += wrote;
		count -= wrote;
		written += wrote;
		check_fence();
	}
	return written;
}

int skip_buffer(int count)
{
	int skipped = 0;
	int skip;
	while (buffer_pos < buffer_size && count > 0) {
		skip = MIN(next_boundary - buffer_pos, count);
		buffer_pos += skip;
		count -= skip;
		skipped += skip;
		check_fence();
	}
	return skipped;
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

void process_data()
{
	/* buffer_init() has been called */
	int next = 0;
	int bs = 0; /* bad (lost) sync: 0=no, 1=yes */
	int hs = 0;
	int lines_per_field = (tv_standard == PAL ? 288 : 240);
	do {
		if (state < VBLANK) {
			next = get_buffer_char();
		}
		unsigned char nc = next;
		/* fprintf(stderr, "%d", state); */
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
					fprintf(stderr, "resync after %d @%d(%04x)\n", hs, buffer_pos, buffer_pos);
				}
				bs = 0;
			} else if (bs != 1) {
				/*
				 * The 1st byte in the TRC must be 0xff. It
				 * wasn't, so sync was either lost or has not
				 * yet been regained. Sync is regained by
				 * ignoring bytes until the next 0xff.
				 */
				fprintf(stderr, "bad sync on line %d @%d (%04x)\n", active_line_count, buffer_pos, buffer_pos);
				/*
				print_bytes_only(pbuffer, buffer_pos + 64);
				print_bytes(pbuffer + buffer_pos, 8);
				*/
				bs = 1;
			}
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
							write(1, frame, 720 * 2 * lines_per_field * 2);
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
			break;
		case VBLANK:
		case VACTIVE:
		case REMAINDER:
			if (state == VBLANK || vblank_found < 20) {
				line_remaining -= skip_buffer(line_remaining);
				/* fprintf(stderr, "vblank_found=%d\n", vblank_found); */
			} else {
				line_remaining -= write_buffer(line_remaining, frame, active_line_count, field);
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
				next = -1;
			}
			break;
		}
	} while (next != -1);
}

#if 0
int sync = 0;
int sync_seek = 1;
int line_count = 0;
int is_vblank = -1;

void find_sync(char* bytes, int len)
{
	int i = 0;
	fprintf(stderr, "find_sync sync=%d\n", sync);
	while (i < len) {
		if (bytes[i] == (char)(0xaa)) {
			if (sync == 0) {
				sync = i;
			}
			/* fprintf(stderr, "aa aa 00 00 block i=%d, sync=%d \n", i, sync); */
			while (sync < i + 0x400) {
				if (bytes[sync] == (char)0xff && bytes[sync+1] == (char)0x00 && bytes[sync+2] == (char)0x00) {
					unsigned char seav = bytes[sync+3];
					/* fprintf(stderr, " %02x ", seav); */
					if (seav & (char)0x40) {
						/* fprintf(stderr, "@ sync=%d F1 ", sync); */
					} else {
						/* fprintf(stderr, "@ sync=%d F0 ", sync); */
					}
					if (seav & (char)0x20) {
						/* fprintf(stderr, "V1 "); */
						if (is_vblank == 0) {
							fprintf(stderr, "V1 lc=%d\n", line_count);
						}
						is_vblank = 1;
					} else {
						if (is_vblank == 0) {
							line_count++;
						}

						if (is_vblank == 1) {
							line_count = 0;
							is_vblank = 0;
						}
						/* fprintf(stderr, "V0 "); */
					}
					if (seav & (char)0x10) {
						/* fprintf(stderr, "EAV \n"); */
						sync += 4;
					} else {
						sync_seek = 0;
						fprintf(stderr, "SAV b=%d", sync % 0x400);
						if (sync % 0x400 >= (0x400 - (1448 - 0x400))) {
							/* frame adjustment */
							sync+= 4;
						}
						/* point to next SAV; skip 720*4 pixels + 4 SAV + 4 EAV */
						sync += 720 * 2 + 4 + 4;

						/* aa aa 00 00 frame marker adjustments */
						sync += 4;
						fprintf(stderr, "  -> next @ sync=%d \n", sync);
					}
				} else {
					sync++;
					if (sync_seek == 0) {
						fprintf(stderr, "sync miss @expected=%d. @pos=%d: ", sync - 1, sync - 7);
						print_bytes(bytes + sync-7, 6);
						print_bytes(bytes + sync-1, 6);
						fprintf(stderr, "\n");
					}
					sync_seek = 1;
				}
			}
		} else {
			/* adjust over filler block */
			sync += 0x400;
			fprintf(stderr, "%02x %02x %02x %02x block i=%d sync(adj)=%d\n", bytes[i], bytes[i+1], bytes[i+2], bytes[i+3], i, sync);
		}
		i += 0x400;
	}
	sync = sync % i;
}

void save_bytes(char *bytes, int len) {
	/* if (len > 0) write(2, bytes, len); */
	/* return; */
	int i = 0;
	int j = 0;
	int t = 0;
	/* if (vbi == 0) i+= 0xc00; */
	int line_number = 0;
	while (i < len) {
		if (bytes[i] == (char)(0xaa)) {
			j = 4;
			while (j < 0x400) {
				if (bytes[i + j] == (char)0xff) {
					/* fprintf(stderr, "%d: ", line_number); */
					/* print_bytes(bytes + i + j, 4); */
					/* fprintf(stderr, "\n"); */
				}
				if ((bytes[i + j] == (char)0xff && (bytes[i + j + 3] & (unsigned char)0xb0) == (unsigned char)0x80) || lineleft < BYTES_PER_LINE) {
					int dowrite = MIN(0x400 - j, lineleft);

					if (vbi < 20) { 
						/* seek vbi, bit 5 in SAV */
						if ((bytes[i + j + 3] & (unsigned char)0x20) == (unsigned char)0x20) {
							vbi++;
							/* fprintf(stderr, "vbi:%d\n",vbi); */
						} /* else {
							vbi--;
							fprintf(stderr, "vbi:%d\n",vbi);
						} */
					}

					if (vbi >= 20) {
						lineleft -= dowrite;
						write(1, bytes + i + j, dowrite);
					}
					j += dowrite;

					if (lineleft <= 0) {
						lineleft = BYTES_PER_LINE;
						line_number++;
					}
				} else {
					j++;
				}
			}
		}
		i += 0x400;
	}
}
#endif

unsigned char isobuf[64 * 3072];
unsigned char isobuf1[64 * 3072];
unsigned char isobuf2[64 * 3072];
unsigned char isobuf3[64 * 3072];
int pcount = 0;
const int FCOUNT = 800000;

void gotdata(struct libusb_transfer *tfr)
{
	int ret;
	int num = tfr->num_iso_packets;
	unsigned char *data = libusb_get_iso_packet_buffer_simple(tfr, 0);
	int length = tfr->iso_packet_desc[0].length;
	int total = 0;
	int i = 0;
	for (; i < num; i++) {
		total += tfr->iso_packet_desc[i].actual_length;
	}
	/* fprintf(stderr, "id %d got %d pkts of length %d. calc=%d, total=%d (%04x)\n", pcount, num, length, num*length, total, total); */
	pcount++;

	if (pcount >= 0) {
		/* find_sync(data, num * length); */
		init_buffer(data, num * length);
		/* init_buffer(data, total); */
		process_data();
		/*
		fprintf(stderr, "write\n");
		write(1, data, total);
		write(1,"----------------",16);
		*/
	}

	if (pcount <= FCOUNT - 4) {
		/* fprintf(stderr, "resubmit id %d\n", pcount - 1); */
		ret = libusb_submit_transfer(tfr);
		if (ret != 0) {
			fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
			exit(1);
		}
	}
}

void usage(char * name)
{
	fprintf(stderr, "Usage: %s pal | ntsc\n", name);
	exit(1);
}

int main(int argc, char **argv)
{
	int ret;
	struct libusb_device *dev;
	unsigned char buf[65535];
	struct libusb_transfer *tfr0;
	struct libusb_transfer *tfr1;
	struct libusb_transfer *tfr2;
	struct libusb_transfer *tfr3;

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
	assert(dev);

	ret = libusb_open(dev, &devh);
	libusb_unref_device(dev);
	assert(ret == 0);
	
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
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3a\x80", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "6 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3b\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "7 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x34\x01", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "8 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x35\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "9 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x20\x82\x01\x30\x80\x80", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "10 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "11 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x34\x11", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "12 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x35\x11", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "13 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3a\x80", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "14 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3b\x80", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "15 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3a\x80", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "16 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x00\x3b\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "17 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x01\x08\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "96 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x02\xc7\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "97 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x03\x33\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "98 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x04\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "99 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x05\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "100 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x06\xe9\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "101 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x07\x0d\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "102 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 08h: d8 or 98 = auto NTSC/PAL, 58 = force NTSC, 18 = force PAL */
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x08\x98\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "103 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x09\x01\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "104 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0a\x80\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "105 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0b\x40\xff", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "106 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0c\x40\xff", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "107 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0d\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "108 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "109 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "110 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "111 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "112 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "113 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "114 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "115 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "116 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "117 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x81\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "118 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0f\x2a\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "119 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x10\x40\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "120 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x11\x0c\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "121 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x12\x01\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "122 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x13\x80\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "123 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x14\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "124 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x15\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "125 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x16\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "126 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x17\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "127 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x40\x02\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "128 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x58\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "129 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 59h: HOFF - horizontal offset */
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x59\x54\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "130 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 5Ah: VOFF - vertical offset */
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x5a\x07\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "131 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 5Bh: HVOFF - horizontal and vertical offset bits */
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x5b\x03\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "132 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 5Eh: SDID codes. 0 = sets SDID5 to SDID0 active */
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x5e\x00\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "133 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x02\xc0\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "134 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0e\x01\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "141 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 40h: 00 for PAL, 80 for NTSC */
	if (tv_standard == PAL) {
		memcpy(buf, "\x0b\x4a\x84\x00\x01\x40\x00\x00", 0x0000008);
	} else {
		memcpy(buf, "\x0b\x4a\x84\x00\x01\x40\x80\xf4", 0x0000008);
	}
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "142 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xa0\x00\x01\x86\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "143 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xa0\x00\x01\x00\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "144 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\x84\x00\x01\x5b\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "145 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xa0\x00\x01\xf3\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "146 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xa0\x00\x01\x00\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "147 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\x84\x00\x01\x10\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "148 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xa0\x00\x01\xc4\x30\xf4", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "149 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 40h: 00 for PAL, 80 for NTSC */
	if (tv_standard == NTSC) {
		memcpy(buf, "\x0b\x4a\xa0\x00\x01\x40\x80\xf4", 0x0000008);
	}
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "150 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	/* Subaddress 5Ah: 07 for PAL, 0a for NTSC */
	if (tv_standard == PAL) {
		memcpy(buf, "\x0b\x4a\xc0\x01\x01\x5a\x07\x00", 0x0000008);
	} else {
		memcpy(buf, "\x0b\x4a\xc0\x01\x01\x5a\x0a\x86", 0x0000008);
	}
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "151 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x59\x54\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "152 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x5b\x83\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "153 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x10\x40\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "154 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x55\xff\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "154a control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x41\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "155 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x42\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "156 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x43\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "157 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x44\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "158 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x45\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "159 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x46\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "160 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x47\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "161 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x48\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "162 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x49\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "163 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x4a\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "164 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x4b\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "165 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x4c\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "166 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x4d\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "167 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x4e\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "168 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x4f\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "169 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x50\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "170 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x51\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "171 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x52\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "172 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x53\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "173 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x54\x77\x86", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "174 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0a\x80\x01", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "176 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0b\x40\x01", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "177 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0d\x00\x01", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "178 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x0c\x40\x01", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "179 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x4a\xc0\x01\x01\x09\x01\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "180 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x17\x40\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "183 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x20\x82\x01\x30\x80\x1b", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "184 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "185 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x00\x82\x01\x17\x40\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "186 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");

	usleep(250 * 1000);
	memcpy(buf, "\x0b\x00\x00\x82\x01\x17\x40\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "187 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	memcpy(buf, "\x0b\x00\x20\x82\x01\x30\x80\x10", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "188 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE + LIBUSB_ENDPOINT_IN, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "189 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");
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
	memcpy(buf, "\x0b\x00\x00\x82\x01\x17\x40\x00", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "193 control msg returned %d, bytes: ", ret);
	print_bytes(buf, ret);
	fprintf(stderr, "\n");

	usleep(30 * 1000);
	tfr0 = libusb_alloc_transfer(64);
	assert(tfr0 != NULL);
	libusb_fill_iso_transfer(tfr0, devh, 0x00000082, isobuf, 64 * 3072, 64, gotdata, NULL, 2000);
	libusb_set_iso_packet_lengths(tfr0, 3072);

	tfr1 = libusb_alloc_transfer(64);
	assert(tfr1 != NULL);
	libusb_fill_iso_transfer(tfr1, devh, 0x00000082, isobuf1, 64 * 3072, 64, gotdata, NULL, 2000);
	libusb_set_iso_packet_lengths(tfr1, 3072);

	tfr2 = libusb_alloc_transfer(64);
	assert(tfr2 != NULL);
	libusb_fill_iso_transfer(tfr2, devh, 0x00000082, isobuf2, 64 * 3072, 64, gotdata, NULL, 2000);
	libusb_set_iso_packet_lengths(tfr2, 3072);

	tfr3 = libusb_alloc_transfer(64);
	assert(tfr3 != NULL);
	libusb_fill_iso_transfer(tfr3, devh, 0x00000082, isobuf3, 64 * 3072, 64, gotdata, NULL, 2000);
	libusb_set_iso_packet_lengths(tfr3, 3072);

	ret = libusb_submit_transfer(tfr0);
	if (ret != 0) {
		fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
		exit(1);
	}

	ret = libusb_submit_transfer(tfr1);
	if (ret != 0) {
		fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
		exit(1);
	}

	ret = libusb_submit_transfer(tfr2);
	if (ret != 0) {
		fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
		exit(1);
	}

	ret = libusb_submit_transfer(tfr3);
	if (ret != 0) {
		fprintf(stderr, "libusb_submit_transfer failed with error %d\n", ret);
		exit(1);
	}

	memcpy(buf, "\x0b\x00\x00\x82\x01\x18\x00\x0d", 0x0000008);
	ret = libusb_control_transfer(devh, LIBUSB_REQUEST_TYPE_VENDOR + LIBUSB_RECIPIENT_DEVICE, 0x0000001, 0x000000b, 0x0000000, buf, 0x0000008, 1000);
	fprintf(stderr, "242 control msg returned %d, bytes: ", ret);
		print_bytes(buf, ret);
	fprintf(stderr, "\n");

	while (pcount < FCOUNT) {
		libusb_handle_events(NULL);
	}

	libusb_free_transfer(tfr0);
	libusb_free_transfer(tfr1);
	libusb_free_transfer(tfr2);
	libusb_free_transfer(tfr3);

	ret = libusb_release_interface(devh, 0);
	assert(ret == 0);
	libusb_close(devh);
	libusb_exit(NULL);
	return 0;
}
