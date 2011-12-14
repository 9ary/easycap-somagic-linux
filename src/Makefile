# Makefile
# Copyright Jeffry Johnston 2011
#
# This file is part of easycap-somagic-linux
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

BINDIR=/usr/local/bin
PROGRAMS=init1 init2 capture capture-ntsc
LFLAGS=-lusb -lusb-1.0
CC=gcc
INSTALL=install -D

all: $(PROGRAMS)

.c:
	$(CC) $< -o $@ $(LFLAGS)

install:
	$(INSTALL) $(PROGRAMS) $(BINDIR)/

clean:
	-rm -f $(PROGRAMS)

.PHONY: all install clean

