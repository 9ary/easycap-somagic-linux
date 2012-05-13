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

static int scratch_len(struct usb_somagic *somagic)
{
	int len = somagic->scratch_write_ptr - somagic->scratch_read_ptr;

	if (len < 0) {
		len += SOMAGIC_SCRATCH_BUF_SIZE;
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
	int free = somagic->scratch_read_ptr - somagic->scratch_write_ptr;
	if (free <= 0) {
		free += SOMAGIC_SCRATCH_BUF_SIZE;
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

	if (somagic->scratch_write_ptr + len < SOMAGIC_SCRATCH_BUF_SIZE) {
		memcpy(somagic->scratch + somagic->scratch_write_ptr,
						data, len);
		somagic->scratch_write_ptr += len;
	} else {
		len_part = SOMAGIC_SCRATCH_BUF_SIZE - somagic->scratch_write_ptr;
		memcpy(somagic->scratch + somagic->scratch_write_ptr,
						data, len_part);

		if (len == len_part) {
			somagic->scratch_write_ptr = 0;
		} else {
			memcpy(somagic->scratch, data + len_part, len - len_part);
			somagic->scratch_write_ptr = len - len_part;
		}
	}

	return len;
}

static int scratch_get_custom(struct usb_somagic *somagic, int *ptr,
                              unsigned char *data, int len)
{
	int len_part;

	if (*ptr + len < SOMAGIC_SCRATCH_BUF_SIZE) {
		memcpy(data, somagic->scratch + *ptr, len);
	  *ptr += len;
	} else {
		len_part = SOMAGIC_SCRATCH_BUF_SIZE - *ptr;
		memcpy(data, somagic->scratch + *ptr, len_part);

		if (len == len_part) {
			*ptr = 0;
		} else {
			memcpy(data + len_part, somagic->scratch, len - len_part);
			*ptr = len - len_part;
		}
	}

	return len;
}

static inline void scratch_create_custom_pointer(struct usb_somagic *somagic,
                                                 int *ptr, int offset)
{
	*ptr = (somagic->scratch_read_ptr + offset) % SOMAGIC_SCRATCH_BUF_SIZE;
} 

static inline int scratch_get(struct usb_somagic *somagic,
											 unsigned char *data, int len)
{
	return scratch_get_custom(somagic, &(somagic->scratch_read_ptr),
														data, len);
}

static void scratch_reset(struct usb_somagic *somagic)
{
	somagic->scratch_read_ptr = 0;
	somagic->scratch_write_ptr = 0;
}

/*
 * allocaate_scratch_buffer()
 *
 * Allocate memory for the scratch - ring buffer
 */
static int allocate_scratch_buffer(struct usb_somagic *somagic)
{
	somagic->scratch = vmalloc_32(SOMAGIC_SCRATCH_BUF_SIZE);
	scratch_reset(somagic);

	if (somagic->scratch == NULL) {
		dev_err(&somagic->dev->dev,
						"%s: unable to allocate %d bytes for scratch\n",
						__func__, SOMAGIC_SCRATCH_BUF_SIZE);
		return -ENOMEM;
	}

	return 0;
}

/*
 * free_scratch_buffer()
 *
 * Free the scratch - ring buffer
 */
static void free_scratch_buffer(struct usb_somagic *somagic)
{
	if (somagic->scratch == NULL) {
		return;
	}
	vfree(somagic->scratch);
	somagic->scratch = NULL;
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
/*                                                                           */
/*            USB transfer                                                   */
/*                                                                           */
/*****************************************************************************/

// Some declarations!
static void process_video(unsigned long somagic_addr);
static void process_audio(unsigned long somagic_addr);

/*
 * isoc_complete
 *
 * This is an interrupt handler.
 * Called when we recieve isochronous usb-data from the device
 */
static void isoc_complete(struct urb *urb)
{
	u8 print = 0;
	int i,rc,audio_wp;
	struct usb_somagic *somagic = urb->context;
	struct timeval now;

	if (urb->status == -ENOENT) {
		printk(KERN_INFO "somagic::%s: Recieved empty ISOC urb!\n", __func__);
		return;
	}

	do_gettimeofday(&now);
	if (somagic->received_urbs != 0 && printk_ratelimit()) {
		printk(KERN_INFO "somagic::%s: %lld usecs since last interrupt ended!\n",
					 __func__, (long long)(now.tv_usec - somagic->prev_timestamp.tv_usec));
	}

	somagic->received_urbs++;
	audio_wp = somagic->audio.dma_write_ptr;

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
			 * beginning with [0xaa 0xaa 0x00 0x00] for video, or [0xaa 0xaa 0x00 0x01] for audio.
		 	 * Check for this signature, and pass each block to scratch buffer for further processing
		 	 */

			if (data[pos] == 0xaa && data[pos+1] == 0xaa &&
					data[pos+2] == 0x00 && data[pos+3] == 0x00) {
				// We have video data, put it on scratch-buffer
				scratch_put(somagic, &data[pos+4], 0x400 - 4);

			} else if (data[pos] == 0xaa && data[pos+1] == 0xaa &&
								 data[pos+2] == 0x00 && data[pos+3] == 0x01) {

				if (somagic->audio.streaming) {
					struct snd_pcm_runtime *runtime = somagic->audio.pcm_substream->runtime;
					memcpy(runtime->dma_area + somagic->audio.dma_write_ptr,
								 data + 4, (0x400 - 4));

					somagic->audio.dma_write_ptr += (0x400 - 4);
					if (somagic->audio.dma_write_ptr >= runtime->dma_bytes) {
						somagic->audio.dma_write_ptr = 0;
					}
				}
			} else if (printk_ratelimit()) {
				printk(KERN_INFO "somagic::%s: Unexpected block, "\
							 "expected [aa aa 00 00], found [%02x %02x %02x %02x]\n",
							 __func__, data[pos], data[pos+1], data[pos+2], data[pos+3]);
			}

			// Skip to next 0xaa 0xaa 0x00 0x00
			pos += 0x400;
		}
	}

	if (audio_wp != somagic->audio.dma_write_ptr) {
		tasklet_hi_schedule(&(somagic->audio.process_audio));
	}

	tasklet_hi_schedule(&(somagic->video.process_video));

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

	do_gettimeofday(&(somagic->prev_timestamp));

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

	tasklet_init(&(somagic->video.process_video), process_video, (unsigned long)somagic);
	tasklet_init(&(somagic->audio.process_audio), process_audio, (unsigned long)somagic);

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

	tasklet_kill(&(somagic->video.process_video));
	tasklet_kill(&(somagic->audio.process_audio));

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

	rc = allocate_scratch_buffer(somagic);
	if (rc != 0) {
		goto err_exit;
	}

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

	rc = somagic_connect_audio(somagic);
	if (rc != 0) {
		goto err_exit;
	}

	return 0;

	err_exit: {
		free_scratch_buffer(somagic);
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

	free_scratch_buffer(somagic);
	free_isoc_buffer(somagic);

	somagic_v4l2_exit(somagic);
	somagic_disconnect_audio(somagic);

	somagic->dev = NULL;
	kfree(somagic);

	printk(KERN_INFO "somagic::%s: Disconnect complete!\n", __func__);
}

/*****************************************************************************/
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*****************************************************************************/

static void find_sync(struct usb_somagic *somagic)
{
	int look_ahead;
	u16 check;
	u8 c;
	u8 trc[3];

	u8 cur_vbi, cur_field;

	while(1) {
		if (scratch_len(somagic) < 1) {
			break;
		}

		scratch_get(somagic, &c, 1);
		if (c != 0xff) {
			continue;
		}

		if (scratch_len(somagic) < sizeof(trc)) {
			break;
		}

		scratch_create_custom_pointer(somagic, &look_ahead, 0);
		scratch_get_custom(somagic, &look_ahead, (unsigned char *)&check, sizeof(check));
		if (check != 0x0000) {
			continue;
		}

		// We have found [0xff 0x00 0x00]. Now find SAV/EAV
		scratch_get(somagic, trc, sizeof(trc));
		if (trc[2] == 0x00 || (trc[2] & 0x10) == 0x10) { // This is EAV (or SDID)
			continue;
		}

		cur_vbi = (trc[2] & 0x20) >> 5;
		cur_field = (trc[2] & 0x40) >> 6;

		if (somagic->video.cur_sync_state == SYNC_STATE_SEARCHING) {
			somagic->video.prev_field = cur_field;
			somagic->video.cur_sync_state = SYNC_STATE_UNSTABLE;
			continue;
		}
		
		if (cur_field == 0 && somagic->video.prev_field == 1) {
			somagic->video.cur_sync_state = SYNC_STATE_STABLE;
			return;
		}

		somagic->video.prev_field = cur_field;
	}
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
static u8 parse_lines(struct usb_somagic *somagic)
{
	struct somagic_frame *frame;
	u8 c;
	int look_ahead;
	u8 check[8];

	frame = somagic->video.cur_frame;

	while(scratch_len(somagic) >= 1448) {
		scratch_create_custom_pointer(somagic, &look_ahead, 1440);
		scratch_get_custom(somagic, &look_ahead, check, 8);
		if (check[0] == 0xff && check[1] == 0x00 && check[2] == 0x00) {
			int line_pos = (2 * frame->line + frame->field) * (720 * 2) + frame->col;
			scratch_get(somagic, frame->data + line_pos, 1440);
			frame->scanlength += 1440;

			/*
       * Just grab the TRC including EAV of this line, and handle it here!
       * check is already holding this info when we reach this part.
       * But we need to increment the regular scratch_read_ptr,
       * so we just grab the code again.
       *
       * Notice: We only read 4 bytes,
       * the last 4 bytes of check is containing data from the last call
       * to scratch_get_custom
			 */
			scratch_get(somagic, check, 4);

			if ((check[3] & 0x10) == 0x10)	{ // Double check that this actually is EAV
					
				frame->line++;
				frame->col = 0;
				if (frame->line > 313) {
					printk(KERN_WARNING "somagic::%s: SYNC Error, got line number %d\n", __func__, frame->line);						
					frame->line = 313;
				}

				// Now we check that the scratch containes the SAV of the next line
				if (check[4] == 0xff && check[5] == 0x00 && check[6] == 0x00) {
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
					u8 field_edge;
					u8 blank_edge;

					// Again, we do this to increment the scratch_read_ptr!
					scratch_get(somagic, check + 4, 4);
					c = check[7];

					field_edge = frame->field;
					blank_edge = frame->blank;

					frame->field = (c & 0x40) >> 6;
					frame->blank = (c & 0x20) >> 5;

					field_edge = frame->field ^ field_edge;
					blank_edge = frame->blank ^ blank_edge;

					if (frame->field == 0 && field_edge) {
						if (frame->scanlength < somagic->video.frame_size) {
							// This frame is not a full frame, something went wrong!
							if (printk_ratelimit())	{
								printk(KERN_INFO "somagic::%s: Got partial video, "\
                       "resetting sync state!\n", __func__);
							}
							somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;
						}
						return 1;
					}

					if (frame->blank == 0 && blank_edge) {
						frame->line = 0;
						frame->col = 0;
					}
					// We have sync, so we try to read next line!
					continue;	
				}
			} // Data is not followed by EAV
		} else {// We dont have FF 00 00 at 1440
			if (printk_ratelimit()) {
				printk(KERN_INFO "somagic::%s: Lost sync on line %d, "\
  	                     "swapping out current frame & resetting sync state!\n", __func__, frame->line);
			}
			somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;
			return 1;
		}
	}
	return 0;
}

static enum parse_state parse_data(struct usb_somagic *somagic)
{
	struct somagic_frame *frame;
	frame = somagic->video.cur_frame;

	while(1) {
		if (somagic->video.cur_sync_state != SYNC_STATE_STABLE) {
			find_sync(somagic);
			if (somagic->video.cur_sync_state != SYNC_STATE_STABLE) {
				return PARSE_STATE_OUT;
			}	
			frame->col = 0;
			frame->scanlength = 0;
			frame->line = 0;
			frame->field = 0;
			frame->blank = 1;
		}

		if (parse_lines(somagic)) {
			return PARSE_STATE_NEXT_FRAME;
		} else {
			return PARSE_STATE_OUT;
		}
	}
	return PARSE_STATE_CONTINUE;
}

/*
 * process_video
 *
 * This is called after the first part of the isochronous usb data-transfer
 */
static void process_video(unsigned long somagic_addr)
{
	enum parse_state state;
	struct somagic_frame **f;
	unsigned long lock_flags;
	struct usb_somagic *somagic = (struct usb_somagic *)somagic_addr;

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
			(*f)->sequence = somagic->video.framecounter;

			spin_lock_irqsave(&somagic->video.queue_lock, lock_flags);
			list_move_tail(&((*f)->frame), &somagic->video.outqueue);
			somagic->video.cur_frame = NULL;
			spin_unlock_irqrestore(&somagic->video.queue_lock, lock_flags);

			somagic->video.framecounter++;

			// Hang here, wait for new frame!
			if (waitqueue_active(&somagic->video.wait_frame)) {
				wake_up_interruptible(&somagic->video.wait_frame);
			}
		}
	}
}

static void process_audio(unsigned long somagic_addr)
{
	struct usb_somagic *somagic = (struct usb_somagic *)somagic_addr;
	snd_pcm_period_elapsed(somagic->audio.pcm_substream);
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

	/* 0x1d = 0001 1101 */
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

	somagic->video.streaming = 1;
	printk(KERN_INFO "somagic:%s:: Sent urbs!\n", __func__);

	return 0;
}

void somagic_dev_video_stop_stream(struct usb_somagic *somagic)
{
	int rc;
	u8 data[2];

	somagic->video.streaming = 0;
	somagic->video.framecounter = 0;
	somagic->video.cur_sync_state = SYNC_STATE_SEARCHING;

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

