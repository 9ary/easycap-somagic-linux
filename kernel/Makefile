################################################################################
# Makefile                                                                     #
#                                                                              #
# Makefile for kernel USB Driver for Somagic EasyCAP DC60                      #
# ##############################################################################
#
# Copyright 2011, 2012 Jon Arne Jørgensen
# 
# This file is part of somagic_dc60
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

SHELL = /bin/sh
obj-m += somagic.o
somagic-y += somagic_dev.o \
             somagic_video.o \
             somagic_audio.o \
             somagic_bootloader.o \
             somagic_main.o
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.SUFFIXES:

.PHONY: all
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

.PHONY: clean
clean:
	-rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c modules.order Module.symvers
	-rm -rf .tmp_versions
