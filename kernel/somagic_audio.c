#include "somagic.h"

static const struct snd_pcm_hardware audio_hardware = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
					SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 32768,
	.period_bytes_min = 4096,
	.period_bytes_max = 32768,
	.periods_min = 1,
	.periods_max = 1024
};

static int somagic_pcm_open(struct snd_pcm_substream *substream)
{
	return -ENODEV;
}

static int somagic_pcm_close(struct snd_pcm_substream *substream)
{
	return -ENODEV;
}

static int somagic_pcm_hw_params(struct snd_pcm_substream *substream,
																 struct snd_pcm_hw_params *hw_params)
{
	return -ENODEV;
}

static int somagic_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return -ENODEV;
}

static int somagic_pcm_prepare(struct snd_pcm_substream *substream)
{
	return -ENODEV;
}

static struct snd_pcm_ops somagic_audio_ops = {
	.open = somagic_pcm_open,
	.close = somagic_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = somagic_pcm_hw_params,
	.hw_free = somagic_pcm_hw_free,
	.prepare = somagic_pcm_prepare
/*
	.ack
	.pointer
	.page
*/
};

int __devinit somagic_connect_audio(struct usb_somagic *somagic)
{
	int rc;
	struct snd_card *sound_card;
	struct snd_pcm *sound_pcm;

	rc = snd_card_create(SNDRV_DEFAULT_IDX1, "somagic_alsa",
											THIS_MODULE, 0, &sound_card);
	if (rc != 0) {
		printk(KERN_ERR "somagic::%s: Could not do ALSA snd_card_create()\n",
					 __func__);
		return rc;
	}

	rc = snd_pcm_new(sound_card, "somagic audio", 0, 0, 1, &sound_pcm);
	snd_pcm_set_ops(sound_pcm, SNDRV_PCM_STREAM_CAPTURE, &somagic_audio_ops);
	sound_pcm->info_flags = 0;
	sound_pcm->private_data = somagic;
	strcpy(sound_pcm->name, "somagic audio capture");
	
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
