/*******************************************************************************
 * text2bin.c                                                                  *
 *                                                                             *
 * This is a small tool to create a binary firmware file from a text file with *
 * hex values.                                                                 *
 * *****************************************************************************
 *
 * Copyright 2011 Jon Arne Jørgensen
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

#include "file.h"

#include <stdio.h>

int main(int argc, char ** argv)
{
	FILE * fp;
	int size;
	fp = fopen(argv[1], "w+");
	size = fwrite((const void *)&file, 1, BYTECOUNT, fp);
	printf("Wrote %d bytes\n", size);
	return 0;
}
