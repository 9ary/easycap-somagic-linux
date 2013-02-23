/*******************************************************************************
 * smi2021_audio.c                                                             *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCap                                            *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This file is part of SMI2021
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
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#include "smi2021.h"

static const struct snd_pcm_hardware smi2021_pcm_hw = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_BATCH, /*          |
		SNDRV_PCM_INFO_MMAP_VALID, */

	.formats = SNDRV_PCM_FMTBIT_S32_LE,

	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = 992,	/* 32640 */ /* 15296 */
	.period_bytes_max = 15872,	/* 65280 */
	.periods_min = 1,		/* 1 */
	.periods_max = 16,		/* 2 */
	.buffer_bytes_max = 65280,	/* 65280 */
};

/*
static unsigned int fmts[] = { SNDRV_PCM_FMTBIT_S32_LE };
static struct snd_pcm_hw_constraint_list constraint_fmts = {
	.count = ARRAY_SIZE(fmts),
	.list = fmts,
	.mask = 0,
};
*/

static int smi2021_pcm_open(struct snd_pcm_substream *substream)
{
	struct smi2021_dev *dev = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (!dev->udev) {
		return -ENODEV;
	}

	runtime->hw = smi2021_pcm_hw;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
/*
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
						&constraint_fmts);
*/
	dev->pcm_substream = substream;

	return 0;
}

static int smi2021_pcm_close(struct snd_pcm_substream *substream)
{
	struct smi2021_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}

	if (substream->runtime->dma_area) {
		vfree(substream->runtime->dma_area);
		substream->runtime->dma_area = NULL;
	}

	return 0;
}


static int smi2021_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	size_t size = params_buffer_bytes(hw_params);

	if (runtime->dma_area) {
		if (runtime->dma_bytes > size) {
			return 0;
		}
		vfree(runtime->dma_area);
	}
	runtime->dma_area = vmalloc(size);
	if (!runtime->dma_area) {
		return -ENOMEM;
	}
	runtime->dma_bytes = size;

	return 0;
}

static int smi2021_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct smi2021_dev *dev = snd_pcm_substream_chip(substream);

	if (!dev->udev) {
		return -ENODEV;
	}
	
	if (atomic_read(&dev->adev_capturing)) {
		atomic_set(&dev->adev_capturing, 0);
		schedule_work(&dev->adev_capture_trigger);
	}

	return 0;
}

static int smi2021_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct smi2021_dev *dev = snd_pcm_substream_chip(substream);

	dev->pcm_dma_offset = 0;
	dev->pcm_packets = 0;
	dev->snd_elapsed_periode = false;

	return 0;
}

static void capture_trigger(struct work_struct *work)
{
	struct smi2021_dev *dev = container_of(work, struct smi2021_dev,
					adev_capture_trigger);

	if (atomic_read(&dev->adev_capturing)) {
		smi2021_write_reg(dev, 0, 0x1740, 0x1d);
	} else { 
		smi2021_write_reg(dev, 0, 0x1740, 0x00);
/*
		dev->snd_elapsed_periode = false;
*/
	}
}

/* This callback is ATOMIC, must not sleep */
static int smi2021_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct smi2021_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}
	switch(cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE: /* fall through */
	case SNDRV_PCM_TRIGGER_RESUME: /* fall through */
	case SNDRV_PCM_TRIGGER_START:
		atomic_set(&dev->adev_capturing, 1);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH: /* fall through */
	case SNDRV_PCM_TRIGGER_SUSPEND: /* fall through */
	case SNDRV_PCM_TRIGGER_STOP:
		atomic_set(&dev->adev_capturing, 0);
		break;
	default:
		return -EINVAL;
	}

	schedule_work(&dev->adev_capture_trigger);

	return 0;
}

static snd_pcm_uframes_t smi2021_pcm_pointer(
					struct snd_pcm_substream *substream)
{
	struct smi2021_dev *dev = snd_pcm_substream_chip(substream);

	if (!dev->udev) {
		return -ENODEV;
	}

	return dev->pcm_dma_write_ptr / 8;

}

static struct page *smi2021_pcm_get_vmalloc_page(struct snd_pcm_substream *subs,
						unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;

	return vmalloc_to_page(pageptr);
}

static struct snd_pcm_ops smi2021_pcm_ops = {
	.open = smi2021_pcm_open,
	.close = smi2021_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = smi2021_pcm_hw_params,
	.hw_free = smi2021_pcm_hw_free,
	.prepare = smi2021_pcm_prepare,
	.trigger = smi2021_pcm_trigger,
	.pointer = smi2021_pcm_pointer,
	.page = smi2021_pcm_get_vmalloc_page,
};

int smi2021_snd_register(struct smi2021_dev *dev)
{
	struct snd_card	*card;
	struct snd_pcm *pcm;
	int rc = 0;

	rc = snd_card_create(SNDRV_DEFAULT_IDX1, "smi2021 Audio", THIS_MODULE,
				0, &card);
	if (rc < 0) {
		return rc;
	}

	rc = snd_pcm_new(card, "smi2021 Audio", 0, 0, 1, &pcm);
	if (rc < 0) {
		goto err_free_card;
	}

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &smi2021_pcm_ops);
	pcm->info_flags = 0;
	pcm->private_data = dev;
	strcpy(pcm->name, "Somagic smi2021 Capture");

	strcpy(card->driver, "smi2021-Audio");
	strcpy(card->shortname, "smi2021 Audio");
	strcpy(card->longname, "Somagic smi2021 Audio");

	INIT_WORK(&dev->adev_capture_trigger, capture_trigger);

	rc = snd_card_register(card);
	if (rc < 0) {
		goto err_free_card;
	}

	dev->snd_card = card;	

	return 0;

err_free_card:
	snd_card_free(card);
	return rc;
}

void smi2021_snd_unregister(struct smi2021_dev *dev)
{
	if (!dev) {
		return;
	}

	if (!dev->snd_card) {
		return;
	}

	snd_card_free(dev->snd_card);
	dev->snd_card = NULL;
}

void smi2021_audio(struct smi2021_dev *dev, u8 *data, int len)
{
	struct snd_pcm_runtime *runtime;
	unsigned int oldptr;
	int stride;
	int overflow_len = 0;
	u8 offset;

	u8 new_offset = 0;

	if (!dev->udev) {
		return;
	}

	if (atomic_read(&dev->adev_capturing) == 0) {
		return;
	}

	if (!dev->pcm_substream) {
		return;
	}
	offset = dev->pcm_dma_offset;
	runtime = dev->pcm_substream->runtime;
	stride = runtime->frame_bits >> 3;

	/* The device is actually sending 24Bit pcm data 
	 * with 0x00 as the header byte before each sample.
	 * We look for this byte to make sure we did not
	 * loose any bytes during transfer.
	 */
	while(len > stride && (data[offset] != 0x00 ||
			data[offset + stride] != 0x00)) {
		offset = 0;
		new_offset++;
		data++;
		len--;
	}

	if (len <= stride) {
		/* We exhausted the buffer looking for 0x00 */
		return;
	}

	if (new_offset != 0) {
		printk(KERN_DEBUG "ovflw: %d, new_offset: %d\n",
			dev->pcm_overflow, new_offset);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16, 1,
			data, 64, 1);

		/* lost sync */
		dev->pcm_dma_offset = new_offset % stride;

		snd_pcm_stream_lock(dev->pcm_substream);
		/* We might have got the first part of a frame in the last urb,
		 * we move the pointer to consume a complete frame.
		 */
		dev->pcm_dma_write_ptr += (stride - dev->pcm_overflow);

		if (dev->pcm_dma_write_ptr >= runtime->dma_bytes) {
			dev->pcm_dma_write_ptr -= runtime->dma_bytes;
		}

		dev->pcm_packets++;
		if (dev->pcm_packets >= runtime->period_size) {
			dev->pcm_packets -= runtime->period_size;
			dev->snd_elapsed_periode = true;
		}

		snd_pcm_stream_unlock(dev->pcm_substream);
	}

	dev->pcm_overflow = (len % stride);

	printk_ratelimited(KERN_DEBUG
		"Complete frames in buffer: %d, extra bytes: %d, periode_size: %d, buf_size: %d\n",
		(len + overflow_len) / stride, (len + overflow_len) % stride,
		runtime->period_size, runtime->dma_bytes);

	oldptr = dev->pcm_dma_write_ptr;
	if (oldptr + len >= runtime->dma_bytes) {
		unsigned int cnt = runtime->dma_bytes - oldptr;
		memcpy(runtime->dma_area + oldptr, data, cnt);
		memcpy(runtime->dma_area, data + cnt, len - cnt);
	} else {
		memcpy(runtime->dma_area + oldptr, data, len);
	}

	snd_pcm_stream_lock(dev->pcm_substream);
	dev->pcm_dma_write_ptr += len + overflow_len;
	if (dev->pcm_dma_write_ptr >= runtime->dma_bytes) {
		dev->pcm_dma_write_ptr -= runtime->dma_bytes;
	}

	dev->pcm_packets += len / stride;
	if (dev->pcm_packets >= runtime->period_size) {
		dev->pcm_packets -= runtime->period_size;
		dev->snd_elapsed_periode = true;
	}
	dev->snd_elapsed_periode = true;
	snd_pcm_stream_unlock(dev->pcm_substream);
}
