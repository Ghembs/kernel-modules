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
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/ssb/ssb.h>

#include "sdio_playback.h"

MODULE_AUTHOR("Giuliano Gambacorta");
MODULE_DESCRIPTION("SDIO sound card");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA, SDIO FPGA}}");


#define MAX_BUFFER (32 * 48) // depending on period_bytes* and periods_max
#define SND_SDIO_DRIVER	"snd_sdio"

// ============================ SDIO func definition =================================
static int sdio_open(struct inode *inode, struct file *file);
static ssize_t sdio_write(struct file *file, const char __user *buf, size_t count,
        loff_t *pos);
static ssize_t sdio_read(struct file *file, char __user *buf, size_t count,
        loff_t *pos);
static long sdio_ioctl(	struct file *file,
                           unsigned int cmd,  	// cmd is command
                           unsigned long arg);

static int __probe(struct sdio_func *func);
static int sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
static void __remove(struct sdio_func *func);

// =========================== ALSA func definition ==================================
static int sdio_hw_params(struct snd_pcm_substream *ss,
                          struct snd_pcm_hw_params *hw_params);
static int sdio_hw_free(struct snd_pcm_substream *ss);
static int sdio_pcm_open(struct snd_pcm_substream *ss);
static int sdio_pcm_close(struct snd_pcm_substream *ss);
static int sdio_pcm_prepare(struct snd_pcm_substream *ss);
static int sdio_pcm_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t sdio_pcm_pointer(struct snd_pcm_substream *ss);


static const struct sdio_device_id sdio_ids[] = {
        { SDIO_DEVICE(0x0213, 0x1002) },
        { },
};

static int 	major = SDIO_MAJOR;
static int  BlockAddress, isExit, isResume;
u8			*kbuf;//[MAX_SDIO_BYTES];		// kmalloc works, u8 not working
static int SampleRate, SampleBits;
static struct task_struct *tsk;
static LIST_HEAD(sdio_card_list);

struct sdio_card {
    struct sdio_func	*func;

    struct list_head 	list;
    unsigned int 		major;

    unsigned long 		ioport;
    struct cdev 		cdev;
    dev_t 				devid;
};

// =========================== REQUIRED ALSA STRUCTS ==================================

// TODO reach 762000 as max rate, possibly more
static struct snd_pcm_hardware sdio_pcm_playback_hw =
{
    .info = (SNDRV_PCM_INFO_MMAP |
             SNDRV_PCM_INFO_INTERLEAVED |
             SNDRV_PCM_INFO_BLOCK_TRANSFER |
             SNDRV_PCM_INFO_MMAP_VALID),
    .formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
                SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |
                SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE),
    .rates            = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_192000,
    .rate_min         = 8000,
    .rate_max         = 192000,
    .channels_min     = 1,
    .channels_max     = 2,
    .buffer_bytes_max = 2 * 1024 * 1024, //(32 * 48) = 1536,
    .period_bytes_min = 64,
    .period_bytes_max = 2 * 1024 * 1024,
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

static const struct file_operations fops_test = {
        .open			= sdio_open,
        .read			= sdio_read,
        .write			= sdio_write,
#ifndef CONFIG_COMPAT	// this for 64bit kernel 32bit rootfs
        .unlocked_ioctl = sdio_ioctl
#else
        .compat_ioctl	= sdio_ioctl
#endif
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
