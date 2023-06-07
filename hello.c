#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>

int howmany;
char* whom = "world";
module_param(howmany, int, S_IRUGO);
module_param(whom, charp, S_IRUGO);
static int __init hello_init(void)
{
        int i;
        for (i = 0; i < howmany; ++i){
	        printk(KERN_ALERT "Hello, %s\n", whom);
        }
	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_ALERT "Goodbye, cruel world\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("Dual BSD/GPL");
