#include <linux/module.h>  
#include <linux/kernel.h>  
#include <linux/init.h>     


static int __init lab3_init(void)
{
    printk(KERN_INFO "Welcome to the Tomsk State University\n");
    return 0;
}


static void __exit lab3_exit(void)
{
    printk(KERN_INFO "Tomsk State University forever!\n");
}


module_init(lab3_init);
module_exit(lab3_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Artem");
MODULE_DESCRIPTION("TSU Lab 3 kernel module");
