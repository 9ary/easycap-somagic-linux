#include "somagic.h"

static const struct snd_pcm_hardware pcm_hardware = {
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
	.buffer_bytes_max = 32640, // 1020 Bytes * 32 Usb packets!
	.period_bytes_min = 3060,
	.period_bytes_max = 32640,
	.periods_min = 1,
	.periods_max = 127
};

static int somagic_pcm_open(struct snd_pcm_substream *substream)
{
	struct usb_somagic *somagic = snd_pcm_substream_chip(substream);
	if (!somagic) {
		return -ENODEV;
	}

	printk(KERN_INFO "somagic::%s Called!, %d users\n", __func__,
				 somagic->audio.users);

	somagic->audio.users++;
	somagic->audio.pcm_substream = substream;
	substream->runtime->private_data = somagic;
	substream->runtime->hw = pcm_hardware;

	snd_pcm_hw_constraint_integer(substream->runtime,
																SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

static int somagic_pcm_close(struct snd_pcm_substream *substream)
{
	struct usb_somagic *somagic = snd_pcm_substream_chip(substream);

	printk(KERN_INFO "somagic::%s Called!, %d users\n", __func__,
				 somagic->audio.users);

	somagic->audio.users--;

	return 0;
}

static int somagic_pcm_hw_params(struct snd_pcm_substream *substream,
																 struct snd_pcm_hw_params *hw_params)
{
	printk(KERN_INFO "somagic::%s: Allocating %d bytes buffer\n",
				 __func__, params_buffer_bytes(hw_params));
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int somagic_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int somagic_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int somagic_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct usb_somagic *somagic = snd_pcm_substream_chip(substream);
	switch(cmd) {
		case SNDRV_PCM_TRIGGER_START: {
			somagic->audio.streaming = 1;
			return 0;
		}
		case SNDRV_PCM_TRIGGER_STOP: {
			somagic->audio.streaming = 0;
			return 0;
		}
		default: {
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static snd_pcm_uframes_t somagic_pcm_pointer(
																					struct snd_pcm_substream *substream)
{
	struct usb_somagic *somagic = snd_pcm_substream_chip(substream);
	return somagic->audio.dma_write_ptr / 8; 
}

static struct snd_pcm_ops somagic_audio_ops = {
	.open = somagic_pcm_open,
	.close = somagic_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = somagic_pcm_hw_params,
	.hw_free = somagic_pcm_hw_free,
	.prepare = somagic_pcm_prepare,
	.trigger = somagic_pcm_trigger,
	.pointer = somagic_pcm_pointer,
};

int __devinit somagic_connect_audio(struct usb_somagic *somagic)
{
	int rc;
	struct snd_card *sound_card;
	struct snd_pcm *sound_pcm;

	rc = snd_card_create(SNDRV_DEFAULT_IDX1, "Somagic",
											THIS_MODULE, 0, &sound_card);
	if (rc != 0) {
		printk(KERN_ERR "somagic::%s: Could not do ALSA snd_card_create()\n",
					 __func__);
		return rc;
	}

	rc = snd_pcm_new(sound_card, "Somagic PCM", 0, 0, 1, &sound_pcm);
	snd_pcm_set_ops(sound_pcm, SNDRV_PCM_STREAM_CAPTURE, &somagic_audio_ops);
	sound_pcm->info_flags = 0;
	sound_pcm->private_data = somagic;
	strcpy(sound_pcm->name, "Somagic PCM");

	snd_pcm_lib_preallocate_pages_for_all(sound_pcm, SNDRV_DMA_TYPE_CONTINUOUS,
																				snd_dma_continuous_data(GFP_KERNEL),
																				0, 64*1024);
	
	strcpy(sound_card->driver, "ALSA driver");
	strcpy(sound_card->shortname, "somagic audio");
	strcpy(sound_card->longname, "somagic ALSA audio");

	rc = snd_card_register(sound_card);
	if (rc != 0) {
		printk(KERN_ERR "somagic::%s: Could not do ALSA snd_card_register()\n",
					 __func__);
		
		snd_card_free(sound_card);
		return rc;
	}


	printk(KERN_INFO "somagic::%s: Successfully registered audio device!\n",
				 __func__);

	somagic->audio.card = sound_card;
	return 0;
}

void __devexit somagic_disconnect_audio(struct usb_somagic *somagic)
{
	if (somagic->audio.card != 0) {
		snd_card_free(somagic->audio.card);
	}
}
