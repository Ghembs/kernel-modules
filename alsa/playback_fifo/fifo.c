/*
 * Basic FIFO playback soundcard
 *
 * Copyright (c) by Giuliano Gambacorta <ggambacora88@gmail.com>
 *
 * Based on minivosc soundcard:
 * Copyright (c) by Smilen Dimitrov <sd@imi.aau.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include <linux/fs.h> 	     /* file stuff */
#include <linux/kernel.h>    /* printk() */
#include <linux/errno.h>     /* error codes */
#include <linux/cdev.h>      /* char device stuff */
#include <linux/uaccess.h>  /* copy_to_user() */

MODULE_AUTHOR("Giuliano Gambacorta");
MODULE_DESCRIPTION("FIFO sound card");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA, FIFO PLAYBACK}}");

#define SND_FIFO_DRIVER	"snd_fifo"

#define byte_pos(x)	((x) / HZ)
#define frac_pos(x)	((x) * HZ)
#define MAX_PCM_SUBSTREAMS	8

/**
 * TODO check aloop.c to create a stream from playback to a fifo, through the corresponding
 * (and still missing) kernel library, instead that a stream from playback to capture
 * probably the important method is copy_play_buf and the information in: snd_pcm_substream->runtime->dma_area
 */

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};

static struct platform_device *device;

struct fifo_snd_device
{
    struct snd_card *card;
    struct snd_pcm *pcm;
    const struct sdio_pcm_ops *timer_ops;
    /* copied from struct loopback: */
    struct mutex cable_lock;
    /* copied from struct loopback_cable: */
    /* PCM parameters */
    unsigned int pcm_period_size;
    unsigned int pcm_bps;		/* bytes per second */
    /* flags */
    unsigned int valid;
    unsigned int running;
    unsigned int period_update_pending :1;
    /* timer stuff */
    unsigned int irq_pos;		/* fractional IRQ position */
    unsigned int period_size_frac;
    unsigned long last_jiffies;
    struct timer_list timer;
    /* copied from struct loopback_pcm: */
    struct snd_pcm_substream *substream;
    unsigned int pcm_buffer_size;
    unsigned int buf_pos;	/* position in buffer */
    unsigned int silent_size;
    struct sdio_card *sdio;
    /* added for waveform: */
};

static const char   g_s_Hello_World_string[] = "Hello, world, from kernel mode!\n\0";
static const ssize_t g_s_Hello_World_size = sizeof(g_s_Hello_World_string);


//====================================== CHAR DEVICE ======================================
static ssize_t device_file_read (struct file *file_ptr,
                                 char __user *user_buffer,
                                size_t count,
                                loff_t *position)
{
        printk(KERN_NOTICE "fifo_sound: device file is read at offset = %i, read bytes count = %u",
        (int)*position,
        (unsigned int)count);
        if(*position >= g_s_Hello_World_size)
        return 0;
        if (*position + count > g_s_Hello_World_size)
        count = g_s_Hello_World_size - *position;
        if (copy_to_user(user_buffer, g_s_Hello_World_string + *position, count) != 0)
        return -EFAULT;
        *position += count;
        return count;
}

static struct file_operations simple_driver_fops = {
        .owner = THIS_MODULE,
        .read = device_file_read,
};

// ==================================== TEMPORARY =====================================
static int fifo_trigger(struct snd_pcm_substream *substream, int cmd)
{
    printk(KERN_WARNING "fifo_trigger");
	struct fifo_snd_device *dev = substream->private_data;
	switch (cmd)
	{
		case SNDRV_PCM_TRIGGER_START:
			if (!dev->running)
			{
				dev->last_jiffies = jiffies;
				//fifo_timer_start(dev);
			}
			dev->running |= (1 << substream->stream);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
			dev->running &= ~(1 << substream->stream);
			if (!dev->running)
				//fifo_timer_stop(dev);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

/*
 * TODO rewrite this to copy on a file instead that on another pcm device, follow calls
 * to required playback ops
 */
static void copy_play_buf(struct fifo_snd_device *play,
						  unsigned int bytes)
{
    printk(KERN_WARNING "copy_play_buf");
	char *src = play->substream->runtime->dma_area;

	unsigned int src_off = play->buf_pos;

	for (;;) {
		unsigned int size = bytes;
		if (src_off + size > play->pcm_buffer_size)
			size = play->pcm_buffer_size - src_off;

        memcpy(g_s_Hello_World_string, src + src_off, size);

        bytes -= size;
        if (!bytes)
            break;

        src_off = (src_off + size) % play->pcm_buffer_size;
	}
}

static void fifo_xfer_buf(struct fifo_snd_device *dev, unsigned int count)
{
	int i;
    printk(KERN_WARNING "fifo_xfer_buf");

	copy_play_buf(dev,
				  count);
	for (i = 0; i < 2; i++) {
		if (dev->running & (1 << i)) {
			dev->buf_pos += count;
			dev->buf_pos %= dev->pcm_buffer_size;
		}
	}
}

static void fifo_pos_update(struct fifo_snd_device *cable)
{
    printk(KERN_WARNING "fifo_pos_update");
	unsigned int last_pos, count;
	unsigned long delta;

	if (!cable->running)
		return;

	delta = jiffies - cable->last_jiffies;
	if (!delta)
		return;

	cable->last_jiffies += delta;
	last_pos = byte_pos(cable->irq_pos);
	cable->irq_pos += delta * cable->pcm_bps;
	count = byte_pos(cable->irq_pos) - last_pos;
	if (!count)
		return;

	fifo_xfer_buf(cable, count);
	if (cable->irq_pos >= cable->period_size_frac) {
		cable->irq_pos %= cable->period_size_frac;
		cable->period_update_pending = 1;
	}
}

static snd_pcm_uframes_t fifo_pointer(struct snd_pcm_substream *substream)
{
    printk(KERN_WARNING "fifo_pointer");
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct fifo_snd_device *dpcm = runtime->private_data;
	fifo_pos_update(dpcm);
	return bytes_to_frames(runtime, dpcm->buf_pos);
}
// ============================ FUNCTION DECLARATIONS =================================
// alsa functions
static int fifo_hw_params(struct snd_pcm_substream *ss,
						  struct snd_pcm_hw_params *hw_params);
static int fifo_hw_free(struct snd_pcm_substream *ss);
static int fifo_pcm_open(struct snd_pcm_substream *ss);
static int fifo_pcm_close(struct snd_pcm_substream *ss);
static int fifo_pcm_prepare(struct snd_pcm_substream *ss);

static int fifo_pcm_dev_free(struct snd_device *device);
static int fifo_pcm_free(struct fifo_snd_device *chip);

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
	.formats          = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
						 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |
						 SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE),
	.rates            = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_192000,
	.rate_min         = 8000,
	.rate_max         = 192000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max =	2 * 1024 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	2 * 1024 * 1024,
	.periods_min =		1,
	.periods_max =		1024,
	//.fifo_size =		0, // apparently useless
};

static struct snd_pcm_ops fifo_pcm_playback_ops =
{
	.open      = fifo_pcm_open,
	.close     = fifo_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = fifo_hw_params,
	.hw_free   = fifo_hw_free,
	.prepare   = fifo_pcm_prepare,
	.trigger   = fifo_trigger,
	.pointer   = fifo_pointer,
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
static int fifo_hw_params(struct snd_pcm_substream *ss,
                        struct snd_pcm_hw_params *hw_params)
{
    printk(KERN_WARNING "fifo_hw_params");
	return snd_pcm_lib_malloc_pages(ss,
	                                params_buffer_bytes(hw_params));
}

static int fifo_hw_free(struct snd_pcm_substream *ss)
{
    printk(KERN_WARNING "fifo_hw_free");
	return snd_pcm_lib_free_pages(ss);
}

static int fifo_pcm_open(struct snd_pcm_substream *ss)
{
	struct fifo_snd_device *mydev = ss->private_data;
    printk(KERN_WARNING "fifo_pcm_open");

    mutex_lock(&mydev->cable_lock);

	ss->runtime->hw = fifo_pcm_hw;

    mydev->substream = ss;

	ss->runtime->private_data = mydev;

    mutex_unlock(&mydev->cable_lock);

	return 0;
}

static int fifo_pcm_close(struct snd_pcm_substream *ss)
{
    struct fifo_snd_device *mydev = ss->private_data;
    printk(KERN_WARNING "fifo_pcm_close");

    mutex_lock(&mydev->cable_lock);

	ss->private_data = NULL;

    mutex_unlock(&mydev->cable_lock);

	return 0;
}


static int fifo_pcm_prepare(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct fifo_snd_device *mydev = runtime->private_data;
	unsigned int bps;

    printk(KERN_WARNING "fifo_pcm_prepare");

    mydev->buf_pos = 0;

	bps = runtime->rate * runtime->channels; // params requested by user app (arecord, audacity)
	bps *= snd_pcm_format_width(runtime->format);
	bps /= 8;
	if (bps <= 0)
		return -EINVAL;

	mydev->pcm_buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
    if (!mydev->running) {
        mydev->irq_pos = 0;
        mydev->period_update_pending = 0;
    }

    mutex_lock(&mydev->cable_lock);
    if (!(mydev->valid & ~(1 << ss->stream))) {
        mydev->pcm_bps = bps;
        mydev->pcm_period_size =
                frames_to_bytes(runtime, runtime->period_size);
        mydev->period_size_frac = frac_pos(mydev->pcm_period_size);
    }

    mydev->valid |= 1 << ss->stream;
    mutex_unlock(&mydev->cable_lock);

	return 0;
}

static int fifo_pcm_free(struct fifo_snd_device *chip)
{
    printk(KERN_WARNING "fifo_pcm_free");
	return 0;
}

static int fifo_pcm_dev_free(struct snd_device *device)
{
    printk(KERN_WARNING "fifo_pcm_dev_free");
	return fifo_pcm_free(device->device_data);
}
// ======================== DEVICE DRIVER OPERATIONS ==================================
static int fifo_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct fifo_snd_device *mydev;
	int dev = devptr->id;

	int ret;

	int nr_subdevs = 1; // how many playback substreams we want

    printk(KERN_WARNING "fifo_probe");

	// sound card creation
	ret = snd_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE,
					   sizeof(struct fifo_snd_device), &card);

    printk(KERN_WARNING "New soundcard");
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

    printk(KERN_WARNING "New device");

	pcm->info_flags = 0;

	strcpy(pcm->name, SND_FIFO_DRIVER);

	ret = snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
										  snd_dma_continuous_data(GFP_KERNEL),
										  32*48, 32*48);

	if (ret < 0)
		goto __nodev;

	ret = snd_card_register(card);

    printk(KERN_WARNING "REGISTERED CARD");


    int result = 0;
    printk(KERN_NOTICE "fifo-soundcard: register_device() il called.");

    result = register_chrdev(0, "fifo-soundcard", &simple_driver_fops);

    if (result < 0)
    {
        printk(KERN_WARNING "Fifo-soundcard: can\'t register character device with errorcode = %i", result);
        return result;
    }

	if (ret == 0)   // or... (!ret)
	{
		platform_set_drvdata(devptr, card);
		return 0; // success
	}

__nodev: // as in aloop/dummy...
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

    platform_device_unregister(device);

    platform_driver_unregister(&fifo_driver);
}

static int __init alsa_card_fifo_init(void)
{
	int i, err, cards;
    printk(KERN_WARNING "alsa_card_fifo_init");

	err = platform_driver_register(&fifo_driver);
	if (err < 0)
		return err;

	cards = 0;

	for (i = 0; i < SNDRV_CARDS; i++)
	{
		//struct platform_device *device;

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

		//device = device;
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