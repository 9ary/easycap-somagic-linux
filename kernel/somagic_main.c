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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne@gmail.com>");
MODULE_DESCRIPTION(SOMAGIC_DRIVER_DESCRIPTION);
MODULE_VERSION(SOMAGIC_DRIVER_VERSION);

/*****************************************************************************/
/*                                                                           */
/*          Function declarations                                            */
/*                                                                           */
/*****************************************************************************/
static int somagic_usb_probe(struct usb_interface *intf,
                      const struct usb_device_id *devid);

static void somagic_usb_disconnect(struct usb_interface *intf);

/*****************************************************************************/
/*                                                                           */
/*          Module setup                                                     */
/*                                                                           */
/*****************************************************************************/
// Set this to false, if you want to use the userspace tools!
bool somagic_default_ntsc = false;
bool somagic_register_capture_device = true;

module_param_named(register_capture_device,
                   somagic_register_capture_device, bool, 0);
module_param_named(default_ntsc,
                   somagic_default_ntsc, bool, 0);
MODULE_PARM_DESC(default_ntsc, " Set the device to initialize in NTSC mode. Default: 0 (PAL)");

struct usb_device_id somagic_usb_device_id_table[] = {
	{ USB_DEVICE(SOMAGIC_USB_VENDOR_ID, SOMAGIC_USB_BOOTLOADER_PRODUCT_ID) },
	{ USB_DEVICE(SOMAGIC_USB_VENDOR_ID, SOMAGIC_USB_PRODUCT_ID) },
	{ }
};

MODULE_DEVICE_TABLE(usb, somagic_usb_device_id_table);
struct usb_driver somagic_usb_driver = {
	.name = "somagic_easycap",
	.id_table = somagic_usb_device_id_table,
	.probe = somagic_usb_probe,
	.disconnect = somagic_usb_disconnect
};

/*****************************************************************************/
/*                                                                           */
/*          DEVICE  -  PROBE   &   DISCONNECT                                */
/*                                                                           */
/*****************************************************************************/
static int __devinit somagic_usb_probe(struct usb_interface *intf,
											const struct usb_device_id *devid)
{
	struct usb_device *dev = usb_get_dev(interface_to_usbdev(intf));

  printk(KERN_INFO "%s: Probing for %04x:%04x\n", __func__,
         dev->descriptor.idVendor,
         dev->descriptor.idProduct);

	if (dev == (struct usb_device *)NULL) {
		printk(KERN_ERR "somagic::%s: device is NULL\n", __func__);
		return -EFAULT;
	}

  if (dev->descriptor.idProduct == SOMAGIC_USB_BOOTLOADER_PRODUCT_ID) {
		somagic_run_bootloader(dev);
		return (0); //-ENODEV;
	} else if (dev->descriptor.idProduct != SOMAGIC_USB_PRODUCT_ID) {
		return -ENODEV;
	}

	if (!somagic_register_capture_device) {
		// Module loaded with somagic.register_capture_device = 0
		return -ENODEV;
	}

  // We have a valid Somagic-Easycap device. now we setup the driver!
	return somagic_dev_init(intf);

}

static void __devexit somagic_usb_disconnect(struct usb_interface *intf)
{
	struct usb_device *dev = usb_get_dev(interface_to_usbdev(intf));

	if (dev->descriptor.idProduct != SOMAGIC_USB_PRODUCT_ID) {
    return;
	}

	somagic_dev_exit(intf);
/*
	somagic = usb_get_intfdata(intf);
	if (somagic == NULL) {
		printk(KERN_WARNING "somagic::%s: "
                "Could not load driver-structure from interface!\n", __func__);
		return;
	}

	somagic->initialized = 0;
	somagic_disconnect_video(somagic);

	somagic->dev = NULL;
	kfree(somagic);

	printk(KERN_INFO "somagic:%s: Driver-struct has been removed\n", __func__);
*/
}

/*****************************************************************************/
/*                                                                           */
/*            MODULE  -  INIT  &  EXIT                                       */
/*                                                                           */
/*****************************************************************************/

int __init somagic_module_init(void)
{
	int rc;

	printk(KERN_INFO "Somagic-Easycap version: "SOMAGIC_DRIVER_VERSION"\n");

	rc = usb_register(&somagic_usb_driver);
	if (rc) { 
		printk(KERN_INFO "%s: Failed to register Somagic-Easycap Driver\n", __func__);
	}
	return rc;
}


// What happens if we rmmod the module, and then pulls out the device?
void __exit somagic_module_exit(void)
{
	usb_deregister(&somagic_usb_driver);
}

module_init(somagic_module_init);
module_exit(somagic_module_exit);


