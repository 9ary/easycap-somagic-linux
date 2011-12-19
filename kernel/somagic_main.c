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

struct usb_device_id somagic_usb_device_id_table[] = {
	{ USB_DEVICE(USB_SOMAGIC_VENDOR_ID, USB_SOMAGIC_BOOTLOADER_PRODUCT_ID) },
	{ USB_DEVICE(USB_SOMAGIC_VENDOR_ID, USB_SOMAGIC_PRODUCT_ID) },
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

const struct file_operations somagic_usb_fops = {
	.owner = THIS_MODULE,
	.open = somagic_usb_open,
	.release = somagic_usb_release,
	.unlocked_ioctl = somagic_usb_unlocked_ioctl,
	.poll = somagic_usb_poll,
	.mmap = somagic_usb_mmap,
  .llseek = no_llseek
};

// Must find out what minor_base is :)
struct usb_class_driver somagic_usb_class = {
	.name = "usb/somagic_easycap_dc60%d",
	.fops = &somagic_usb_fops,
	.minor_base = 192
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

	if (somagic_register_capture_device) {
		printk(KERN_DEBUG "somagic: register_capture_device = true\n");
	} else {
		printk(KERN_DEBUG "somagic: register_capture_device = false\n");
	}

	if (!somagic_register_capture_device) {
		return -ENODEV;
	}

	if ((usb_register_dev(interface, &somagic_usb_class)) != 0) {
		printk(KERN_ERR "somagic: Not able to get a minor for this device\n");
		usb_set_intfdata(interface, NULL);
		return -ENODEV;
	}

	printk(KERN_DEBUG "somagic: Attached device to minor #%d\n", interface->minor);
	
	return 0;
}

void somagic_usb_disconnect(struct usb_interface *interface)
{
	printk(KERN_DEBUG "somagic: Disconnect Called\n");
	usb_deregister_dev(interface, &somagic_usb_class);
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

int somagic_usb_open(struct inode *inode, struct file *file)
{
	return 0;
}

int somagic_usb_release(struct inode *inode, struct file *file)
{
	return 0;
}

long somagic_usb_unlocked_ioctl(struct file *file, unsigned int cmd,
                                unsigned long arg)
{
	return 0;
}

unsigned int somagic_usb_poll(struct file * file, poll_table *wait)
{
	return 0;
}

int somagic_usb_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;	
}

void __exit somagic_module_exit(void)
{
	usb_deregister(&somagic_usb_driver);
	printk(KERN_DEBUG "somagic::%s: Unregistered SOMAGIC Driver\n", __func__);
}

module_init(somagic_module_init);
module_exit(somagic_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne@gmail.com>");
MODULE_DESCRIPTION(SOMAGIC_DRIVER_DESCRIPTION);
MODULE_VERSION(SOMAGIC_DRIVER_VERSION);

