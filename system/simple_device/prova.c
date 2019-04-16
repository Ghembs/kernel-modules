#include "device.h"
#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("BrUH");

static int my_init(void)
{
    int result = 0;
    printk( KERN_NOTICE "Simple-driver: Initialization started" );

    result = register_device();
    return result;
}

static void my_exit(void)
{
    printk( KERN_NOTICE "Simple-driver: Exiting" );
    unregister_device();
}

module_init(my_init);
module_exit(my_exit);