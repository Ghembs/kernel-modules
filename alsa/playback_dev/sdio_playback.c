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
#include <sound/initval.h>

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
#define MAX_PCM_SUBSTREAMS	2
#define byte_pos(x)	((x) / HZ)
#define frac_pos(x)	((x) * HZ)

// ============================ SDIO func declaration =================================
static int sdio_open(struct inode *inode, struct file *file);
static ssize_t sdio_write(struct file *file, const char __user *buf, size_t count,
        loff_t *pos);
static ssize_t sdio_read(struct file *file, char __user *buf, size_t count,
        loff_t *pos);
static long sdio_snd_ioctl(	struct file *file,
                           unsigned int cmd,  	// cmd is command
                           unsigned long arg);

static int __probe(struct sdio_func *func);
static int sdio_probe(struct sdio_func *func, const struct sdio_device_id *id);
static void __remove(struct sdio_func *func);
static void sdio_remove(struct sdio_func *func);

static const struct sdio_device_id sdio_ids[] = {
        { SDIO_DEVICE(0x0213, 0x1002) },
        { },
};

struct sdio_card {
    struct sdio_func	*func;

    struct list_head 	list;
    unsigned int 		major;

    unsigned long 		ioport;
    struct cdev 		cdev;
    dev_t 				devid;
};
// =========================== ALSA func declaration ==================================
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */

static int sdio_hw_params(struct snd_pcm_substream *ss,
                          struct snd_pcm_hw_params *hw_params);
static int sdio_hw_free(struct snd_pcm_substream *ss);
static int sdio_pcm_open(struct snd_pcm_substream *ss);
static int sdio_pcm_close(struct snd_pcm_substream *ss);
static int sdio_pcm_prepare(struct snd_pcm_substream *ss);
static int sdio_pcm_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t sdio_pcm_pointer(struct snd_pcm_substream *ss);
static int sdio_pcm_dev_free(struct snd_device *device);

static int 	major = SDIO_MAJOR;
static int  BlockAddress, isExit, isResume;
u8			*kbuf;//[MAX_SDIO_BYTES];		// kmalloc works, u8 not working
static int SampleRate, SampleBits;
static struct task_struct *tsk;
static LIST_HEAD(sdio_card_list);
// =========================== REQUIRED ALSA STRUCTS ==================================
// TODO reach 762000 as max rate, possibly more
static struct snd_pcm_hardware sdio_pcm_hw =
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
    .ioctl     = snd_pcm_lib_ioctl,
    .hw_params = sdio_hw_params,
    .hw_free   = sdio_hw_free,
    .prepare   = sdio_pcm_prepare,
    .trigger   = sdio_pcm_trigger,
    .pointer   = sdio_pcm_pointer,
};

// ================================ SDIO structs ======================================
static const struct file_operations fops_test = {
        .open			= sdio_open,
        .read			= sdio_read,
        .write			= sdio_write,
#ifndef CONFIG_COMPAT	// this for 64bit kernel 32bit rootfs
        .unlocked_ioctl = sdio_snd_ioctl
#else
        .compat_ioctl	= sdio_snd_ioctl
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
};
// ================================ ALSA structs ======================================
struct sdio_pcm;

struct sdio_cable
{
    struct sdio_pcm *stream;

    struct snd_pcm_hardware hw;
    unsigned int pcm_period_size;
    unsigned int pcm_bps;

    // flags
    unsigned int valid;
    unsigned int period_update_pending :1;
    unsigned int running;

    //timer
    unsigned int irq_pos;
    unsigned int period_size_frac;
    unsigned long last_jiffies;
    struct timer_list timer;
};

struct sdio_device
{
    struct sdio_cable *cable[MAX_PCM_SUBSTREAMS];
    struct mutex cable_lock;

    // alsa structs
    struct snd_card *card;
    struct snd_pcm *pcm;
};

struct sdio_pcm
{
    struct sdio_device *fifo;
    struct sdio_cable *cable;

    struct snd_pcm_substream *substream;
    unsigned int pcm_buffer_size;
    unsigned int buf_pos;
    unsigned int silent_size;
};

static struct snd_device_ops dev_ops =
{
    .dev_free = sdio_pcm_dev_free,
};
// =========================== SDIO function definition ===============================

// ============================== ALSA func definition ================================
static void copy_play_buf(struct sdio_pcm *play,
                          struct sdio_pcm *capt,
                          unsigned int bytes)
{
    char *src = play->substream->runtime->dma_area;
    char *dst = capt->substream->runtime->dma_area;
    unsigned int src_off = play->buf_pos;
    unsigned int dst_off = capt->buf_pos;
    for (;;) {
        unsigned int size = bytes;
        if (src_off + size > play->pcm_buffer_size)
            size = play->pcm_buffer_size - src_off;
        if (dst_off + size > capt->pcm_buffer_size)
            size = capt->pcm_buffer_size - dst_off;
        memcpy(dst + dst_off, src + src_off, size);
        if (size < capt->silent_size)
            capt->silent_size -= size;
        else
            capt->silent_size = 0;
        bytes -= size;
        if (!bytes)
            break;
        src_off = (src_off + size) % play->pcm_buffer_size;
        dst_off = (dst_off + size) % capt->pcm_buffer_size;
    }
}

static void sdio_xfer_buf(struct sdio_cable *dev, unsigned int count)
{
    int i;

    copy_play_buf(dev->stream,
                  dev->stream,
                  count);
    for (i = 0; i < 2; i++) {
        if (dev->running & (1 << i)) {
            struct sdio_pcm *pcm = dev->stream;
            pcm->buf_pos += count;
            pcm->buf_pos %= pcm->pcm_buffer_size;
        }
    }
}

static void sdio_pos_update(struct sdio_cable *cable)
{
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
    sdio_xfer_buf(cable, count);
    if (cable->irq_pos >= cable->period_size_frac) {
        cable->irq_pos %= cable->period_size_frac;
        cable->period_update_pending = 1;
    }
}

static void sdio_timer_start(struct sdio_cable *cable)
{
    unsigned long tick;
    tick = cable->period_size_frac - cable->irq_pos;
    tick = (tick + cable->pcm_bps - 1) / cable->pcm_bps;
    cable->timer.expires = jiffies + tick;
    add_timer(&cable->timer);
}

static void sdio_timer_stop(struct sdio_cable *dev)
{
    del_timer(&dev->timer);
}

static int sdio_pcm_free(struct sdio_device *chip)
{
    return 0;
}

static int sdio_pcm_dev_free(struct snd_device *device)
{
    return sdio_pcm_free(device->device_data);
}

static int sdio_hw_params(struct snd_pcm_substream *ss,
                          struct snd_pcm_hw_params *hw_params)
{
    return snd_pcm_lib_malloc_pages(ss,
                                    params_buffer_bytes(hw_params));
}

static int sdio_hw_free(struct snd_pcm_substream *ss)
{
    return snd_pcm_lib_free_pages(ss);
}

static int sdio_pcm_open(struct snd_pcm_substream *ss)
{
    struct sdio_pcm *dpcm;
    struct sdio_device *mydev = ss->private_data;
    printk(KERN_WARNING "opening this beautiful useless device");

    mutex_lock(&mydev->cable_lock);

    ss->runtime->hw = sdio_pcm_hw;

    dpcm->substream = ss;

    ss->runtime->private_data = mydev;

    mutex_unlock(&mydev->cable_lock);

    return 0;
}

static int sdio_pcm_close(struct snd_pcm_substream *ss)
{
    struct sdio_device *mydev = ss->private_data;
    printk(KERN_WARNING "closing this beautiful useless device");

    mutex_lock(&mydev->cable_lock);

    ss->private_data = NULL;

    mutex_unlock(&mydev->cable_lock);

    return 0;
}

static int sdio_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    //struct loopback_pcm *dpcm = runtime->private_data;
    struct sdio_cable *dev = runtime->private_data; // dpcm->cable;
    switch (cmd)
    {
        case SNDRV_PCM_TRIGGER_START:
            if (!dev->running)
            {
                dev->last_jiffies = jiffies;
                sdio_timer_start(dev);
            }
            dev->running |= (1 << substream->stream);
            break;
        case SNDRV_PCM_TRIGGER_STOP:
            dev->running &= ~(1 << substream->stream);
            if (!dev->running)
                sdio_timer_stop(dev);
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

static int sdio_pcm_prepare(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    //struct loopback_pcm *dpcm = runtime->private_data;
    struct sdio_pcm *dev = runtime->private_data; // dpcm->cable;
    struct sdio_cable *cable = dev->cable;
    unsigned int bps;
    bps = runtime->rate * runtime->channels;
    bps *= snd_pcm_format_width(runtime->format);
    bps /= 8;
    if (bps <= 0)
        return -EINVAL;
    dev->buf_pos = 0;
    dev->pcm_buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
    if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
        /* clear capture buffer */
        dev->silent_size = dev->pcm_buffer_size;
        memset(runtime->dma_area, 0, dev->pcm_buffer_size);
    }
    if (!cable->running) {
        cable->irq_pos = 0;
        cable->period_update_pending = 0;
    }
    mutex_lock(&dev->fifo->cable_lock);
    if (!(cable->valid & ~(1 << substream->stream))) {
        cable->pcm_bps = bps;
        cable->pcm_period_size =
                frames_to_bytes(runtime, runtime->period_size);
        cable->period_size_frac = frac_pos(cable->pcm_period_size);
        cable->hw.formats = (1ULL << runtime->format);
        cable->hw.rate_min = runtime->rate;
        cable->hw.rate_max = runtime->rate;
        cable->hw.channels_min = runtime->channels;
        cable->hw.channels_max = runtime->channels;
        cable->hw.period_bytes_min = cable->pcm_period_size;
        cable->hw.period_bytes_max = cable->pcm_period_size;
    }
    cable->valid |= 1 << substream->stream;
    mutex_unlock(&dev->fifo->cable_lock);
    return 0;
}

static snd_pcm_uframes_t sdio_pcm_pointer(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct sdio_pcm *dpcm = runtime->private_data;
    sdio_pos_update(dpcm->cable);
    return bytes_to_frames(runtime, dpcm->buf_pos);
}
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
