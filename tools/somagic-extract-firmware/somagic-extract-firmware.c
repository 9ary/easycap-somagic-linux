/*******************************************************************************
 * somagic-extract-firmware.c                                                  *
 *                                                                             *
 * Extract the EasyCAP Somagic firmware from a Windows driver file.            *
 * *****************************************************************************
 *
 * Copyright 2011, 2012 Jeffry Johnston
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

#include <errno.h>
#include <gcrypt.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

/* Constants */
#define PROGRAM_NAME "somagic-extract-firmware"
#define VERSION "1.0"
#define SOMAGIC_FIRMWARE_PATH "/lib/firmware/somagic_firmware.bin"

/* 
 * Index  Firmware 
 * -----  --------                               
 *     0  SmiUsbGrabber3C.sys, EasyCAP DC60   
 *     1  SmiUsbGrabber3E.sys, EasyCAP 002    
 */
static const int SOMAGIC_FIRMWARE_LENGTH[2] = {
	7502, 
	6634
};
static const unsigned char SOMAGIC_FIRMWARE_MAGIC[2][4] = {
	{'\x0c', '\x94', '\xce', '\x00'}, 
	{'\x0c', '\x94', '\xcc', '\x00'}
};
static const unsigned char SOMAGIC_FIRMWARE_CRC32[2][4] = {
	{'\x34', '\x89', '\xf7', '\x7b'}, 
	{'\x9d', '\x91', '\x8a', '\x92'}
};

void version()
{
	fprintf(stderr, PROGRAM_NAME" "VERSION"\n");
	fprintf(stderr, "Copyright 2011, 2012 Jeffry Johnston\n");
	fprintf(stderr, "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n");
	fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
	fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n");
}

void usage()
{
	fprintf(stderr, "Usage: "PROGRAM_NAME" [options] DRIVER_FILENAME\n");
	fprintf(stderr, "  -f, --firmware=FILENAME  Write to firmware file FILENAME\n");
	fprintf(stderr, "                           (default: "SOMAGIC_FIRMWARE_PATH")\n");
	fprintf(stderr, "      --help               Display usage\n");
	fprintf(stderr, "      --version            Display version information\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example (run as root):\n");
	fprintf(stderr, PROGRAM_NAME" SmiUsbGrabber3C.sys\n");
}

int main(int argc, char **argv)
{
	FILE *infile;
	int ret;
	char *firmware_path = SOMAGIC_FIRMWARE_PATH;
	unsigned char last4[4] = {'\0', '\0', '\0', '\0'};
	int firmware_found = 0;
	long pos;
	char firmware[SOMAGIC_FIRMWARE_LENGTH[0]];
	unsigned char digest[4];
	FILE *outfile;
	int i;

	/* Parsing */
	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{"help", 0, 0, 0},    /* index 0 */
		{"version", 0, 0, 0}, /* index 1 */
		{"firmware", 1, 0, 'f'},
		{0, 0, 0, 0}
	};

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
	if (optind + 1 != argc) {
		usage();
		return 1;
	}

	infile = fopen(argv[optind], "r");
	if (infile == NULL) {
		fprintf(stderr, "%s: Error opening driver file '%s': %s\n", argv[0], argv[optind], strerror(errno));
		return 1;
	}

	/* Search for the firmware magic */
	while (!firmware_found) {
		/* Read next byte from file */
		c = fgetc(infile);
		if (c == EOF) {
			/* Either at EOF or a read error occurred */
			if (!feof(infile)) {
				perror("Error reading driver file");
				return 1;
			}
			break;
		}

		/* Roll new character into array */
		memmove(last4, last4 + 1, 3);
		last4[3] = c;

		/* Check firmware magic */
		for (i = 0; i < 2; i++) {
			if (memcmp(last4, SOMAGIC_FIRMWARE_MAGIC[i], 4) == 0) {
				/* Found, save file position */
				pos = ftell(infile);

				/* Read rest of firmware */
				memcpy(firmware, last4, 4);
				ret = fread(firmware + 4, 1, SOMAGIC_FIRMWARE_LENGTH[i] - 4, infile);
				if (ret != SOMAGIC_FIRMWARE_LENGTH[i] - 4) {
					perror("Error reading driver file");
					return 1;
				}

				/* Check CRC32 */
				gcry_md_hash_buffer(GCRY_MD_CRC32, digest, firmware, SOMAGIC_FIRMWARE_LENGTH[i]);
				#ifdef DEBUG
				fprintf(stderr, "{'\\x%02x', '\\x%02x', '\\x%02x', '\\x%02x'}\n", digest[0], digest[1], digest[2], digest[3]);
				#endif 
				if (memcmp(digest, SOMAGIC_FIRMWARE_CRC32[i], 4) == 0) {
					/* CRC32 matched */
					firmware_found = 1;

					/* Write firmware file */
					outfile = fopen(firmware_path, "w+");
					if (outfile == NULL) {
						fprintf(stderr, "%s: Error opening firmware file '%s': %s\n", argv[0], firmware_path, strerror(errno));
						return 1;
					}
					ret = fwrite(firmware, 1, SOMAGIC_FIRMWARE_LENGTH[i], outfile);
					if (ret != SOMAGIC_FIRMWARE_LENGTH[i]) {
						perror("Error writing firmware file");
						return 1;
					}
					ret = fclose(outfile);
					if (ret) {
						perror("Error closing firmware file");
						return 1;
					}
			  		fprintf(stderr, "Firmware written to '%s'.\n", firmware_path);
				} else {
					/* False positive, return to previous file position and keep looking */
					ret = fseek(infile, pos, SEEK_SET);
					if (ret) {
						perror("Error seeking driver file");
						return 1;
					}
				}
			}
		}
	}

	ret = fclose(infile);
	if (ret) {
		perror("Error closing driver file");
		return 1;
	}

	if (!firmware_found) {
  		fprintf(stderr, "Somagic firmware was not found in driver file.\n");
		return 1;
	}

	return 0;
}

