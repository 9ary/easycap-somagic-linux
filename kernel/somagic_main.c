/*******************************************************************************
 * somagic_main.c                                                              *
 *                                                                             *
 * USB Driver for Somagic Easycap DC60                                         *
 * USB ID 1c88:0007                                                            *
 *                                                                             *
 * This driver will only upload the firmware for the somagic chip,             *
 * and reconnect the usb-dongle with new product id: 1c88:0003c.               *
 * *****************************************************************************
 *
 * Copyright 2011 Jon Arne Jørgensen
 *
 * This file is part of somagic_dc60
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#include "somagic.h"
#include "somagic_bootloader.h"
#include "somagic_main.h"

struct usb_device_id somagic_usb_device_id_table[] = {
	{ USB_DEVICE(USB_SOMAGIC_VENDOR_ID, USB_SOMAGIC_BOOTLOADER_PRODUCT_ID) },
	{ USB_DEVICE(USB_SOMAGIC_VENDOR_ID, USB_SOMAGIC_PRODUCT_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, somagic_usb_device_id_table);

struct usb_driver somagic_usb_driver = {
	.name = "somagic_easycap",
	.id_table = somagic_usb_device_id_table,
	.probe = somagic_usb_probe,
	.disconnect = somagic_usb_disconnect
};

int somagic_usb_probe(struct usb_interface *interface,
											const struct usb_device_id *interface_dev_id)
{
	struct usb_device * somagic_device;

  printk(KERN_DEBUG "somagic: Probing for %x:%x\n",
         interface_dev_id->idVendor,
         interface_dev_id->idProduct);

	somagic_device = container_of(interface->dev.parent,
																										struct usb_device, dev);
	if (somagic_device == (struct usb_device *)NULL) {
		printk(KERN_ERR "somagic: device is NULL\n");
		return -EFAULT;
	}

	if (interface_dev_id->idProduct == USB_SOMAGIC_BOOTLOADER_PRODUCT_ID) {
		somagic_upload_firmware(somagic_device);
		return -ENODEV;
	}
	
	return -ENODEV;
}

void somagic_usb_disconnect(struct usb_interface *interface)
{
	printk(KERN_DEBUG "somagic: Disconnect Called\n");
}

int __init somagic_module_init(void)
{
	int rc;
	rc = usb_register(&somagic_usb_driver);
	if (rc == 0) {
		printk(KERN_DEBUG "somagic::%s: Registered SOMAGIC Driver\n", __func__);
	} else {
		printk(KERN_DEBUG "somagic::%s: Failed to register SOMAGIC Driver\n", __func__);
	}
	return rc;
}

void __exit somagic_module_exit(void)
{
	usb_deregister(&somagic_usb_driver);
	printk(KERN_DEBUG "somagic::%s: Unregistered SOMAGIC Driver\n", __func__);
}

module_init(somagic_module_init);
module_exit(somagic_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne@gmail.com");
MODULE_DESCRIPTION(SOMAGIC_DRIVER_DESCRIPTION);
MODULE_VERSION(SOMAGIC_DRIVER_VERSION);

