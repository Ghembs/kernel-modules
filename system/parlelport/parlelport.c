#include <linux/init.h>
//#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
//#include <linux/system.h>
#include <linux/uaccess.h>
#include <asm-generic/io.h>
#include <linux/ioport.h>

MODULE_LICENSE("Dual BSD/GPL");

// declaration of parlelport functions
static int parlelport_open(struct inode *inode, struct file *filp);
static int parlelport_release(struct inode *inode, struct file *filp);
static ssize_t parlelport_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t parlelport_write(struct file *filp, char *buf, size_t count, loff_t *f_pos);

void parlelport_exit(void);
int parlelport_init(void);

// structure that declares the usual file

static struct file_operations parlelport_fops = {
    read: parlelport_read,
    write: parlelport_write,
    open: parlelport_open,
    release: parlelport_release
};

// declaration of module init and exit

module_init(parlelport_init);
module_exit(parlelport_exit);

// Global variables
static int parlelport_major = 61;

static int port;

int parlelport_init(void)
{
    int result;

    result = register_chrdev(parlelport_major, "parlelport", &parlelport_fops);
    if (result < 0)
    {
        printk(KERN_WARNING "parlelport: cannot register character device, error code %i", result);
        return result;
    }

    parlelport_major = result;
    printk(KERN_NOTICE "parlelport: device registered with major number %i and minor number 0...255", parlelport_major);

    /*port = check_region(0x378, 1);
    if(port)
    {
        printk(KERN_WARNING "parlelport: cannot reserve 0x378\n");
        result = port;
        parlelport_exit();
        return result;
    }*/
    request_region(0x378, 1, "parlelport");

    printk(KERN_NOTICE "Inserting parlelport module\n");
    return 0;
}

void parlelport_exit(void)
{
    //freeing major number
    unregister_chrdev(parlelport_major, "parlelport");

    if (!port)
    {
        release_region(0x378, 1);
    }

    printk(KERN_NOTICE "Removing parlelport module\n");
}

static int parlelport_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int parlelport_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t parlelport_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    char parlelport_buffer;

    parlelport_buffer = inb(0x378);

    copy_to_user(buf, &parlelport_buffer, 1);

    if (*f_pos == 0)
    {
        *f_pos += 1;
        return 1;
    }
    else
    {
        return 0;
    }
}

static ssize_t parlelport_write(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    char *tmp;

    char parlelport_buffer;

    tmp = buf+count-1;
    copy_from_user(parlelport_buffer, tmp, 1);

    outb(parlelport_buffer, 0x378);

    return 1;
}