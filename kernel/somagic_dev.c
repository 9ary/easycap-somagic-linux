#include "somagic.h"

/* Scratch buffer */
#define DEFAULT_SCRATCH_BUF_SIZE (0x20000) // 128kB memory scratch buffer
static const int scratch_buf_size = DEFAULT_SCRATCH_BUF_SIZE;

static int scratch_len(struct usb_somagic *somagic)
{
	int len = somagic->video.scratch_write_ptr - somagic->video.scratch_read_ptr;

	if (len < 0) {
		len += scratch_buf_size;
	}

	return len;
}

/*
 * scratch_free()
 *
 * Returns the free space left in buffer
 */
static int scratch_free(struct usb_somagic *somagic)
{
	int free = somagic->video.scratch_read_ptr - somagic->video.scratch_write_ptr;
	if (free <= 0) {
		free += scratch_buf_size;
	}

	if (free) {
		// At least one byte in the buffer must be left blank,
		// otherwise ther is no chance to differ between full and empty
		free -= 1;
	}

	return free;
}

static int scratch_put(struct usb_somagic *somagic,
												unsigned char *data, int len)
{
	int len_part;

	if (somagic->video.scratch_write_ptr + len < scratch_buf_size) {
		memcpy(somagic->video.scratch + somagic->video.scratch_write_ptr,
						data, len);
		somagic->video.scratch_write_ptr += len;
	} else {
		len_part = scratch_buf_size - somagic->video.scratch_write_ptr;
		memcpy(somagic->video.scratch + somagic->video.scratch_write_ptr,
						data, len_part);

		if (len == len_part) {
			somagic->video.scratch_write_ptr = 0;
		} else {
			memcpy(somagic->video.scratch, data + len_part, len - len_part);
			somagic->video.scratch_write_ptr = len - len_part;
		}
	}

	return len;
}

static int scratch_get(struct usb_somagic *somagic,
											 unsigned char *data, int len)
{
	int len_part;

	if (somagic->video.scratch_read_ptr + len < scratch_buf_size) {
		memcpy(data, somagic->video.scratch + somagic->video.scratch_read_ptr,
						len);
		somagic->video.scratch_read_ptr += len;
	} else {
		len_part = scratch_buf_size - somagic->video.scratch_read_ptr;
		memcpy(data, somagic->video.scratch + somagic->video.scratch_read_ptr,
						len_part);

		if (len == len_part) {
			somagic->video.scratch_read_ptr = 0;
		} else {
			memcpy(data + len_part, somagic->video.scratch, len - len_part);
			somagic->video.scratch_read_ptr = len - len_part;
		}
	}

	return len;
}

static void scratch_rm_old(struct usb_somagic *somagic, int len)
{
	somagic->video.scratch_read_ptr += len;
	somagic->video.scratch_read_ptr %= scratch_buf_size;
}

static void scratch_reset(struct usb_somagic *somagic)
{
	somagic->video.scratch_read_ptr = 0;
	somagic->video.scratch_write_ptr = 0;
}

/*
 * somagic_dev_video_alloc_scratch()
 *
 * Allocate memory for the scratch - ring buffer
 */
int somagic_dev_video_alloc_scratch(struct usb_somagic *somagic)
{
	somagic->video.scratch = vmalloc_32(scratch_buf_size);
	scratch_reset(somagic);

	if (somagic->video.scratch == NULL) {
		dev_err(&somagic->dev->dev,
						"%s: unable to allocate %d bytes for scratch\n",
						__func__, scratch_buf_size);
		return -ENOMEM;
	}

	return 0;
}

/*
 * somagic_dev_video_free_scratch()
 *
 * Free the scratch - ring buffer
 */
void somagic_dev_video_free_scratch(struct usb_somagic *somagic)
{
	vfree(somagic->video.scratch);
	somagic->video.scratch = NULL;
}

/*****************************************************************************/
/*																																					 */
/* 																																					 */
/* 																																					 */
/*****************************************************************************/

static const struct saa_setup {
	int reg;
	int val;
} saa_setupPAL[256] = {
	{0x01, 0x08},
	{0x02, 0xc0}, // FUSE0 + FUSE1 -- MODE0 (CVBS - Input)
	{0x03, 0x33}, // WHITEPEAK OFF | LONG V-BLANK -- AGC ON | AUTOMATIC GAIN MODE0-3 | GAI18 = 1 | GAI28 = 1
	{0x04, 0x00}, // GAI10-GAI17 Gain-control Input 1
	{0x05, 0x00}, // GAI20-GAI27 Gain-control Input 2
	{0x06, 0xe9}, // Horizontal-Sync Begin (-23)
	{0x07, 0x0d}, // Horizontal-Sync End (13)
	{0x08, 0x98}, // Sync CTRL (Automatic field detection (PAL / NTSC))
	{0x09, 0x01},	// Luminance CTRL (Aperture factor 0.25)
	{0x0a, 0x80}, // Luminance Brightness
	{0x0b, 0x40}, // Luminance Contrast
	{0x0c, 0x40}, // Chrominance Saturation
	{0x0d, 0x00}, // Chrominance Hue
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
	{0x41, 0xFF}, // Line Control Register2
	{0x42, 0xFF}, // Line Control Register3
	{0x43, 0xFF}, // Line Control Register4
	{0x44, 0xFF}, // Line Control Register5
	{0x45, 0xFF}, // Line Control Register6
	{0x46, 0xFF}, // Line Control Register7
	{0x47, 0xFF}, // Line Control Register8
	{0x48, 0xFF}, // Line Control Register9
	{0x49, 0xFF}, // Line Control Register10
	{0x4a, 0xFF}, // Line Control Register11
	{0x4b, 0xFF}, // Line Control Register12
	{0x4c, 0xFF}, // Line Control Register13
	{0x4d, 0xFF}, // Line Control Register14
	{0x4e, 0xFF}, // Line Control Register15
	{0x4f, 0xFF}, // Line Control Register16
	{0x50, 0xFF}, // Line Control Register17
	{0x51, 0xFF}, // Line Control Register18
	{0x52, 0xFF}, // Line Control Register19
	{0x53, 0xFF}, // Line Control Register20
	{0x54, 0xFF}, // Line Control Register21
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
		printk(KERN_ERR "somagic:%s:: error while trying to set " \
                    "register %04x to %02x; usb subsytem returned %d\n",
										__func__, reg, val, rc);
		return -1;
	}

	return rc;
}

/*
 * somagic_dev_init_video()
 *
 * Send the SAA7113 Setup commands to the device!
 */
int somagic_dev_init_video(struct usb_somagic *somagic, v4l2_std_id std)
{
	int i,rc;
	u8 buf[2];

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


	for(i=0; saa_setupPAL[i].reg != 0xff; i++) {
		rc = saa_write(somagic, saa_setupPAL[i].reg, saa_setupPAL[i].val);
		if (rc < 0) {
			return -1;
		}
	}

	printk(KERN_INFO "somagic:%s:: SAA7113 Setup sent!\n",
										__func__);
	somagic->video.setup_sent = 1;
	return 0;
}


/*
static inline void write_frame(struct somagic_frame *frame, u8 c)
{
	frame->data[frame->scanlength++] = c;
}

static enum parse_state find_SAV(struct usb_somagic *somagic)
{
	unsigned char c;
	struct somagic_frame *frame = somagic->video.cur_frame;
	enum parse_state rc = PARSE_STATE_CONTINUE;

	scratch_get(somagic, &c, 1);

// 	 * Timing reference code (TRC)	:
// 	 * 		[ff 00 00 SAV] [ff 00 00 EAV]
// 	 * A Line of video will look like 1448 bytes total:
// 	 * [ff 00 00 SAV] [1440 bytes of active video (UYVY)] [ff 00 00 EAV]


	switch(frame->line_sync) {
		case HSYNC : {
			if (c == 0xff) {
				frame->line_sync++;
			}
			break;			
		}
		case SYNCZ1 : // Same handler
		case SYNCZ2 : {
			if (c == 0x00) {
				frame->line_sync++;
			}
			break;
		}	
		case SYNCAV : {
			// Found 0xff 0x00 0x00, now expecting SAV or EAV.
			// Might also be SDID (Sliced data id), 0x00.
			if (c == 0x00) { // SDID
				frame->line_sync = HSYNC;
				break;
			}

			// If Bit4 = 1 We are in EAV, else it's SAV
			if (c & 0x10) {
				frame->line_sync = HSYNC;
				break;
			} else { // In SAV
 //				 * F (Field Bit) = Bit 6 (mask 0x40)
 //				 * 0: Odd, 1: Even
 //				 *
 //				 * V (Vertical Blanking) = Bit 5 (mask 0x20)
 //				 * 0: in VBI, 1: in active video
				if (((c & 0x20) == 0) && !frame->odd_read) { // In VBI
						// Wait for active video
						frame->line_sync = HSYNC;
						break;
				}
				if (c & 0x40) { // Even field!
					if (!frame->odd_read) {
						// Wait for odd field!
						frame->line_sync = HSYNC;
						break;
					}
					frame->even_read = 1;
				} else { // Odd Field!
					frame->odd_read = 1;
				}
				frame->sav = c;
				somagic->video.scan_state = SCAN_STATE_LINES;

				if (c & 0x20) { // V-BLANK = Bit 5, 0: in VBI 1: active video 
					frame->scanstate = SCAN_STATE_LINES;
				} else {
					frame->scanstate = SCAN_STATE_VBI;
					if (frame->scanlength > 525) {
						rc = PARSE_STATE_NEXT_FRAME;
					}
				}
			}
		}
	}
	return rc;
}
static enum parse_state parse_lines(struct usb_somagic *somagic)
{
	struct somagic_frame *frame;
	u32 trash;
	int len = 1440;
	
	frame = somagic->video.cur_frame;

	if (scratch_len(somagic) < len + 4 ) { // Include EAV (4 Bytes)
		printk(KERN_INFO "somagic::%s: Scratchbuffer underrun\n", __func__);
		return PARSE_STATE_OUT;
	}

	scratch_get(somagic, &frame->data[frame->scanlength], len);
	frame->scanlength += len;

	// Throw away EAV (End of Active data marker)
	scratch_get(somagic, (unsigned char *)&trash, 4);

	frame->scanstate = SCAN_STATE_SCANNING;

	return PARSE_STATE_CONTINUE;
}
*/

/* One PAL frame is 905000 Bytes, including TRC - (625 Lines).
 * An ODD Field is 4517756 Bytes, including TRC - (312 Lines).
 * An EVEN Field is 453224 Bytes, including TRC - (313 Lines).
 */
static enum parse_state parse_data(struct usb_somagic *somagic)
{
	int j;
	struct somagic_frame *frame;
	u8 c;

	frame = somagic->video.cur_frame;

	while(scratch_len(somagic)) {
		scratch_get(somagic, &c, 1);

		if (frame->scanlength > 900000) {
			frame->scanlength = 0;
			printk(KERN_INFO "somagic::%s: Frame-error: buffer overflow!\n", __func__);
		}

		switch(frame->line_sync) {
			case HSYNC : {
				if (c == 0xff) {
					frame->line_sync++;
				} else {
					frame->data[frame->scanlength++] = c;
				}
				break;
			}
			case SYNCZ1 :
				if (c == 0x00) {
					frame->line_sync++;
				} else {
					frame->data[frame->scanlength++] = 0xff;
					frame->data[frame->scanlength++] = c;
					frame->line_sync = HSYNC;
				}
				break;
			case SYNCZ2 : {
				if (c == 0x00) {
					frame->line_sync++;
				} else {
					frame->data[frame->scanlength++] = 0xff;
					frame->data[frame->scanlength++] = 0x00;
					frame->data[frame->scanlength++] = c;
					frame->line_sync = HSYNC;
				}
				break;
			}
			case SYNCAV : {
				frame->line_sync = HSYNC;

				if ((c & 0x10) == 0x10)	{ 	// TRC _ End of Line (EAV)
						frame->line++;
						if (frame->line == 625) {
							printk(KERN_INFO "somagic::%s: We have a full frame!, size is %d bytes!\n", __func__, frame->scanlength);
							return PARSE_STATE_NEXT_FRAME;
							// FOR DEBUG!
							// frame->scanlength = 0;
							// frame->line = 0;
							
						}
				}
			}
		}
	}
/*
				if (somagic->video.scan_state == SCANNER_FIND_ODD) {
					if ((c & 0x10) == 0)	{ 	// TRC _ Start of Line (SAV)
						if ((c & 0x20) == 0x20) { 	// In Active Video // ELSE VBI
							if ((c & 0x40) == 0) { // ODD
*/
	
	return PARSE_STATE_CONTINUE;
}

static void somagic_dev_isoc_video_irq(struct urb *urb)
{
	unsigned long lock_flags;
	int i,j,rc,debugged = 0;
	struct usb_somagic *somagic = urb->context;
	struct somagic_frame **f;
	enum parse_state state;

	if (urb->status == -ENOENT) {
		printk(KERN_INFO "somagic::%s: Recieved empty ISOC urb!\n", __func__);
		return;
	}

	somagic->video.received_urbs++;


	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned char *data = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		int packet_len = urb->iso_frame_desc[i].actual_length;
		int pos = 0;


		if ((packet_len % 0x400) != 0) {
				printk(KERN_INFO "somagic::%s: recieved ISOC packet with "\
							           "unknown size! Size is %d\n",
								__func__, packet_len);
		}
/*
 		if (somagic->video.received_urbs < 4 && i == 0) {
			printk(KERN_INFO "somagic::PACKAGE_DUMP\n");
			for(j = 0; j<0x400; j+=0x10) {
				printk(KERN_INFO "\t%02x %02x %02x %02x %02x %02x %02x %02x "\
												 "%02x %02x %02x %02x %02x %02x %02x %02x\n",
							 data[j], data[j+1], data[j+2], data[j+3],
							 data[j+4], data[j+5], data[j+6], data[j+7],
							 data[j+8], data[j+9], data[j+10], data[j+11],
							 data[j+12], data[j+13], data[j+14], data[j+15]);
			}
		}
*/
		while(pos < packet_len) {
			/*
			 * Within each packet of transfer, the data is divided into blocks of 0x400 (1024) bytes
			 * beginning with [0xaa 0xaa 0x00 0x00],
		 	 * Check for this signature, and pass each block to scratch buffer for further processing
		 	 */

			if (data[pos] == 0xaa && data[pos+1] == 0xaa &&
					data[pos+2] == 0x00 && data[pos+3] == 0x00) {
				// We have data, put it on scratch-buffer
				scratch_put(somagic, &data[pos+4], 0x400 - 4);
//			} else if (somagic->video.received_urbs < 4 && pos == 0 && i == 0) {
			} else {
				printk(KERN_INFO "somagic::%s: Unexpected block, "\
							 "expected [0xaa 0xaa 0x00 0x00], found [%02x %02x %02x %02x]\n",
							 __func__, data[pos], data[pos+1], data[pos+2], data[pos+3]);
/*
				if (debugged == 0) {
					debugged = 1;
					printk(KERN_INFO "somagic::PACKAGE_DUMP\n");
					for(j = 0; j<0x400; j+=0x10) {
							printk(KERN_INFO "\t%02x %02x %02x %02x %02x %02x %02x %02x "\
															 "%02x %02x %02x %02x %02x %02x %02x %02x\n",
										 data[pos+j], data[pos+j+1], data[pos+j+2], data[pos+j+3],
										 data[pos+j+4], data[pos+j+5], data[pos+j+6], data[pos+j+7],
										 data[pos+j+8], data[pos+j+9], data[pos+j+10], data[pos+j+11],
										 data[pos+j+12], data[pos+j+13], data[pos+j+14], data[pos+j+15]);
					}
				}
*/
			}
			pos += 0x400;
		}
	}


	// We check if we have a v4l2_framebuffer to fill!
	f = &somagic->video.cur_frame;
	if (scratch_len(somagic) > 0x800 && !list_empty(&(somagic->video.inqueue))) {
		if (!(*f)) { // cur_frame == NULL
			(*f) = list_entry(somagic->video.inqueue.next,
												struct somagic_frame, frame);
		}
	
		state = parse_data(somagic);

		if (state == PARSE_STATE_NEXT_FRAME) {
			(*f)->grabstate = FRAME_STATE_DONE;
			do_gettimeofday(&((*f)->timestamp));
			(*f)->sequence = somagic->video.frame_num;

			spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
			list_move_tail(&((*f)->frame), &somagic->video.outqueue);
			somagic->video.cur_frame = NULL;
			spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);

			somagic->video.frame_num++;

			// Hang here, wait for new frame!
			if (waitqueue_active(&somagic->video.wait_frame)) {
				wake_up_interruptible(&somagic->video.wait_frame);
			}
		}
	}


	for (i = 0; i < 32; i++) { // 32 = NUM_URB_FRAMES	
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

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

int somagic_dev_video_start_stream(struct usb_somagic *somagic)
{

	struct usb_device *dev = somagic->dev;
	int buf_idx,rc;
	u8 data[2];
	data[0] = 0x01;
	data[1] = 0x05;

	/* TODO: Check that we have allocated the ISOC memory (fbuf) */

	if (somagic->video.streaming) {
		return 0;
	}

	somagic->video.scan_state = SCANNER_FIND_VBI;
	somagic->video.cur_frame = NULL;
	scratch_reset(somagic);

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
	printk(KERN_INFO "somagic:%s:: Changed to alternate setting 2 on interface 0\n",
										__func__);

	rc = reg_write(somagic, 0x1740, 0x00);
	if (rc < 0) {
		printk(KERN_ERR "somagic:%s:: Failed to set 0x1740 to 0x00\n",
										__func__);
		return -1;
		
	}
	printk(KERN_INFO "somagic:%s:: Set reg 0x1740 to 0x00\n", __func__);

	// Submit urbs
	for (buf_idx = 0; buf_idx < 2; buf_idx++) { // 2 = NUM_SBUF
		rc = usb_submit_urb(somagic->video.sbuf[buf_idx].urb, GFP_KERNEL);
		if (rc) {
			dev_err(&somagic->dev->dev,
							"%s: usb_submit_urb(%d) failed: error %d\n",
							__func__, buf_idx, rc);
		}
	}

	somagic->video.streaming = 1;
	printk(KERN_INFO "somagic:%s:: Sent urbs!\n", __func__);

	return 0;
}

void somagic_dev_video_stop_stream(struct usb_somagic *somagic)
{
	somagic->video.streaming = 0;
}

int somagic_dev_video_alloc_isoc(struct usb_somagic *somagic)
{
	int buf_idx, rc;
	int sb_size = 32 * 3072; // 32 = NUM_URB_FRAMES * 3072 = VIDEO_ISOC_PACKET_SIZE

	for (buf_idx = 0; buf_idx < 2; buf_idx++) { // 2 = NUM_SBUF
		int j,k;
		struct urb *urb;

		urb = usb_alloc_urb(32, GFP_KERNEL); // 32 = NUM_URB_FRAMES
		if (urb == NULL) {
			dev_err(&somagic->dev->dev,
			"%s: usb_alloc_urb() failed\n", __func__);
			return -ENOMEM;
		}

		somagic->video.sbuf[buf_idx].urb = urb;
		somagic->video.sbuf[buf_idx].data =
				usb_alloc_coherent(somagic->dev,
													sb_size,
													GFP_KERNEL,
													&urb->transfer_dma);

		urb->dev = somagic->dev;
		urb->context = somagic;
		urb->pipe = usb_rcvisocpipe(somagic->dev, 0x82);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = 1;
		urb->transfer_buffer = somagic->video.sbuf[buf_idx].data;
		urb->complete = somagic_dev_isoc_video_irq;
		urb->number_of_packets = 32; // 32 = NUM_URB_FRAMES
		urb->transfer_buffer_length = sb_size;
		for (j = k = 0; j < 32; j++, k += 3072) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = 3072;
		}
	}

	printk(KERN_INFO "somagic::%s: Allocated ISOC urbs!\n", __func__);

	return 0;
}

void somagic_dev_video_free_isoc(struct usb_somagic *somagic)
{
	int buf_idx, rc;
	int sb_size = 32 * 3072;

	for (buf_idx = 0; buf_idx < 2; buf_idx++) {
		usb_kill_urb(somagic->video.sbuf[buf_idx].urb);

		if (somagic->video.sbuf[buf_idx].data) {
			usb_free_coherent(somagic->dev,
												sb_size,
												somagic->video.sbuf[buf_idx].data,
												somagic->video.sbuf[buf_idx].urb->transfer_dma);
		}

		usb_free_urb(somagic->video.sbuf[buf_idx].urb);
		somagic->video.sbuf[buf_idx].urb = NULL;
	}

	printk(KERN_INFO "somagic::%s: Freed ISOC urbs!\n", __func__);
}

