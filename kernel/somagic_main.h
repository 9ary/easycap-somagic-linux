/*******************************************************************************
 * somagic_main.c                                                              *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * This driver will only upload the firmware for the Somagic chip,             *
 * and reconnect the usb-dongle with new product id: 1c88:003c.                *
 * *****************************************************************************
 *
 * Copyright 2011 Jon Arne Jørgensen
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

#ifndef SOMAGIC_MAIN_H
#define SOMAGIC_MAIN_H 

#include "somagic.h"

int somagic_usb_probe(struct usb_interface *interface,
                      const struct usb_device_id *interface_dev_id);

void somagic_usb_disconnect(struct usb_interface *interface);

int somagic_usb_open(struct inode *inode, struct file *file);
int somagic_usb_release(struct inode *inode, struct file *file);
long somagic_usb_unlocked_ioctl(struct file *file, unsigned int cmd,
                                unsigned long arg);
unsigned int somagic_usb_poll(struct file *file, poll_table *wait);
int somagic_usb_mmap(struct file *file, struct vm_area_struct *vma);
#endif /* SOMAGIC_MAIN_H */
