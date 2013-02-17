/*******************************************************************************
 * somagic_audio.c                                                             *
 *                                                                             *
 * USB Driver for Somagic EasyCAP DC60                                         *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This file is part of easycap-somagic-linux
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

#include "somagic.h"

static const struct snd_pcm_hardware somagic_pcm_hw = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 65280,	/* 1020 * 32Packets * 2 Periods */
	.period_bytes_min = 32640,	/* 1020 * 32Packets * 1 Periode */
	.period_bytes_max = 65280,
	.periods_min = 1,
	.periods_max = 2
};

static int somagic_pcm_open(struct snd_pcm_substream *substream)
{
	struct somagic_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}

	dev->pcm_substream = substream;
	substream->runtime->private_data = dev;
	substream->runtime->hw = somagic_pcm_hw;
	snd_pcm_hw_constraint_integer(substream->runtime,
					SNDRV_PCM_HW_PARAM_PERIODS);
	dev->pcm_dma_offset = 0;
	dev->pcm_packets = 0;
	dev->snd_elapsed_periode = false;

	return 0;
}

static int somagic_pcm_close(struct snd_pcm_substream *substream)
{
	struct somagic_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}

	dev->pcm_substream = NULL;

	return 0;
}


static int somagic_pcm_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int somagic_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct somagic_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}

	return snd_pcm_lib_free_pages(substream);	
}

static int somagic_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

/* This callback is ATOMIC, must not sleep */
static int somagic_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct somagic_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}
	switch(cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev->pcm_dma_offset = 0;
		dev->pcm_packets = 0;
		goto end_ok;
	case SNDRV_PCM_TRIGGER_STOP:
		goto end_ok;
	default:
		return -EINVAL;
	}

end_ok:
	return 0;
}

static snd_pcm_uframes_t somagic_pcm_pointer(
					struct snd_pcm_substream *substream)
{
	struct somagic_dev *dev = snd_pcm_substream_chip(substream);
	if (!dev->udev) {
		return -ENODEV;
	}

	return dev->pcm_dma_write_ptr / 8;	
}

static struct snd_pcm_ops somagic_pcm_ops = {
	.open = somagic_pcm_open,
	.close = somagic_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = somagic_pcm_hw_params,
	.hw_free = somagic_pcm_hw_free,
	.prepare = somagic_pcm_prepare,
	.trigger = somagic_pcm_trigger,
	.pointer = somagic_pcm_pointer,
};

int somagic_snd_register(struct somagic_dev *dev)
{
	int rc = 0;

	rc = snd_card_create(SNDRV_DEFAULT_IDX1, "SOMAGIC",
						THIS_MODULE, 0, &dev->snd_card);
	if (rc < 0) {
		return rc;
	}

	rc = snd_pcm_new(dev->snd_card, "somagic_dc60_pcm", 0, 0, 1, &dev->snd_pcm);
	if (rc < 0) {
		goto err_free_card;
	}

	snd_pcm_set_ops(dev->snd_pcm, SNDRV_PCM_STREAM_CAPTURE, &somagic_pcm_ops);
	dev->snd_pcm->info_flags = 0;
	dev->snd_pcm->private_data = dev;
	strcpy(dev->snd_pcm->name, "somagic_dc60_pcm");

	rc = snd_pcm_lib_preallocate_pages_for_all(dev->snd_pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_KERNEL), 65280, 65280);
	if (rc < 0) {
		goto err_free_card;
	}

	strcpy(dev->snd_card->driver, "ALSA driver");
	strcpy(dev->snd_card->shortname, "Somagic DC60");
	strcpy(dev->snd_card->longname, "somagic_dc60_pcm");

	rc = snd_card_register(dev->snd_card);
	if (rc < 0) {
		goto err_free_pages;
	}

	return 0;

err_free_pages:
	snd_pcm_lib_preallocate_free_for_all(dev->snd_pcm);
err_free_card:
	snd_card_free(dev->snd_card);
	dev->snd_card = NULL;
	return rc;
}

void somagic_snd_unregister(struct somagic_dev *dev)
{
	if (!dev->snd_card) {
		return;
	}

	snd_pcm_lib_preallocate_free_for_all(dev->snd_pcm);
	snd_card_free(dev->snd_card);
	dev->snd_card = NULL;
}

void somagic_audio(struct somagic_dev *dev, u8 *data, int len)
{
	struct snd_pcm_runtime *runtime;
	int len_part;
	u8 *buffer;

	if (!dev->udev) {
		return;
	}

	if (!dev->pcm_substream) {
		return;
	}

	runtime = dev->pcm_substream->runtime;
	buffer = runtime->dma_area;

	if (dev->pcm_dma_write_ptr + len < runtime->dma_bytes) {
		memcpy(buffer + dev->pcm_dma_write_ptr, data, len);
		dev->pcm_dma_write_ptr += len;
	} else {
		len_part = runtime->dma_bytes - dev->pcm_dma_write_ptr;
		memcpy(buffer + dev->pcm_dma_write_ptr, data, len_part);
		if (len == len_part) {
			dev->pcm_dma_write_ptr = 0;
		} else {
			memcpy(buffer, data + len_part, len - len_part);
			dev->pcm_dma_write_ptr = len - len_part;
		}
	}
	dev->snd_elapsed_periode = true;
}

