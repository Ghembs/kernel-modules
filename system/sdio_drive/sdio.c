#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>

static const struct sdio_device_id sdio_ids[] = {
        { SDIO_DEVICE(0x0213, 0x1002) },
        { },
};

static struct sdio_driver sdio_driver = {
        .name		= MODULE_NAME,
        .id_table	= sdio_ids,
        .probe		= sdio_probe,
        .remove		= sdio_remove,
};

static int __init sdio_init(void)
{
    ret = sdio_register_driver(&sdio_driver);
}

static int __probe(struct sdio_func *func)
{
    struct sdio_card *card;
    int res, ret;

    card = kzalloc(sizeof(struct sdio_card), GFP_KERNEL);
    if (card == NULL)
        return -ENOMEM;

    func->max_blksize = 2048;
    func->enable_timeout = 20000;
    card->func = func;

    sdio_claim_host(func);
    ret = sdio_enable_func(func);
    sdio_set_block_size(func, 512);
    if (ret) {
        printk(" couldn't register device number   %d\n",ret);
        goto release;
    }

    card->major = 	 major;
    res = register_chrdev(major, MODULE_NAME, &fops_test);
    major ++;
    list_add_tail(&card->list, &sdio_card_list);
    printk("card->major   %d  \n",card->major);

    sdio_release_host(func);
    sdio_set_drvdata(func, card);
    printk("SDIO data module probe:%d .\n", card->major);
    BlockAddress = 0;

    if (card->major==SDIO_MAJOR)
    {
        kbuf=kmalloc(MAX_SDIO_BYTES, GFP_KERNEL);
        if (!kbuf)	pr_err("SDIO kmalloc failed .");
        SampleRate=SampleBits=0;

        //ret = request_irq(/*irq_number*/, (irq_handler_t)sdio_callback, IRQ_SHARE, "mmc-sdio", func);
    }

    return ret;
    release:
    sdio_release_host(func);
    kfree(card);
    return ret;
}

static int sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
    pr_err("SDIO driver probe ...\n");
    isResume ++;
    return __probe(func);
}

static void __remove(struct sdio_func *func)
{
    struct sdio_card *card;

    if (major == SDIO_MAJOR) return;
    pr_err("SDIO major=%d func = %p ... \n", major, func);
    card = sdio_get_drvdata(func);
    pr_err("SDIO card = %p ... \n", card);

    pr_err("card->major   %d  \n",card->major);
    unregister_chrdev(card->major,"sdio_test");
    list_del(&card->list);
    major --;
    sdio_claim_host(func);
    sdio_disable_func(func);
    sdio_set_drvdata(func, NULL);
    sdio_release_host(func);
    pr_err("SDIO data module removed\n");
    if (kbuf)
    {
        kfree(kbuf);
        kbuf=NULL;

        //free_irq(/*irq_num*/, void *dev_id);
    }
}

static void sdio_remove(struct sdio_func *func)
{
    pr_err("SDIO driver exit ...\n");
    isExit ++;
    __remove(func);
}