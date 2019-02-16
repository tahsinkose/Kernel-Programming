#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tahsincan Kose");
MODULE_DESCRIPTION("A simple Linux Hello world driver.");
MODULE_VERSION("0.1");


static char *name = "world";
module_param(name,charp, S_IRUGO);
MODULE_PARM_DESC(name, "The name to display in /var/log/kern.log");

// static restricts the visibility of the function to within this C file.
// __init macro means that for a built-in driver (not a LKM) the function is only used
// at initialization time and that it can be discarded and its memory freed up after
// that point.
static int __init hello_init(void){
	printk(KERN_INFO "Hello %s from the LKM",name);
	return 0;
}

static void __exit hello_exit(void){
	printk(KERN_INFO "Goodbye %s from the LKM!\n",name);
}

module_init(hello_init);
module_exit(hello_exit);