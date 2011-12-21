/*
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
#ifndef SOMAGIC_CAPTURE_DEVICE_H
#define SOMAGIC_CAPTURE_DEVICE_H 

#include "somagic.h"
#include <media/v4l2-device.h>

struct usb_somagic {
	struct v4l2_device v4l2_dev;
	struct video_device *vdev;
	struct v4l2_capability vcap;

	/* Device structure */
	struct usb_device *dev;
	struct mutex v4l2_lock;

	/* Status */
	int initialized;
	unsigned int nr;								/* Number of the device */
};

int somagic_capture_device_register(struct usb_interface *interface);
void somagic_capture_device_deregister(struct usb_interface *interface);

static inline struct usb_somagic *to_somagic(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct usb_somagic, v4l2_dev);
}

#endif /* SOMAGIC_CAPTURE_DEVICE_H */
