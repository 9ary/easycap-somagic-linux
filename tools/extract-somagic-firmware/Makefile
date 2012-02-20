################################################################################
# Makefile                                                                     #
#                                                                              #
# Makefile for extract_somagic_firmware program                                #
# ##############################################################################
#
# Copyright 2011, 2012 Jeffry Johnston 
# 
# This file is part of somagic_easycap
# http://code.google.com/p/easycap-somagic-linux/
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.

BINDIR=/usr/local/bin
#MANDIR=/usr/local/share/man/man1
PROGRAMS=extract-somagic-firmware
#MANUALS=man/extract-somagic-firmware.1
LFLAGS=-lgcrypt
CC=gcc
CFLAGS=-W -Wall
INSTALL=install -D

all: $(PROGRAMS)

.c:
	-$(CC) $(CFLAGS) $< -o $@ $(LFLAGS)

install:
	$(INSTALL) $(PROGRAMS) $(BINDIR)/
	#$(INSTALL) $(MANUALS) $(MANDIR)/

clean:
	-rm -f $(PROGRAMS)

.PHONY: all install clean
