/*
 * Basic Encoding conversion
 *
 * Copyright Giuliano Gambacorta, Arseniy Suchkov
 * <ggambacorta88@gmail.com>
 * <arseniy.suchkov@gmail.com>
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
#include <linux/fs.h> 	     /* file stuff */
#include <linux/kernel.h>    /* printk() */
#include <linux/errno.h>     /* error codes */
#include <linux/module.h>  /* THIS_MODULE */
#include <linux/cdev.h>      /* char device stuff */
#include <linux/uaccess.h>  /* copy_to_user() */

#include "kernel_encoding.h"

MODULE_AUTHOR("Giuliano Gambacorta");
MODULE_DESCRIPTION("encoding converter");
MODULE_LICENSE("GPL");

static int device_file_major_number = 0;
static const char device_name[] = "kernel encoding";
static char   message[256] = {0};           ///< Memory for the string that is passed from userspace
static short  size_of_message;              ///< Used to remember the size of the string stored

static const unsigned cp1251[] = {192, 193, 194, 195, 196, 197, 168, 198, 199,
                                  200, 201, 202, 203, 204, 205, 206, 207, 208,
                                  209, 210, 211, 212, 213, 214, 215, 216, 217,
                                  218, 219, 220, 221, 222, 223, 224, 225, 226,
                                  227, 228, 229, 184, 230, 231, 232, 233, 234,
                                  235, 236, 237, 238, 239, 240, 241, 242, 243,
                                  244, 245, 246, 247, 248, 249, 250, 251, 252,
                                  253, 254, 255};

static const unsigned koi8_r[] = {225, 226, 247, 231, 228, 229, 179, 246, 250,
                                  233, 234, 235, 236, 237, 238, 239, 240, 242,
                                  243, 244, 245, 230, 232, 227, 254, 251, 253,
                                  255, 249, 248, 252, 224, 241, 193, 194, 215,
                                  199, 196, 197, 163, 214, 218, 201, 202, 203,
                                  204, 205, 206, 207, 208, 210, 211, 212, 213,
                                  198, 200, 195, 222, 219, 221, 223, 217, 216,
                                  220, 192, 209};

static const unsigned cp866[]  = {128, 129, 130, 131, 132, 133, 240, 134, 135,
                                  136, 137, 138, 139, 140, 141, 142, 143, 144,
                                  145, 146, 147, 148, 149, 150, 151, 152, 153,
                                  154, 155, 156, 157, 158, 159, 160, 161, 162,
                                  163, 164, 165, 241, 166, 167, 168, 169, 170,
                                  171, 172, 173, 174, 175, 224, 225, 226, 227,
                                  228, 229, 230, 231, 232, 233, 234, 235, 236,
                                  237, 238, 239};

static int encode(char *buffer, const unsigned *_from, const unsigned *_to) {
    const unsigned long size = strlen(buffer);
    unsigned i, j;
    for (i = 0; i < size; ++i) {

        unsigned char code = (unsigned char)buffer[i];
        for (j = 0; j < 66; ++j) {
            if (code == _from[j]) {
                buffer[i] = (char)_to[j];
                break;
            }
        }
    }

    return 0;
}

static struct file_operations fops =
        {
                .open = dev_open,
                .read = dev_read,
                .write = dev_write,
                .release = dev_release,
        };

static int dev_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int dev_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
    encode(message, cp1251, koi8_r);
    copy_to_user(buffer, message, size_of_message);
    size_of_message = strlen(message);

    memset(message, 0, 256);

    return size_of_message;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
    copy_from_user(message, buffer, len);
    size_of_message = strlen(message);

    return len;
}

static int encodings_init(void)
{
    int result = 0;
    printk(KERN_NOTICE "kernel-encoding: registering device...");

    result = register_chrdev(0, device_name, &fops);

    if (result < 0)
    {
        printk(KERN_WARNING "kernel-encoding: can\'t register character device with errorcode = %i", result);
        return result;
    }

    device_file_major_number = result;
    printk(KERN_NOTICE "kernel-encoding: registered character device with major number = %i and minor numbers 0...255", device_file_major_number);
    return 0;
}

static void encodings_exit(void)
{
    printk(KERN_NOTICE "kernel-encoding: unregistering device");
    if (device_file_major_number != 0)
    {
        unregister_chrdev(device_file_major_number, device_name);
    }
}

module_init(encodings_init);
module_exit(encodings_exit);