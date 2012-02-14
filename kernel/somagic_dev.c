#include "somagic.h"

/*****************************************************************************/
/*                                                                           */
/*            Scratch Buffer                                                 */
/*                                                                           */
/*            Ring-buffer used to store the bytes received from              */
/*            in the isochronous transfers while doing capture.              */
/*							                                                             */
/*            The ring-buffer is read when the driver processes the data,    */
/*            and the data is then copied to the frame-buffers               */
/*            that we shift between kernelspace & userspace                  */
/*                                                                           */
/*****************************************************************************/

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
 *
 * NOT USED, UNCOMMENT IF NEEDED!
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
*/

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
/*                                                                           */
/*            Frame Buffers                                                  */
/*                                                                           */
/*            The frames are passed between kernelspace & userspace          */
/*            while the capture is running.                                  */
/*                                                                           */
/*            All the buffers is stored in one big chunk of memory.          */
/*            Each somagic_frame struct has a pointer to a different         */
/*            offset in this memory                                          */
/*                                                                           */
/*            The passing of the somagic_frame structs                       */
/*            is handeled in somagic_video.c by                              */
/*            vidioc_dqbuf() and vidioc_qbuf()                               */
/*                                                                           */
/*****************************************************************************/
static void *somagic_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem) {
		return NULL;
	}

	memset(mem, 0, size);
	adr = (unsigned long)mem;
	while ((long) size > 0) {
		SetPageReserved(vmalloc_to_page((void*)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void somagic_rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem) {
		return;
	}

	size = PAGE_ALIGN(size);
	adr = (unsigned long) mem;

	while((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vfree(mem);
}

// Allocate Buffer for somagic->video.fbuf
//
// This is the buffer that will hold the frame data received from the device,
// when we have stripped of the TRC header & footer of each line.
//
// Return the number of frames we managed to allocate!
//
int somagic_dev_video_alloc_frames(struct usb_somagic *somagic,
                                   int number_of_frames)
{
	int i;

	somagic->video.max_frame_size = PAGE_ALIGN(720 * 2 * 627 * 2);
	somagic->video.num_frames = number_of_frames;

	/* This function will be called from userspace
   * by a V4L2 API Call (vidioc_reqbufs).
   *
   * We must try to allocate the requested frames,
   * but if we don't have the memory we decrease by 
   * one frame and try again.
   */

	while (somagic->video.num_frames > 0) {
		somagic->video.fbuf_size = somagic->video.num_frames *
				somagic->video.max_frame_size;

		somagic->video.fbuf = somagic_rvmalloc(somagic->video.fbuf_size);
		if (somagic->video.fbuf) {
			// Success, we managed to allocate a frame buffer
			break;
		}
		somagic->video.num_frames--;
	}

	// Initialize locks and waitqueue
	spin_lock_init(&somagic->video.queue_lock);
	init_waitqueue_head(&somagic->video.wait_frame);
	init_waitqueue_head(&somagic->video.wait_stream);

	// Setup the frames
	for (i = 0; i < somagic->video.num_frames; i++) {
		somagic->video.frame[i].index = i;	
		somagic->video.frame[i].grabstate = FRAME_STATE_UNUSED;
		somagic->video.frame[i].data = somagic->video.fbuf + 
			(i * somagic->video.max_frame_size);
	}

	return somagic->video.num_frames;
}

void somagic_dev_video_free_frames(struct usb_somagic *somagic)
{
	if (somagic->video.fbuf != NULL) {
		somagic_rvfree(somagic->video.fbuf, somagic->video.fbuf_size);
		somagic->video.fbuf = NULL;

		somagic->video.num_frames = 0;
	}
}

void somagic_dev_video_empty_framequeues(struct usb_somagic *somagic)
{
	int i;
	INIT_LIST_HEAD(&(somagic->video.inqueue));
	INIT_LIST_HEAD(&(somagic->video.outqueue));

	for (i = 0; i < somagic->video.num_frames; i++) {
		somagic->video.frame[i].grabstate = FRAME_STATE_UNUSED;
		somagic->video.frame[i].bytes_read = 0;	
	}
}

/*****************************************************************************/
/*																																					 */
/* 																																					 */
/* 																																					 */
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
int somagic_dev_init_video(struct usb_somagic *somagic, v4l2_std_id tvnorm)
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

/**
 * Write data to frame buffer.
 * Interleave Odd & Even fields, and put VBI data in bottom of framebuffer.
 *
 * When we copy the buffer to userspace, we only copy the first 576 / 480 Lines.
 * The rest of the buffer is only Vertical Blanking
 */
static inline void fill_frame(struct somagic_frame *frame, u8 c)
{
	int line_pos;

	if (frame->field > 1) {
		printk(KERN_INFO "somagic::%s: Frame->field value is out of range!\n", __func__);
		frame->field = 0;
	}

	line_pos = (2 * frame->line + frame->field) * (720 * 2) + frame->col;
	frame->col++;
	
	if (frame->col > 720 * 2) {
		frame->col = 720 * 2;
	}

	if (line_pos > (720 * 2 * 627 * 2)) {
		printk(KERN_INFO "somagic::%s: line_pos is larger than buffer\n", __func__);
		line_pos = 0;
	}

	frame->data[line_pos] = c;
	frame->scanlength++;
}

/* One PAL frame is 905000 Bytes, including TRC - (625 Lines).
 * An ODD Field is 4517756 Bytes, including TRC - (312 Lines). (288 Active video lines + 24 Lines of VBI)
 * An EVEN Field is 453224 Bytes, including TRC - (313 Lines). (288 Actice video lines + 25 Lines of VBI)
 *
 * VLC allocates: 831488 pr frame!
 *
 * mplayer expects one PAL frame to be: 829440
 * That is 576 (288 * 2) lines of 1440 (720 * 2)Bytes
 *
 */
static enum parse_state parse_data(struct usb_somagic *somagic)
{
	struct somagic_frame *frame;
	u8 c;

	frame = somagic->video.cur_frame;

	while(scratch_len(somagic)) {
		scratch_get(somagic, &c, 1);

		if (frame->scanlength > (720 * 2 * 627 * 2)) {
			frame->scanlength = 0;
			printk(KERN_INFO "somagic::%s: Frame-error: buffer overflow!\n", __func__);
		}

		switch(frame->line_sync) {
			case HSYNC : {
				if (c == 0xff) {
					frame->line_sync++;
				} else {
					fill_frame(frame, c);
				}
				break;
			}
			case SYNCZ1 :
				if (c == 0x00) {
					frame->line_sync++;
				} else {
					fill_frame(frame, 0xff);
					fill_frame(frame, c);
					frame->line_sync = HSYNC;
				}
				break;
			case SYNCZ2 : {
				if (c == 0x00) {
					frame->line_sync++;
				} else {
					fill_frame(frame, 0xff);
					fill_frame(frame, 0x00);
					fill_frame(frame, c);
					frame->line_sync = HSYNC;
				}
				break;
			}
			case SYNCAV : {
				frame->line_sync = HSYNC;

				if (c == 0x00) { // SDID (Sliced Data ID)
					break;
				}

				if ((c & 0x10) == 0x10)	{ 	// TRC _ End of active data (EAV)
					frame->line++;
					frame->col = 0;
					if (frame->line > 313) {
						printk(KERN_INFO "somagic::%s: SYNC Error, got line number %d\n", __func__, frame->line);						
						frame->line = 313;
					}
				} else { // TRC _ Start of active data (SAV)
					/* SAV
 					 * 
 					 * F (Field bit) = Bit 6 (mask 0x40)
 					 * 0: Odd Field;
 					 * 1: Even Field;
 					 *
 					 * V (Vertical blanking bit) = Bit 5 (mask 0x20)
 					 * 0: in VBI
 					 * 1: in Active video
 					 */
					int field_edge;
					int blank_edge;

					field_edge = frame->field;
					blank_edge = frame->blank;

					frame->field = (c & 0x40) ? 1 : 0;
					frame->blank = (c & 0x20) ? 1 : 0; // 0 = In VBI!

					field_edge = frame->field ^ field_edge;
					blank_edge = frame->blank ^ blank_edge;

					if (frame->field == 0 && field_edge) {
//					printk(KERN_INFO "somagic::%s: We have a full frame!, size is %d bytes!\n", __func__, frame->scanlength);
						/* We Should have a full frame by now.
 						 * If we don't; reset this frame and try again
 						 */
						  if (frame->scanlength < somagic->video.frame_size) {
							frame->scanlength = 0;
							frame->line = 0;
							frame->col = 0;
							continue;
						}
						return PARSE_STATE_NEXT_FRAME;
					}

					if (frame->blank == 0 && blank_edge) {
						frame->line = 0;
						frame->col = 0;
					}
				}
			}
		}
	}
	
	return PARSE_STATE_CONTINUE;
}

static void somagic_dev_isoc_video_irq(struct urb *urb)
{
	unsigned long lock_flags;
	int i,rc;
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

			} else {

				printk(KERN_INFO "somagic::%s: Unexpected block, "\
							 "expected [0xaa 0xaa 0x00 0x00], found [%02x %02x %02x %02x]\n",
							 __func__, data[pos], data[pos+1], data[pos+2], data[pos+3]);
			}

			// Skip to next 0xaa 0xaa 0x00 0x00
			pos += 0x400;
		}
	}


	// We check if we have a v4l2_framebuffer to fill!
	f = &somagic->video.cur_frame;
	if (scratch_len(somagic) > 0x800 && !list_empty(&(somagic->video.inqueue))) {

		//printk(KERN_INFO "somagic::%s: Parsing Data", __func__);

		if (!(*f)) { // cur_frame == NULL
			(*f) = list_entry(somagic->video.inqueue.next,
												struct somagic_frame, frame);
			(*f)->scanlength = 0;
		}
	
		state = parse_data(somagic);

		if (state == PARSE_STATE_NEXT_FRAME) {
			// This should never occur, don't know if we need to check this here?
			if ((*f)->scanlength > somagic->video.frame_size) {
				(*f)->scanlength = somagic->video.frame_size;
			}

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
	if (somagic->video.streaming == 1) {
		printk(KERN_INFO "somagic::%s: Warning: application is trying to "\
                     "change tv-standard while streaming!\n", __func__);	
		return -EAGAIN;
	}

/*
	printk(KERN_INFO "somagic::%s: User requests standard 0x%08X%08X\n", __func__,
				 (int)((id & (((v4l2_std_id)0xFFFFFFFF) << 32 )) >> 32),
				 (int)(id & ((v4l2_std_id)0xFFFFFFFF)));	
*/


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

int somagic_dev_video_start_stream(struct usb_somagic *somagic)
{

	int buf_idx,rc;
	u8 data[2];
	data[0] = 0x01;
	data[1] = 0x05;

	/* TODO: Check that we have allocated the ISOC memory (fbuf) */

	if (somagic->video.streaming) {
		return 0;
	}

	// somagic->video.scan_state = SCANNER_FIND_VBI;

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
	int rc;
	u8 data[2];

	somagic->video.streaming = 0;

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

int somagic_dev_video_alloc_isoc(struct usb_somagic *somagic)
{
	int buf_idx;
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
	int buf_idx;
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

