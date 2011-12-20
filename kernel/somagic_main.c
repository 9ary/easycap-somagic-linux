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

#include "somagic.h"
#include "somagic_bootloader.h"
#include "somagic_main.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne@gmail.com>");
MODULE_DESCRIPTION(SOMAGIC_DRIVER_DESCRIPTION);
MODULE_VERSION(SOMAGIC_DRIVER_VERSION);

struct usb_device_id somagic_usb_device_id_table[] = {
	{ USB_DEVICE(SOMAGIC_USB_VENDOR_ID, SOMAGIC_USB_BOOTLOADER_PRODUCT_ID) },
	{ USB_DEVICE(SOMAGIC_USB_VENDOR_ID, SOMAGIC_USB_PRODUCT_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, somagic_usb_device_id_table);

// Set this to false, if you want to use the userspace tools!
bool somagic_register_capture_device = true;
module_param_named(register_capture_device,
                   somagic_register_capture_device, bool, 0);


struct usb_driver somagic_usb_driver = {
	.name = "somagic_easycap",
	.id_table = somagic_usb_device_id_table,
	.probe = somagic_usb_probe,
	.disconnect = somagic_usb_disconnect
};

static int __devinit somagic_usb_probe(struct usb_interface *intf,
											const struct usb_device_id *devid)
{
	struct usb_device *dev = usb_get_dev(interface_to_usbdev(intf));

  printk(KERN_INFO "%s: Probing for %#04x:%#04x\n", __func__,
         dev->descriptor.idVendor,
         dev->descriptor.idProduct);

	if (dev == (struct usb_device *)NULL) {
		printk(KERN_ERR "somagic: device is NULL\n");
		return -EFAULT;
	}

	if (dev->descriptor.idProduct == SOMAGIC_USB_BOOTLOADER_PRODUCT_ID) {
		somagic_upload_firmware(dev);
		return -ENODEV;
	}

	if (!somagic_register_capture_device) {
		return -ENODEV;
	}

	return somagic_capture_device_register(intf);
}

static void __devexit somagic_usb_disconnect(struct usb_interface *intf)
{
	printk(KERN_DEBUG "somagic: Disconnect Called\n");
	somagic_capture_device_deregister(intf);
}

int __init somagic_module_init(void)
{
	int rc;
	rc = usb_register(&somagic_usb_driver);
	if (rc == 0) {
		printk(KERN_INFO "%s: Registered SOMAGIC Driver\n", __func__);
	} else {
		printk(KERN_INFO "%s: Failed to register SOMAGIC Driver\n", __func__);
	}
	return rc;
}


void __exit somagic_module_exit(void)
{
	usb_deregister(&somagic_usb_driver);
	printk(KERN_INFO "%s: Unregistered SOMAGIC Driver\n", __func__);
}

module_init(somagic_module_init);
module_exit(somagic_module_exit);


