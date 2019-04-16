#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

MODULE_AUTHOR("Giuliano Gambacorta");
MODULE_DESCRIPTION("SDIO sound card");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA, FIFO PLAYBACK}}");

#define SND_FIFO_DRIVER	"snd_fifo"

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};

static struct platform_device *devices[SNDRV_CARDS];

struct fifo_device
{
	struct snd_card *card;
	struct snd_pcm *pcm;
	unsigned int pcm_buffer_size;
    struct mutex cable_lock;

    unsigned int buf_pos;
    unsigned int running;

    struct snd_pcm_substream *substream;
};

// ============================ FUNCTION DECLARATIONS =================================
// alsa functions
static int fifo_hw_params(struct snd_pcm_substream *ss,
						  struct snd_pcm_hw_params *hw_params);
static int fifo_hw_free(struct snd_pcm_substream *ss);
static int fifo_pcm_open(struct snd_pcm_substream *ss);
static int fifo_pcm_close(struct snd_pcm_substream *ss);
static int fifo_pcm_prepare(struct snd_pcm_substream *ss);
static int fifo_pcm_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t fifo_pcm_pointer(struct snd_pcm_substream *ss);

static int fifo_pcm_dev_free(struct snd_device *device);
static int fifo_pcm_free(struct fifo_device *chip);

//static void fifo_pos_update(struct fifo_device *mydev);

// * functions for driver/kernel module initialization
static void fifo_unregister_all(void);
static int __init alsa_card_fifo_init(void);
static void __exit alsa_card_fifo_exit(void);

// * declare functions for this struct describing the driver (to be defined later):
static int fifo_probe(struct platform_device *devptr);
static int fifo_remove(struct platform_device *devptr);

// ============================== ALSA STRUCTURES =====================================
static struct snd_pcm_hardware fifo_pcm_hw =
{
	.info = (SNDRV_PCM_INFO_MMAP |
			 SNDRV_PCM_INFO_INTERLEAVED |
			 SNDRV_PCM_INFO_BLOCK_TRANSFER |
			 SNDRV_PCM_INFO_MMAP_VALID),
	.formats          = SNDRV_PCM_FMTBIT_U8,
	.rates            = SNDRV_PCM_RATE_8000,
	.rate_min         = 8000,
	.rate_max         = 8000,
	.channels_min     = 1,
	.channels_max     = 1,
	.buffer_bytes_max = 32*48,
	.period_bytes_min = 48,
	.period_bytes_max = 48,
	.periods_min      = 1,
	.periods_max      = 32,
};

static struct snd_pcm_ops fifo_pcm_playback_ops =
{
	.open      = fifo_pcm_open,
	.close     = fifo_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = fifo_hw_params,
	.hw_free   = fifo_hw_free,
	.prepare   = fifo_pcm_prepare,
	.trigger   = fifo_pcm_trigger,
	.pointer   = fifo_pcm_pointer,
};

// specifies what func is called @ snd_card_free
// used in snd_device_new
static struct snd_device_ops dev_ops =
{
	.dev_free = fifo_pcm_dev_free,
};

// =============================== DEVICE STRUCTURES ==================================
// * we need a struct describing the driver:
static struct platform_driver fifo_driver =
{
	.probe		= fifo_probe,
	.remove		= fifo_remove,
	.driver		= {
		.name	= SND_FIFO_DRIVER,
		.owner  = THIS_MODULE
	},
};

// ======================= PCM PLAYBACK OPERATIONS ====================================
static int fifo_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
    return 0;
}

static snd_pcm_uframes_t fifo_pcm_pointer(struct snd_pcm_substream *ss)
{
    struct snd_pcm_runtime *runtime = ss->runtime;
    struct fifo_device *mydev = runtime->private_data;

    //fifo_pos_update(mydev);

    return bytes_to_frames(runtime, mydev->buf_pos);
}

static int fifo_hw_params(struct snd_pcm_substream *ss,
                        struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(ss,
	                                params_buffer_bytes(hw_params));
}

static int fifo_hw_free(struct snd_pcm_substream *ss)
{
	return snd_pcm_lib_free_pages(ss);
}

static int fifo_pcm_open(struct snd_pcm_substream *ss)
{
	struct fifo_device *mydev = ss->private_data;
    printk(KERN_WARNING "opening this beautiful useless device qualcosa");

    mutex_lock(&mydev->cable_lock);

	ss->runtime->hw = fifo_pcm_hw;

    mydev->substream = ss;

	ss->runtime->private_data = mydev;

    mutex_unlock(&mydev->cable_lock);

	return 0;
}

static int fifo_pcm_close(struct snd_pcm_substream *ss)
{
    struct fifo_device *mydev = ss->private_data;
    printk(KERN_WARNING "closing this beautiful useless device qualcosa");

    mutex_lock(&mydev->cable_lock);

	ss->private_data = NULL;

    mutex_unlock(&mydev->cable_lock);

	return 0;
}


static int fifo_pcm_prepare(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct fifo_device *mydev = runtime->private_data;
	unsigned int bps;

    printk(KERN_WARNING "DIS IS PREPARE");

    mydev->buf_pos = 0;

	bps = runtime->rate * runtime->channels; // params requested by user app (arecord, audacity)
	bps *= snd_pcm_format_width(runtime->format);
	bps /= 8;
	if (bps <= 0)
		return -EINVAL;

	mydev->pcm_buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
	if (ss->stream == SNDRV_PCM_STREAM_CAPTURE) {
		memset(runtime->dma_area, 45, mydev->pcm_buffer_size);
	}

	return 0;
}

static int fifo_pcm_free(struct fifo_device *chip)
{
	return 0;
}

static int fifo_pcm_dev_free(struct snd_device *device)
{
	return fifo_pcm_free(device->device_data);
}
// ======================== DEVICE DRIVER OPERATIONS ==================================
static int fifo_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct fifo_device *mydev;
	int dev = devptr->id;

	int ret;

	int nr_subdevs = 1; // how many playback substreams we want

    printk(KERN_WARNING "probing this beautiful useless device");

	// sound card creation
	ret = snd_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE,
					   sizeof(struct fifo_device), &card);

    printk(KERN_WARNING "NU SOUNDCARD");
	if (ret < 0)
		goto __nodev;

	mydev = card->private_data;
	mydev->card = card;

    mutex_init(&mydev->cable_lock);

	strcpy(card->driver, "virtual device");
	sprintf(card->longname, "MySoundCard Audio %s", SND_FIFO_DRIVER);
	sprintf(card->shortname, "%s", SND_FIFO_DRIVER);

	// sound device creation
	ret = snd_device_new(card, SNDRV_DEV_LOWLEVEL, mydev, &dev_ops);

	if (ret < 0)
		goto __nodev;

	// * we want 1 playback, and 0 capture substreams (4th and 5th arg) ..
	ret = snd_pcm_new(card, card->driver, 0, nr_subdevs, 0, &pcm);

	if (ret < 0)
		goto __nodev;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &fifo_pcm_playback_ops);
	pcm->private_data = mydev;

    printk(KERN_WARNING "NU DEVICE");

	pcm->info_flags = 0;

	strcpy(pcm->name, SND_FIFO_DRIVER);

	ret = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
										  snd_dma_continuous_data(GFP_KERNEL),
										  32*48, 32*48);

	if (ret < 0)
		goto __nodev;

	ret = snd_card_register(card);

    printk(KERN_WARNING "REGISTERED CARD");

	if (ret == 0)   // or... (!ret)
	{
		platform_set_drvdata(devptr, card);
		return 0; // success
	}

__nodev: // as in aloop/dummy...
    printk(KERN_WARNING "KILLING NAZIS ON THE MOON");
	snd_card_free(card); // this will autocall .dev_free (= fifo_pcm_dev_free)
	return ret;
}

static int fifo_remove(struct platform_device *devptr)
{
	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

// INIT FUNCTIONS
static void fifo_unregister_all(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(devices); ++i)
        platform_device_unregister(devices[i]);

    platform_driver_unregister(&fifo_driver);
}

static int __init alsa_card_fifo_init(void)
{
	int i, err, cards;
    printk(KERN_WARNING "init this beautiful useless device");

	err = platform_driver_register(&fifo_driver);
	if (err < 0)
		return err;

	cards = 0;

	for (i = 0; i < SNDRV_CARDS; i++)
	{
		struct platform_device *device;

		if (!enable[i])
			continue;

		device = platform_device_register_simple(SND_FIFO_DRIVER, i, NULL, 0);

		if (IS_ERR(device))
			continue;

		if (!platform_get_drvdata(device))
		{
			platform_device_unregister(device);
			continue;
		}

		devices[i] = device;
		cards++;
	}

	if (!cards)
	{
#ifdef MODULE
		printk(KERN_ERR "fifo-alsa: No enabled, not found or device busy\n");
#endif
		fifo_unregister_all();
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_fifo_exit(void)
{
	fifo_unregister_all();
}

// ====================================================================================
module_init(alsa_card_fifo_init)
module_exit(alsa_card_fifo_exit)