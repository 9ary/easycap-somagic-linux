#include "somagic.h"
#include "somagic_bootloader.h"

#include <linux/firmware.h>

void somagic_upload_firmware(struct usb_device *somagic_device)
{
	int rc, firmware_parts, i, e;
	__u16 igot;
	const u8 *dptr;
	u8 datapart[SOMAGIC_DATAPART_SIZE];
	const struct firmware * firmware = (const struct firmware *)NULL;
	rc = request_firmware(&firmware, SOMAGIC_FIRMWARE, &somagic_device->dev);

	if (rc != 0) {
		printk(KERN_ERR "somagic::%s: request_firmware returned %d!\n", __func__, rc);
		return;
	}
	if (firmware == (const struct firmware *)NULL) {
		return;
	}

	if ((firmware->size % (SOMAGIC_DATAPART_SIZE - SOMAGIC_DATAPART_HEADER_SIZE)) != 0) {
		printk(KERN_ERR "somagic::%s: Firmware has wrong size!\n", __func__);
		return;
	}

	// Prepare device for firmware upload!
	rc = usb_control_msg(somagic_device, usb_rcvctrlpipe(somagic_device, SOMAGIC_URB_RECEIVE_ENDPOINT),
											 (__u8)SOMAGIC_URB_STD_REQUEST,
											 (__u8)(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
											 (__u16)SOMAGIC_URB_PREPARE_FOR_FIRMWARE_VALUE,
											 (__u16)SOMAGIC_URB_STD_INDEX,
											 (void *)&igot,
											 (__u16)sizeof(igot),
											 SOMAGIC_URB_STD_TIMEOUT);
	if (igot != (__u16)SOMAGIC_ACK_READY_FOR_FIRMWARE) {
		printk(KERN_ERR "somagic::%s: Unexpected reply from device"
                    "when trying to prepare for firmware upload,"
                    "expected: %x, got %x.", __func__,
                    SOMAGIC_ACK_READY_FOR_FIRMWARE, igot);
		return;
	}

	firmware_parts = firmware->size / (SOMAGIC_DATAPART_SIZE - SOMAGIC_DATAPART_HEADER_SIZE);
	dptr = firmware->data;
	datapart[0] = SOMAGIC_DATAPART_HEADER_LO;
	datapart[1] = SOMAGIC_DATAPART_HEADER_HI;

	for (i = 0; i < firmware_parts; i++) {
		for (e = SOMAGIC_DATAPART_HEADER_SIZE; e < SOMAGIC_DATAPART_SIZE; e++) {
			datapart[e] = *dptr;
			dptr++;
		}
		rc = usb_control_msg(somagic_device, usb_sndctrlpipe(somagic_device, SOMAGIC_URB_SEND_ENDPOINT),
												 (__u8)SOMAGIC_URB_STD_REQUEST,
												 (__u8)(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
												 (__u16)SOMAGIC_URB_FIRMWARE_PART_VALUE,
												 (__u16)SOMAGIC_URB_STD_INDEX,
												 (void *)datapart,
												 (__u16)SOMAGIC_DATAPART_SIZE,
												 SOMAGIC_URB_STD_TIMEOUT);

		if (rc < 0) {
      printk(KERN_ERR "somagic::%s: error while uploading firmware,"
                      " usb_control_message #%d returned: %d",
                      __func__, i, rc);
			return;
		}
	}

	igot = igot >> 8; // 0x0701 -> 0x0007
	// Done with firmware upload!
	rc = usb_control_msg(somagic_device, usb_sndctrlpipe(somagic_device, SOMAGIC_URB_SEND_ENDPOINT),
											 (__u8)SOMAGIC_URB_STD_REQUEST,
											 (__u8)(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE),
											 (__u16)igot,
											 (__u16)SOMAGIC_URB_STD_INDEX,
											 (void *)&igot,
											 (__u16)sizeof(igot),
											 SOMAGIC_URB_STD_TIMEOUT);
	
	//printk(KERN_DEBUG "somagic: last request returned %d bytes\n", rc);
	return;
}
