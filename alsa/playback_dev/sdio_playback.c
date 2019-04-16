/*
 * Basic SDIO FPGA soundcard
 *
 * Based on minivosc soundcard
 * original code:
 * Copyright (c) by Smilen Dimitrov <sd at imi.aau.dk>
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
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/version.h>

MODULE_AUTHOR("Giuliano Gambacorta");
MODULE_DESCRIPTION("SDIO sound card");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA, SDIO FPGA}}");


#define MAX_BUFFER (32 * 48) // depending on period_bytes* and periods_max
#define SND_SDIO_DRIVER	"snd_sdio"

static const struct sdio_device_id sdio_ids[] = {
        { SDIO_DEVICE(0x0213, 0x1002) },
        { },
};

// =========================== REQUIRED ALSA STRUCTS ==================================

// TODO reach 762000 as max rate, possibly more
static struct snd_pcm_hardware sdio_pcm_playback_hw =
{
    .info = (SNDRV_PCM_INFO_MMAP |
             SNDRV_PCM_INFO_INTERLEAVED |
             SNDRV_PCM_INFO_BLOCK_TRANSFER |
             SNDRV_PCM_INFO_MMAP_VALID),
    .formats          = SNDRV_PCM_FMTBIT_S32_LE,
    .rates            = SNDRV_PCM_RATE_8000_192000,
    .rate_min         = 8000,
    .rate_max         = 192000,
    .channels_min     = 2,
    .channels_max     = 2,
    .buffer_bytes_max = MAX_BUFFER, //(32 * 48) = 1536,
    .period_bytes_min = 48,
    .period_bytes_max = 48,
    .periods_min      = 1,
    .periods_max      = 1024,
};

static struct snd_pcm_ops sdio_pcm_ops =
{
    .open      = sdio_pcm_open,
    .close     = sdio_pcm_close,
#ifndef CONFIG_COMPAT	// this for 64bit kernel 32bit rootfs
    .unlocked_ioctl = sdio_snd_ioctl
#else
    .compat_ioctl = sdio__snd_ioctl
#endif
    .hw_params = sdio_hw_params,
    .hw_free   = sdio_hw_free,
    .prepare   = sdio_pcm_prepare,
    .trigger   = sdio_pcm_trigger,
    .pointer   = sdio_pcm_pointer,
};

static struct sdio_driver sdio_driver =
{
    .name = MODULE_NAME,
    .probe	= sdio_probe,
    .id_table = sdio_ids,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
    .remove	= __devexit_p(sdio_remove),
#else
    .remove	= sdio_remove,
#endif
//~ #ifdef CONFIG_PM
                //~ .suspend	= sdio_suspend,
                //~ .resume	= sdio_resume,
//~ #endif
    .driver	= {
        .name	= SND_SDIO_DRIVER,
        .owner = THIS_MODULE
    },
};

// ====================================================================================

// TODO remove elements from timer or sample sound array
struct sdio_card {
    struct sdio_func	*func;

    struct list_head 	list;
    unsigned int 		major;

    unsigned long 		ioport;
    struct cdev 		cdev;
    dev_t 				devid;

    struct snd_card *card;
    struct snd_pcm *pcm;
    const struct sdio_pcm_ops *timer_ops;
    /*
    * we have only one substream, so all data in this struct
    */
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
    // unsigned long last_jiffies; // not needed 'cause real hw
    struct timer_list timer;
    /* copied from struct loopback_pcm: */
    struct snd_pcm_substream *substream;
    unsigned int pcm_buffer_size;
    unsigned int buf_pos;	/* position in buffer */
    unsigned int silent_size;
    /* added for waveform: */
    unsigned int wvf_pos;	/* position in waveform array */
    unsigned int wvf_lift;	/* lift of waveform array */

};

// TODO add alsa stuff
static int __init sdio_alsa_init_module(void)
{
    int ret = 0;

    printk(KERN_NOTICE "SDIO init module ...\n");
    tsk = NULL;
    kbuf = NULL;
    ret = sdio_register_driver(&sdio_driver);
    return ret;
}

static void __exit sdio_alsa_exit_module(void)
{
    printk(KERN_NOTICE "SDIO exit module ...\n");
    if (tsk && !IS_ERR(tsk))
    {
        pr_err("SDIO : Stop kthread ...\n");
        kthread_stop(tsk);
        tsk = NULL;
    }
    sdio_unregister_driver(&sdio_driver);
}

module_init(sdio_alsa_init_module);
module_exit(sdio_alsa_exit_module);
