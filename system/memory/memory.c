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

MODULE_LICENSE("Dual BSD/GPL");

// declaration of memory functions
static int memory_open(struct inode *inode, struct file *filp);
static int memory_release(struct inode *inode, struct file *filp);
static ssize_t memory_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t memory_write(struct file *filp, char *buf, size_t count, loff_t *f_pos);

void memory_exit(void);
int memory_init(void);

// structure that declares the usual file

static struct file_operations memory_fops = {
    read: memory_read,
    write: memory_write,
    open: memory_open,
    release: memory_release
};

// declaration of module init and exit

module_init(memory_init);
module_exit(memory_exit);

// Global variables
static int memory_major = 60;

static char *memory_buffer;

int memory_init(void)
{
    int result;

    result = register_chrdev(memory_major, "memory", &memory_fops);
    if (result < 0)
    {
        printk(KERN_WARNING "memory: cannot register character device, error code %i", result);
        return result;
    }

    memory_major = result;
    printk(KERN_NOTICE "memory: device registered with major number %i and minor number 0...255", memory_major);

    memory_buffer = kmalloc(1, GFP_KERNEL);
    if(!memory_buffer)
    {
        result = -ENOMEM;
        memory_exit();
        return result;
    }

    memset(memory_buffer, 0, 1);

    printk(KERN_NOTICE "Inserting memory module\n");
    return 0;
}

void memory_exit(void)
{
    //freeing major number
    unregister_chrdev(memory_major, "memory");

    //freeing buffer memory
    if(memory_buffer)
    {
        kfree(memory_buffer);
    }

    printk(KERN_NOTICE "Removing memory module\n");
}

static int memory_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int memory_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t memory_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    copy_to_user(buf, memory_buffer, 1);

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

static ssize_t memory_write(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    char *tmp;

    tmp = buf+count-1;
    copy_from_user(memory_buffer, tmp, 1);
    return 1;
}