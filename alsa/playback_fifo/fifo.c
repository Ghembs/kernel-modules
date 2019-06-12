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
#include <linux/fs.h> 	     /* file stuff */
#include <linux/kernel.h>    /* printk() */
#include <linux/errno.h>     /* error codes */
#include <linux/cdev.h>      /* char device stuff */
#include <linux/uaccess.h>  /* copy_to_user() */
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

MODULE_AUTHOR("Giuliano Gambacorta");
MODULE_DESCRIPTION("FIFO sound card");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA, FIFO PLAYBACK}}");

#define SND_FIFO_DRIVER	"snd_fifo"

#define byte_pos(x)	((x) / HZ)
#define frac_pos(x)	((x) * HZ)
#define MAX_PCM_SUBSTREAMS	8

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};

static struct platform_device *device;

struct fifo_snd_device
{
    spinlock_t lock;
    unsigned int pcm_rate_shift;	/* rate shift value */
    struct snd_card *card;
    struct snd_pcm *pcm;
    /* copied from struct loopback: */
    struct mutex cable_lock;
    /* copied from struct loopback_cable: */
    /* PCM parameters */
    unsigned int pcm_period_size;
    unsigned int pcm_bps;		/* bytes per second */
    unsigned int pcm_salign;	/* bytes per sample * channels */
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
    /* added for waveform: */
};

static int device_file_major_number = 0;
static const char   g_s_Hello_World_string[] = "Hello, world, from kernel mode!\n\0";
static char *memory_buffer;
static const ssize_t g_s_Hello_World_size = sizeof(g_s_Hello_World_string);


//====================================== CHAR DEVICE ======================================
//TODO check if possible to let dimension correspond to alsa buffer size
static ssize_t device_file_read (struct file *file_ptr,
                                 char __user *user_buffer,
                                size_t count,
                                loff_t *position)
{
    int dimension = sizeof(memory_buffer);

    printk(KERN_NOTICE "fifo_sound: device file is read at offset = %i, read bytes count = %u",
    (int)*position,
    (unsigned int)count);

    if (copy_to_user(user_buffer, memory_buffer, dimension) != 0)
        return -EFAULT;

    memset(memory_buffer, 0, dimension);
    return count;
}

static struct file_operations simple_driver_fops = {
        .owner = THIS_MODULE,
        .read = device_file_read,
};

// ============================ FUNCTION DECLARATIONS =================================
// alsa functions
static int fifo_pcm_open(struct snd_pcm_substream *ss);
static int fifo_pcm_close(struct snd_pcm_substream *ss);
static int fifo_pcm_prepare(struct snd_pcm_substream *ss);
static int fifo_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t fifo_pointer(struct snd_pcm_substream *substream);
static void copy_play_buf(struct fifo_snd_device *play, unsigned int bytes);
static void fifo_xfer_buf(struct fifo_snd_device *dev, unsigned int count);
static unsigned fifo_pos_update(struct fifo_snd_device *cable);

static int fifo_hw_params(struct snd_pcm_substream *ss,
                          struct snd_pcm_hw_params *hw_params);
static int fifo_hw_free(struct snd_pcm_substream *ss);
static int fifo_pcm_dev_free(struct snd_device *device);
static int fifo_pcm_free(struct fifo_snd_device *chip);

// * functions for driver/kernel module initialization
static void fifo_unregister_all(void);
static int __init alsa_card_fifo_init(void);
static void __exit alsa_card_fifo_exit(void);

// * declare functions for this struct describing the driver (to be defined later):
static int fifo_probe(struct platform_device *devptr);
static int fifo_remove(struct platform_device *devptr);

static void fifo_timer_function(struct timer_list *t);
static void fifo_timer_start(struct fifo_snd_device *dpcm);
static inline void fifo_timer_stop(struct fifo_snd_device *dpcm);
static inline void fifo_timer_stop_sync(struct fifo_snd_device *dpcm);

// ============================== ALSA STRUCTURES =====================================
static struct snd_pcm_hardware fifo_pcm_hw =
{
	.info = (SNDRV_PCM_INFO_MMAP |
			 SNDRV_PCM_INFO_INTERLEAVED |
			 SNDRV_PCM_INFO_BLOCK_TRANSFER |
			 SNDRV_PCM_INFO_MMAP_VALID),
	.formats          = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
	                     SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |
                         SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |
						 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |
						 SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE),
	.rates            = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_192000,
	.rate_min         = 8000,
	.rate_max         = 192000,
	.channels_min     = 1,
	.channels_max     = 2,
	.buffer_bytes_max =	2 * 1024 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	1024 * 1024,
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
static void fifo_timer_function(struct timer_list *t)
{
    struct fifo_snd_device *dpcm = from_timer(dpcm, t, timer);
    unsigned long flags;

    spin_lock_irqsave(&dpcm->lock, flags);
    if (fifo_pos_update(dpcm) & (1 << dpcm->substream->stream)) {
        fifo_timer_start(dpcm);
        if (dpcm->period_update_pending) {
            dpcm->period_update_pending = 0;
            spin_unlock_irqrestore(&dpcm->lock, flags);
            /* need to unlock before calling below */
            snd_pcm_period_elapsed(dpcm->substream);
            return;
        }
    }
    spin_unlock_irqrestore(&dpcm->lock, flags);
}

static void fifo_timer_start(struct fifo_snd_device *dpcm)
{
    unsigned long tick;
    unsigned int rate_shift = dpcm->pcm_rate_shift;//get_rate_shift(dpcm);

    if (rate_shift != dpcm->pcm_rate_shift) {
        dpcm->pcm_rate_shift = rate_shift;
        dpcm->period_size_frac = frac_pos(dpcm->pcm_period_size);
    }
    if (dpcm->period_size_frac <= dpcm->irq_pos) {
        dpcm->irq_pos %= dpcm->period_size_frac;
        dpcm->period_update_pending = 1;
    }
    tick = dpcm->period_size_frac - dpcm->irq_pos;
    tick = (tick + dpcm->pcm_bps - 1) / dpcm->pcm_bps;
    mod_timer(&dpcm->timer, jiffies + tick);
}

static inline void fifo_timer_stop(struct fifo_snd_device *dpcm)
{
    del_timer(&dpcm->timer);
    dpcm->timer.expires = 0;
}

static inline void fifo_timer_stop_sync(struct fifo_snd_device *dpcm)
{
    del_timer_sync(&dpcm->timer);
}

static int fifo_trigger(struct snd_pcm_substream *substream, int cmd)
{
    struct fifo_snd_device *dev = substream->private_data;
    switch (cmd)
    {
        case SNDRV_PCM_TRIGGER_START:
            if (!dev->running)
            {
                dev->last_jiffies = jiffies;
                fifo_timer_start(dev);
            }
            dev->running |= (1 << substream->stream);
            break;
        case SNDRV_PCM_TRIGGER_STOP:
            dev->running &= ~(1 << substream->stream);
            if (!dev->running)
                fifo_timer_stop(dev);
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

/*
 * TODO set proper copy algorithm checking update in buffer on aloop and minivosc
 */
static void copy_play_buf(struct fifo_snd_device *play,
                          unsigned int bytes)
{
    struct snd_pcm_runtime *runtime = play->substream->runtime;
    char *src = runtime->dma_area;
    unsigned int src_off = play->buf_pos;

    memory_buffer = kmalloc(play->pcm_buffer_size, GFP_KERNEL);

    if (runtime->status->state == SNDRV_PCM_STATE_DRAINING &&
        snd_pcm_playback_hw_avail(runtime) < runtime->buffer_size) {
        snd_pcm_uframes_t appl_ptr, appl_ptr1, diff;
        appl_ptr = appl_ptr1 = runtime->control->appl_ptr;
        appl_ptr1 -= appl_ptr1 % runtime->buffer_size;
        appl_ptr1 += play->buf_pos / play->pcm_salign;
        if (appl_ptr < appl_ptr1)
            appl_ptr1 -= runtime->buffer_size;
        diff = (appl_ptr - appl_ptr1) * play->pcm_salign;
        if (diff < bytes) {
            bytes = diff;
        }
    }

    printk(KERN_WARNING "copy_play_buf");
    for (;;) {
        unsigned int size = bytes;
        if (src_off + size > play->pcm_buffer_size)
        {
            size = play->pcm_buffer_size - src_off;
        }

        memcpy(memory_buffer, src + src_off, size);
        printk(KERN_WARNING "copy done");
        bytes -= size;
        if (!bytes)
            break;

        src_off = (src_off + size) % play->pcm_buffer_size;
    }
}

#define CABLE_PLAYBACK	(1 << SNDRV_PCM_STREAM_PLAYBACK)

static void fifo_xfer_buf(struct fifo_snd_device *dev, unsigned int count)
{
    printk(KERN_WARNING "fifo_xfer_buf");

    switch (dev->running){
        case CABLE_PLAYBACK:
            copy_play_buf(dev, count);
            break;
    }
    if (dev->running) {
        dev->buf_pos += count;
        dev->buf_pos %= dev->pcm_buffer_size;
    }
}

static unsigned fifo_pos_update(struct fifo_snd_device *cable)
{
    unsigned int last_pos, count;
    unsigned long delta;

    printk(KERN_WARNING "fifo_pos_update");
    if (!cable->running)
        return 0;

    delta = jiffies - cable->last_jiffies;
    if (!delta)
        goto unlock;

    cable->last_jiffies += delta;
    last_pos = byte_pos(cable->irq_pos);
    cable->irq_pos += delta * cable->pcm_bps;
    count = byte_pos(cable->irq_pos) - last_pos;
    if (!count)
        goto unlock;

    fifo_xfer_buf(cable, count);
    if (cable->irq_pos >= cable->period_size_frac) {
        cable->irq_pos %= cable->period_size_frac;
        cable->period_update_pending = 1;
    }

    unlock:
    return cable->running;
}

static snd_pcm_uframes_t fifo_pointer(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct fifo_snd_device *dpcm = runtime->private_data;

    printk(KERN_WARNING "fifo_pointer");
    fifo_pos_update(dpcm);
    return bytes_to_frames(runtime, dpcm->buf_pos);
}

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

    timer_setup(&mydev->timer, fifo_timer_function, 0);

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

// TODO check explicit setting for period size and buffer size to avoid underrun
static int fifo_pcm_prepare(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct fifo_snd_device *mydev = runtime->private_data;
	unsigned int bps;

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
        mydev->pcm_period_size = frames_to_bytes(runtime, runtime->period_size);
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
    int result = 0;

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

    printk(KERN_NOTICE "fifo-soundcard: register_device() is called.");

    result = register_chrdev(0, "fifo-soundcard", &simple_driver_fops);

    device_file_major_number = result;

    memory_buffer = kmalloc(1, GFP_KERNEL);
    memset(memory_buffer, 0, 1);

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
    unregister_chrdev(device_file_major_number, "fifo-soundcard");
	platform_set_drvdata(devptr, NULL);
	return 0;
}

// INIT FUNCTIONS
static void fifo_unregister_all(void)
{
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
		printk(KERN_ERR "fifo-alsa: Not enabled, not found or device busy\n");
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