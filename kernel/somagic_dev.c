#include "somagic.h"

/*****************************************************************************/
/*                                                                           */
/* SYSFS Code	- Copied from the stv680.c usb module.												 */
/* Device information is located at /sys/class/video4linux/videoX/device     */
/* Device parameters information is located at /sys/module/somagic_easycap   */
/* Device USB information is located at /sys/bus/usb/drivers/somagic_easycap */
/*                                                                           */
/*****************************************************************************/

static ssize_t show_isoc_count(struct device *cd,
							struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = container_of(cd, struct usb_interface, dev);
	struct usb_somagic *somagic = usb_get_intfdata(intf);
	return sprintf(buf, "%d\n", somagic->received_urbs); 
}

static DEVICE_ATTR(isocs, S_IRUGO, show_isoc_count, NULL);

static ssize_t test_sysfs(struct device *char_dev,
													struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Test fra somagic\n");
}

static DEVICE_ATTR(test, S_IRUGO, test_sysfs, NULL);

static void create_sysfs(struct usb_device *dev)
{
	int res;
	if (!dev) {
		return;
	}

	do {
		res = device_create_file(&dev->dev, &dev_attr_test);
		if (res < 0) {
			break;
		}
		res = device_create_file(&dev->dev, &dev_attr_isocs);
		if (res >= 0) {
			return;
		}
	} while(0);

	dev_err(&dev->dev, "somagic::%s error: %d\n", __func__, res);
}

static void remove_sysfs(struct usb_device *dev)
{
	if (dev) {
		device_remove_file(&dev->dev, &dev_attr_test);
		device_remove_file(&dev->dev, &dev_attr_isocs);
	}
}

/*****************************************************************************/
/*                                                                           */
/*            USB transfer                                                   */
/*                                                                           */
/*****************************************************************************/

/*
 * isoc_complete
 *
 * This is an interrupt handler.
 * Called when we recieve isochronous usb-data from the device
 */
static void isoc_complete(struct urb *urb)
{
	int i,rc;
	struct usb_somagic *somagic = urb->context;

	if (!(somagic->streaming_flags & SOMAGIC_STREAMING_STARTED)) {
		return;
	}

	if (urb->status == -ENOENT) {
		printk(KERN_INFO "somagic::%s: Recieved empty ISOC urb!\n", __func__);
		return;
	}

	somagic->received_urbs++;

	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned char *data = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		int packet_len = urb->iso_frame_desc[i].actual_length;
		int pos = 0;

		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;

		if ((packet_len % 0x400) != 0) {
				printk(KERN_INFO "somagic::%s: Discrad ISOC packet with "\
							           "unknown size! Size is %d\n",
								__func__, packet_len);
				continue;
		}

		while(pos < packet_len) {
			/*
			 * Within each packet of transfer, the data is divided into blocks of 0x400 (1024) bytes
			 * beginning with [0xaa 0xaa 0x00 0x00] for video, or [0xaa 0xaa 0x00 0x01] for audio.
		 	 * Check for this signature, and pass each block to scratch buffer for further processing
		 	 */

			if (data[pos] == 0xaa && data[pos+1] == 0xaa &&
					data[pos+2] == 0x00 && data[pos+3] == 0x00) {
				// We have video data, put it on scratch-buffer

				somagic_video_put(somagic, &data[pos+4], 0x400 - 4);

			} else if (data[pos] == 0xaa && data[pos+1] == 0xaa &&
								 data[pos+2] == 0x00 && data[pos+3] == 0x01) {

				somagic_audio_put(somagic, &data[pos + 4], 0x400 - 4);

			} else if (printk_ratelimit()) {
				printk(KERN_INFO "somagic::%s: Unexpected block, "\
							 "expected [aa aa 00 00], found [%02x %02x %02x %02x]\n",
							 __func__, data[pos], data[pos+1], data[pos+2], data[pos+3]);
			}

			// Skip to next 0xaa 0xaa 0x00 0x00
			pos += 0x400;
		}
	}

	tasklet_hi_schedule(&(somagic->audio.process_audio));
	tasklet_hi_schedule(&(somagic->video.process_video));

	urb->status = 0;
	urb->dev = somagic->dev;
	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc) {
		dev_err(&somagic->dev->dev,
						"%s: usb_submit_urb failed: error %d\n",
						__func__, rc);
	}

	return;
}

static int allocate_isoc_buffer(struct usb_somagic *somagic)
{
	int buf_idx;
	int isoc_buf_size = 32 * 3072; // 32 = NUM_URB_FRAMES * 3072 = VIDEO_ISOC_PACKET_SIZE

	for (buf_idx = 0; buf_idx < SOMAGIC_NUM_ISOC_BUFFERS; buf_idx++) {
		int j,k;
		struct urb *urb;

		urb = usb_alloc_urb(32, GFP_KERNEL); // 32 = NUM_URB_FRAMES
		if (urb == NULL) {
			dev_err(&somagic->dev->dev,
			"%s: usb_alloc_urb() failed\n", __func__);
			return -ENOMEM;
		}

		somagic->isoc_buf[buf_idx].urb = urb;
		somagic->isoc_buf[buf_idx].data =
				usb_alloc_coherent(somagic->dev,
													isoc_buf_size,
													GFP_KERNEL,
													&urb->transfer_dma);

		urb->dev = somagic->dev;
		urb->context = somagic;
		urb->pipe = usb_rcvisocpipe(somagic->dev, 0x82);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = 1;
		urb->transfer_buffer = somagic->isoc_buf[buf_idx].data;
		urb->complete = isoc_complete;
		urb->number_of_packets = 32; // 32 = NUM_URB_FRAMES
		urb->transfer_buffer_length = isoc_buf_size;
		for (j = k = 0; j < 32; j++, k += 3072) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = 3072;
		}
	}

	printk(KERN_INFO "somagic::%s: Allocated ISOC urbs!\n", __func__);
	return 0;
}

void free_isoc_buffer(struct usb_somagic *somagic)
{
	int buf_idx;
	int isoc_buf_size = 32 * 3072;

	for (buf_idx = 0; buf_idx < SOMAGIC_NUM_ISOC_BUFFERS; buf_idx++) {
		usb_kill_urb(somagic->isoc_buf[buf_idx].urb);

		if (somagic->isoc_buf[buf_idx].data) {
			usb_free_coherent(somagic->dev,
												isoc_buf_size,
												somagic->isoc_buf[buf_idx].data,
												somagic->isoc_buf[buf_idx].urb->transfer_dma);
		}

		usb_free_urb(somagic->isoc_buf[buf_idx].urb);
		somagic->isoc_buf[buf_idx].urb = NULL;
	}

	printk(KERN_INFO "somagic::%s: Freed ISOC urbs!\n", __func__);
}

/*****************************************************************************/
/*                                                                           */
/*            Device communication                                           */
/*                                                                           */
/*****************************************************************************/

static const struct saa_setup {
	int reg;
	int val;
} saa_setupNTSC[256] = {
	{0x01, 0x08},
	{0x02, 0xc0}, // FUSE0 + FUSE1 -- MODE0 (CVBS - Input)
	{0x03, 0x33}, // WHITEPEAK OFF | LONG V-BLANK -- AGC ON | AUTOMATIC GAIN MODE0-3 | GAI18 = 1 | GAI28 = 1
	{0x04, 0x00}, // GAI10-GAI17 Gain-control Input 1
	{0x05, 0x00}, // GAI20-GAI27 Gain-control Input 2
	{0x06, 0xe9}, // Horizontal-Sync Begin (-23)
	{0x07, 0x0d}, // Horizontal-Sync End (13)
	{0x08, 0x98}, // Sync CTRL (Automatic field detection (PAL / NTSC))
	{0x09, 0x01},	// Luminance CTRL (Aperture factor 0.25)
	{0x0a, SOMAGIC_DEFAULT_BRIGHTNESS}, // Luminance Brightness
	{0x0b, SOMAGIC_DEFAULT_CONTRAST}, // Luminance Contrast
	{0x0c, SOMAGIC_DEFAULT_SATURATION}, // Chrominance Saturation
	{0x0d, SOMAGIC_DEFAULT_HUE}, // Chrominance Hue
	{0x0e, 0x01}, // Chrominance CTRL // Chrominance Bandwidth = 800kHz - Colorstandard selection NTSC M/PAL BGHIN
	{0x0f, 0x2a}, // Chrominance gain control
	{0x10, 0x40}, // Format/Delay CTRL //pm
	{0x11, 0x0c}, // Output CTRL #1
	{0x12, 0x01}, // RTS0/RTS1 Output CTRL
	{0x13, 0x00}, // {0x13, 0x80}, // Output CTRL #2 //pm
	{0x14, 0x00}, // -- RESERVED
	{0x15, 0x00}, // VGATE Start
	{0x16, 0x00}, // VGATE Stop
	{0x17, 0x00}, // VGATE MSB
	// 0x18 - 0x3f   -- RESERVED
	{0x40, 0x82}, // Slicer CTRL //pm
	{0x41, 0x77}, // Line Control Register2
	{0x42, 0x77}, // Line Control Register3
	{0x43, 0x77}, // Line Control Register4
	{0x44, 0x77}, // Line Control Register5
	{0x45, 0x77}, // Line Control Register6
	{0x46, 0x77}, // Line Control Register7
	{0x47, 0x77}, // Line Control Register8
	{0x48, 0x77}, // Line Control Register9
	{0x49, 0x77}, // Line Control Register10
	{0x4a, 0x77}, // Line Control Register11
	{0x4b, 0x77}, // Line Control Register12
	{0x4c, 0x77}, // Line Control Register13
	{0x4d, 0x77}, // Line Control Register14
	{0x4e, 0x77}, // Line Control Register15
	{0x4f, 0x77}, // Line Control Register16
	{0x50, 0x77}, // Line Control Register17
	{0x51, 0x77}, // Line Control Register18
	{0x52, 0x77}, // Line Control Register19
	{0x53, 0x77}, // Line Control Register20
	{0x54, 0x77}, // Line Control Register21
	{0x55, 0xFF}, // Line Control Register22
	{0x56, 0xFF}, // Line Control Register23
	{0x57, 0xFF}, // Line Control Register24
	{0x58, 0x00}, // Programmable framing code
	{0x59, 0x54}, // Horiz. offset
	{0x5a, 0x0A}, // Vert. offset //pm
	{0x5b, 0x83}, // Field offset
	{0x5c, 0x00}, // -- RESERVED
	{0x5d, 0x00}, // -- RESERVED
	{0x5e, 0x00}, // Sliced data id code
	
	{0xff, 0xff} // END MARKER
};

	/*
   * Register 0x10 [OFTS1 OFTS0] [HDEL1 HDEL0] [VRLN] [YDEL2 YDEL1 YDEL0]
   *
   * YDEL (Y Delay | Luminance Delay) Legal values (-4 -> 3)
   *
   * VRLN = VREF Pulse-position, and length of VRLN
   * 	@60Hz (- 525 Lines) 0:240, 1:242 (0: (19 - 258 | 282 - 521), 1: (18 - 259 | 281 - 522))
   *	@50Hz (- 625 Lines) 0:286, 1:288 (0: (24 - 309 | 337 - 622), 1: (23 - 310 | 336 - 623))
   *
   * HDEL (Fineposition of HS) Legal values 0 -> 3
   * 
   * OFTS (Output Format Selection) 
   * 	0: STD ITU656 FMT
   * 	1: V-FLAG in TRC Generated by VREF
   * 	2: V-FLAG in TRC Generated by data-type
   * 	3: Reserved
   */

	/*
   * Register 0x13 [ADLSB] [---- ----][OLDSP] [FIDP] [----] [AOSL1 AOSL0]
   *
   * AOSL (Analog test select)
   * 	0: AOUT Connected to internal test point 1
   * 	1: -"-  Connected to input AD1 
   * 	2: -"-  Connected to input AD1 
   * 	3: -"-  Connected to internal test point 2
   *
   * FIDP (FieldId Polarity on RTS0 or RTS1) 0: Default, 1: Inverted
   *
   * OLDSP (Settings of StatusByte functionality) 0: Default, 1: OldCompatibility
   * ADLSB (A/D Output bits on VPO0-7) 0: AD8-AD1, 1: AD7-AD0
   */

	/*
   * Register 0x40 [FISET] [HAM_N] [FCE] [HUNT_N] [----] [CLKSEL1 CLKSEL0] [----]
   *
   * CLKSEL (Dataslicer clock selection) 1:13.5Mhz (All other values are reserved!)
   * HUNT_N (Amplitude searching) 0: Active, 1: Stopped
   * FCE (Framing code error) 0:One error allowed, 1:No errors allowed
   * HAM_N (Hamming check) 0: Hamming check for 2 bytes after framing code, depend on data-type. 0:No Hamming check
   * FISET (Field size select) 0: 50Hz Field rate, 1: 60Hz Field rate
   */

	/*
   * Register 0x5a [VOFF7-0] Allowed value 0 -> 312
   *   VOFF8 in 0x5b Bit4
   *
   * Defult:
   * 	50Hz 625 Lines = 0x07
   * 	60Hz 525 Lines = 0x0A
   *
   */

struct saa_setup saa_setupPAL[256] = {
	{0x01, 0x08},
	{0x02, 0xc0}, // FUSE0 + FUSE1 -- MODE0 (CVBS - Input)
	{0x03, 0x33}, // WHITEPEAK OFF | LONG V-BLANK -- AGC ON | AUTOMATIC GAIN MODE0-3 | GAI18 = 1 | GAI28 = 1
	{0x04, 0x00}, // GAI10-GAI17 Gain-control Input 1
	{0x05, 0x00}, // GAI20-GAI27 Gain-control Input 2
	{0x06, 0xe9}, // Horizontal-Sync Begin (-23)
	{0x07, 0x0d}, // Horizontal-Sync End (13)
	{0x08, 0x98}, // Sync CTRL (Automatic field detection (PAL / NTSC))
	{0x09, 0x01},	// Luminance CTRL (Aperture factor 0.25)
	{0x0a, SOMAGIC_DEFAULT_BRIGHTNESS}, // Luminance Brightness
	{0x0b, SOMAGIC_DEFAULT_CONTRAST}, // Luminance Contrast
	{0x0c, SOMAGIC_DEFAULT_SATURATION}, // Chrominance Saturation
	{0x0d, SOMAGIC_DEFAULT_HUE}, // Chrominance Hue
	{0x0e, 0x01}, // Chrominance CTRL // Chrominance Bandwidth = 800kHz - Colorstandard selection NTSC M/PAL BGHIN
	{0x0f, 0x2a}, // Chrominance gain control
	{0x10, 0x00}, // Format/Delay CTRL
	{0x11, 0x0c}, // Output CTRL #1
	{0x12, 0x01}, // RTS0/RTS1 Output CTRL
	{0x13, 0x00}, // Output CTRL #2
	{0x14, 0x00}, // -- RESERVED
	{0x15, 0x00}, // VGATE Start
	{0x16, 0x00}, // VGATE Stop
	{0x17, 0x00}, // VGATE MSB
	// 0x18 - 0x3f   -- RESERVED
	{0x40, 0x02}, // Slicer CTRL
	{0x41, 0x77}, // Line Control Register2
	{0x42, 0x77}, // Line Control Register3
	{0x43, 0x77}, // Line Control Register4
	{0x44, 0x77}, // Line Control Register5
	{0x45, 0x77}, // Line Control Register6
	{0x46, 0x77}, // Line Control Register7
	{0x47, 0x77}, // Line Control Register8
	{0x48, 0x77}, // Line Control Register9
	{0x49, 0x77}, // Line Control Register10
	{0x4a, 0x77}, // Line Control Register11
	{0x4b, 0x77}, // Line Control Register12
	{0x4c, 0x77}, // Line Control Register13
	{0x4d, 0x77}, // Line Control Register14
	{0x4e, 0x77}, // Line Control Register15
	{0x4f, 0x77}, // Line Control Register16
	{0x50, 0x77}, // Line Control Register17
	{0x51, 0x77}, // Line Control Register18
	{0x52, 0x77}, // Line Control Register19
	{0x53, 0x77}, // Line Control Register20
	{0x54, 0x77}, // Line Control Register21
	{0x55, 0xFF}, // Line Control Register22
	{0x56, 0xFF}, // Line Control Register23
	{0x57, 0xFF}, // Line Control Register24
	{0x58, 0x00}, // Programmable framing code
	{0x59, 0x54}, // Horiz. offset
	{0x5a, 0x07}, // Vert. offset
	{0x5b, 0x83}, // Field offset
	{0x5c, 0x00}, // -- RESERVED
	{0x5d, 0x00}, // -- RESERVED
	{0x5e, 0x00}, // Sliced data id code
	
	{0xff, 0xff} // END MARKER
};

static int saa_write(struct usb_somagic *somagic, int reg, int val)
{
	int rc;
	struct saa_i2c_data_struct {
		u8 magic;
		u8 i2c_write_address;
		u8 bmDevCtrl;
		u8 bmDataPointer;
		u8 loopCounter;
		u8 reg;
		u8 val;
		u8 reserved;
	} saa_i2c_write = {
		.magic = 0x0b,
		.i2c_write_address = 0x4a,
		.bmDevCtrl = 0xc0, 					// 1100 0000
		.bmDataPointer = 0x01, 			// HighNibble = Control Bits
		.loopCounter = 0x01,
		.reserved = 0x00
	};

	saa_i2c_write.reg = reg;
	saa_i2c_write.val = val;

	rc = usb_control_msg(somagic->dev,
											 usb_sndctrlpipe(somagic->dev, 0x00),
											 SOMAGIC_USB_STD_REQUEST,
											 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
											 0x0b, // VALUE
											 0x00, // INDEX
											 (void *)&saa_i2c_write,
											 sizeof(saa_i2c_write),
											 1000);
	if (rc < 0) {
		printk(KERN_ERR "somagic:%s:: error while trying to set saa7113 " \
                    "register %02x to %02x, usb subsytem returned %d\n",
										__func__, reg, val, rc);
		return -1;
	}

	return rc;
}

static int reg_write(struct usb_somagic *somagic, u16 reg, u8 val)
{
	int rc;
	struct reg_data_struct {
		u8 magic;
		u8 reserved;
		u8 bmDevCtrl;				// Bit 5 - 0: Write, 1: Read
		u8 bmDataPointer;		// Bit 0..3 : Data Offset!
		u8 loopCounter;
		u8 regHi;						// Big Endian Register
		u8 regLo;
		u8 val;
		u8 reserved1;
	} reg_write_data = {
		.magic = 0x0b,
		.reserved = 0x00,
		.bmDevCtrl = 0x00,
		.bmDataPointer = 0x82,
		.loopCounter = 0x01,
		.reserved1 = 0x00
	};

	/* FIXME: This is hardcoded for Little-Endian Systems.
	 * will probably break on Big-Endian
	 */
	reg_write_data.regHi = reg >> 8;
	reg_write_data.regLo = reg & 0xff;
	reg_write_data.val = val;

	rc = usb_control_msg(somagic->dev,
											 usb_sndctrlpipe(somagic->dev, 0x00),
											 SOMAGIC_USB_STD_REQUEST,
											 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
											 0x0b, // URB_VALUE
											 0x00, // URB_INDEX
											 (void *)&reg_write_data,
											 sizeof(reg_write_data),
										 	 1000);

	if (rc < 0) {
		printk(KERN_ERR "somagic::%s: Error while trying to set " \
                    "register %04x to %02x; usb subsytem returned %d\n",
										__func__, reg, val, rc);
		return -1;
	}

	printk(KERN_INFO "somagic::%s: Set register 0x%04x to 0x%02x\n",
				 __func__, reg, val);
	return rc;
}

/*
 * send_video_setup()
 *
 * Send the SAA7113 Setup commands to the device!
 */
static int send_video_setup(struct usb_somagic *somagic, v4l2_std_id tvnorm)
{
	int i,rc;
	u8 buf[2];
	const struct saa_setup *setup;

	// No need to send this more than once?	
	if (somagic->video.setup_sent) {
		return 0;
	}
	
	rc = usb_control_msg(somagic->dev,
									usb_rcvctrlpipe(somagic->dev, 0x80),
									SOMAGIC_USB_STD_REQUEST,
									USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
									0x01,	// VALUE
									0x00,	// INDEX
									(void *)&buf,
									sizeof(buf),
									SOMAGIC_URB_STD_TIMEOUT);

	printk(KERN_INFO "somagic:%s:: First Ctrl msg returned %d bytes: %02x %02x\n",
										__func__, rc, buf[0], buf[1]);

	if (rc < 0 || buf[1] != 0x03) {
		printk(KERN_ERR "somagic:%s:: error: " \
                    "Device is in unexpected state!\n",
										__func__);
		return -1;
	}

	/* Set DDRA = 0x80 */
	reg_write(somagic, 0x003a, 0x80);

	/* Toggle PORTA (RESET of SAA7xxx & Audiochip) */
	reg_write(somagic, 0x003b, 0x80);
	reg_write(somagic, 0x003b, 0x00);

	somagic->video.cur_input = INPUT_CVBS;
	somagic->video.cur_std = tvnorm;
	somagic->video.cur_brightness = SOMAGIC_DEFAULT_BRIGHTNESS;
	somagic->video.cur_contrast = SOMAGIC_DEFAULT_CONTRAST;
	somagic->video.cur_saturation = SOMAGIC_DEFAULT_SATURATION;
	somagic->video.cur_hue = SOMAGIC_DEFAULT_HUE;

	if (tvnorm == V4L2_STD_PAL) {
		setup = saa_setupPAL;
		somagic->video.field_lines = SOMAGIC_STD_FIELD_LINES_PAL;
		printk("somagic::%s: Setup PAL!\n", __func__);
	} else {
		setup = saa_setupNTSC;
		somagic->video.field_lines = SOMAGIC_STD_FIELD_LINES_NTSC;
		printk("somagic::%s: Setup NTSC!\n", __func__);
	}

	somagic->video.frame_size = somagic->video.field_lines * 2 * SOMAGIC_BYTES_PER_LINE;

	for(i=0; setup[i].reg != 0xff; i++) {
		rc = saa_write(somagic, setup[i].reg, setup[i].val);
		if (rc < 0) {
			return -1;
		}
	}

	printk(KERN_INFO "somagic:%s:: SAA7113 Setup sent!\n",
										__func__);
	somagic->video.setup_sent = 1;
	return 0;
}

/*****************************************************************************/
/*																																					 */
/* 						Device initialization and disconnection												 */
/* 																																					 */
/*****************************************************************************/

int __devinit somagic_dev_init(struct usb_interface *intf)
{
	struct usb_device *dev = usb_get_dev(interface_to_usbdev(intf));
	struct usb_somagic *somagic = kzalloc(sizeof(struct usb_somagic), GFP_KERNEL);
	int rc;

	if (somagic == NULL) {
		dev_err(&intf->dev, "%s: Error, could not allocate memory for the somagic struct\n",
						__func__);
		return -ENOMEM;
	}

	somagic->dev = dev;

	usb_set_intfdata(intf, somagic);

	rc = allocate_isoc_buffer(somagic);
	if (rc != 0) {
		goto err_exit;
	}

	// v4l2_std_id default_norm = (default_ntsc) ? V4L2_STD_NTSC : V4L2_STD_PAL;
	send_video_setup(somagic, V4L2_STD_PAL);

	rc = somagic_v4l2_init(somagic);
	if (rc != 0) {
		goto err_exit;
	}

	rc = somagic_alsa_init(somagic);
	if (rc != 0) {
		goto err_exit;
	}

	create_sysfs(somagic->dev);

	return 0;

	err_exit: {
		free_isoc_buffer(somagic);
		kfree(somagic);
		return -ENODEV;
	}
}

void __devexit somagic_dev_exit(struct usb_interface *intf)
{
	struct usb_somagic *somagic = usb_get_intfdata(intf);
	if (somagic == NULL) {
		return;
	}
	
	remove_sysfs(somagic->dev);

	free_isoc_buffer(somagic);

	somagic_v4l2_exit(somagic);
	somagic_alsa_exit(somagic);

	somagic->dev = NULL;
	kfree(somagic);

	printk(KERN_INFO "somagic::%s: Disconnect complete!\n", __func__);
}

// Set video standard NTSC | PAL
int somagic_dev_video_set_std(struct usb_somagic *somagic, v4l2_std_id id)
{
	int i, rc;
	static const struct v_std {
		int reg;
		int val;
	}	ntsc[] = {
		{0x10, 0x40}, // Format/Delay CTRL
		{0x40, 0x82}, // Slicer CTRL
		{0x5a, 0x0A}, // Vert. offset
		{0xff, 0xff} // END MARKER
	};

	static const struct v_std pal[] = {
		{0x10, 0x00}, // Format/Delay CTRL
		{0x40, 0x02}, // Slicer CTRL
		{0x5a, 0x07}, // Vert. offset
		{0xff, 0xff} // END MARKER
	};

	const struct v_std *std;

	printk(KERN_INFO "somagic::%s: Cur std is %s, User requests standard %s\n",
         __func__,
         v4l2_norm_to_name(somagic->video.cur_std),
         v4l2_norm_to_name(id));

	if ((somagic->video.cur_std & id) == id) {
		return 0;
	}

	// Not sure what will happen if we change this while we are streaming!
	// Could probably be tested!
	if (somagic->streaming_flags & SOMAGIC_STREAMING_STARTED) {
		printk(KERN_INFO "somagic::%s: Warning: application is trying to "\
                     "change tv-standard while streaming!\n", __func__);	
		return -EAGAIN;
	}

	if ((id & V4L2_STD_NTSC) == id) {
		printk(KERN_INFO "somagic::%s: Set device to NTSC!\n", __func__);
		std = ntsc;
		somagic->video.cur_std = V4L2_STD_NTSC;
		somagic->video.field_lines = SOMAGIC_STD_FIELD_LINES_NTSC;
	} else if ((id & V4L2_STD_PAL) == id) {
		printk(KERN_INFO "somagic::%s: Set device to PAL!\n", __func__);
		std = pal;
		somagic->video.cur_std = V4L2_STD_PAL;
		somagic->video.field_lines = SOMAGIC_STD_FIELD_LINES_PAL;
	} else {
		printk(KERN_INFO "somagic::%s: Warning: "\
                     "Application tries to set unsupported tv-standard!\n",
           __func__);	
		return -EINVAL;
	}

	somagic->video.frame_size = somagic->video.field_lines * 2 * SOMAGIC_BYTES_PER_LINE;

	for(i=0; std[i].reg != 0xff; i++) {
		rc = saa_write(somagic, std[i].reg, std[i].val);
		if (rc < 0) {
			return -EAGAIN;
		}
	}

	return 0;
}

int somagic_dev_video_set_input(struct usb_somagic *somagic, unsigned int input)
{
	enum somagic_inputs new_input = (enum somagic_inputs)input;
	if (new_input == somagic->video.cur_input) {
		return 0;
	}
	if (new_input >= INPUT_MANY) {
		return -EINVAL;
	}
	/* FIXME:
	 * Register 0x09 Contains the current luminance settings,
	 * this will discard any luminance changes made by the user!
	 */

	if (new_input == INPUT_CVBS) {
		saa_write(somagic, 0x02, 0xC0);
		saa_write(somagic, 0x03, 0x33);
		saa_write(somagic, 0x09, 0x01);
		saa_write(somagic, 0x13, 0x80);
	} else if (new_input == INPUT_SVIDEO) {
		saa_write(somagic, 0x02, 0xC7);
		saa_write(somagic, 0x03, 0x31);
		saa_write(somagic, 0x09, 0x81);
		saa_write(somagic, 0x13, 0x00);
	}
	somagic->video.cur_input = new_input;
	return 0;
}

void somagic_dev_video_set_brightness(struct usb_somagic *somagic, s32 value)
{
	if (value < 0 || value > (u8)0xff) {
		return;
	}
	saa_write(somagic, 0x0a, value);
	somagic->video.cur_brightness = value;
}

void somagic_dev_video_set_contrast(struct usb_somagic *somagic, s32 value)
{
	if (value > 127 || value < -128) {
		return;
	}
	saa_write(somagic, 0x0b, value);
	somagic->video.cur_contrast = value;
}

void somagic_dev_video_set_saturation(struct usb_somagic *somagic, s32 value)
{
	if (value > 127 || value < -128) {
		return;
	}
	saa_write(somagic, 0x0c, value);
	somagic->video.cur_saturation = value;
}

void somagic_dev_video_set_hue(struct usb_somagic *somagic, s32 value)
{
	if (value > 127 || value < -128) {
		return;
	}
	saa_write(somagic, 0x0d, value);
	somagic->video.cur_hue = value;
}

int somagic_start_stream(struct usb_somagic *somagic)
{

	int buf_idx,rc;
	u8 data[2];
	data[0] = 0x01;
	data[1] = 0x05;

	/* TODO: Check that we have allocated the ISOC memory (fbuf) */

	/* Check that we have not started the streaming already */
	if (somagic->streaming_flags & SOMAGIC_STREAMING_STARTED) {
		return 0;
	}

	rc = usb_control_msg(somagic->dev,
											 usb_sndctrlpipe(somagic->dev, 0x00),
											 SOMAGIC_USB_STD_REQUEST,
											 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
											 0x01, // VALUE
											 0x00, // INDEX
											 (void *)&data,
											 sizeof(data),
											 1000);
	if (rc < 0) {
		printk(KERN_ERR "somagic:%s:: error while trying to initialize device for " \
                    "videostreaming, recieved usb error: %d\n",
										__func__, rc);
		return -1;
	}


	rc = usb_set_interface(somagic->dev, 0, 2);
	if (rc < 0) {
		printk(KERN_ERR "somagic:%s:: Failed to set alt_setting 2 on interface 0",
										__func__);
		return -1;
	}

	/*
	 * Start Audio streaming
	 * 0x1d = 0001 1101
	 */
	rc = reg_write(somagic, 0x1740, 0x1d);
	if (rc < 0) {
		return -1;
	}

	// Submit urbs
	for (buf_idx = 0; buf_idx < SOMAGIC_NUM_ISOC_BUFFERS; buf_idx++) {
		rc = usb_submit_urb(somagic->isoc_buf[buf_idx].urb, GFP_KERNEL);
		if (rc) {
			dev_err(&somagic->dev->dev,
							"%s: usb_submit_urb(%d) failed: error %d\n",
							__func__, buf_idx, rc);
			printk(KERN_INFO "somagic:%s:: Failed to send ISOC requests!\n", __func__);
			return -1;
		}
	}

	somagic->streaming_flags |= SOMAGIC_STREAMING_STARTED;
	printk(KERN_INFO "somagic:%s:: Started stream ISOC_TRANSFER\n", __func__);

	return 0;
}

void somagic_stop_stream(struct usb_somagic *somagic)
{
	int rc;
	int buf_idx;
	u8 data[2];

	if ((somagic->streaming_flags & SOMAGIC_STREAMING_CAPTURE_MASK) != 0x00) {
		printk(KERN_INFO "somagic::%s: Stop requested, "
					 					 "but we still have a consumer!\n", __func__);
		return;
	}
	
	if (!(somagic->streaming_flags & SOMAGIC_STREAMING_STARTED)) {
		return;
	}

	somagic->streaming_flags &= ~SOMAGIC_STREAMING_STARTED;

	for (buf_idx = 0; buf_idx < SOMAGIC_NUM_ISOC_BUFFERS; buf_idx++) {
		usb_kill_urb(somagic->isoc_buf[buf_idx].urb);
	}

	printk(KERN_INFO "somagic::%s: Stopped stream ISOC_TRANSFER!", __func__);

	data[0] = 0x01;
	data[1] = 0x03;

	rc = usb_control_msg(somagic->dev,
											 usb_sndctrlpipe(somagic->dev, 0x00),
											 SOMAGIC_USB_STD_REQUEST,
											 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
											 0x01, // VALUE
											 0x00, // INDEX
											 (void *)&data,
											 sizeof(data),
											 1000);
	if (rc < 0) {
		printk(KERN_ERR "somagic:%s:: error while trying to set device" \
                    "to idle mode: %d\n",
										__func__, rc);
	}

	rc = usb_set_interface(somagic->dev, 0, 0);
	if (rc < 0) {
		printk(KERN_ERR "somagic:%s:: error while trying to set" \
                    "alt interface to 0: %d\n",
										__func__, rc);
	}
}

