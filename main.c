#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>       /* printk() */
#include <linux/slab.h>         /* kmalloc() */ 
#include <linux/fs.h>           /* everything... */
#include <linux/errno.h>        /* error code */
#include <linux/types.h>        /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>        /* 0_ACCMODE */
#include <linux/seq_file.h>     
#include <linux/cdev.h>

#include <asm/uaccess.h>        /* copy_(from/to)_user */

#include "scull.h"              /* local header */


int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_LICENSE("GPL");

struct scull_dev *scull_devices;

#ifdef SCULL_DEBUG

struct proc_dir_entry *scull_proc_entry;
static void *scull_seq_start(struct seq_file *s, loff_t *p);
static void *scull_seq_next(struct seq_file *s, void* v, loff_t* p);
static void scull_seq_stop(struct seq_file *s, void *v);
static int scull_seq_show(struct seq_file *s, void *v);
struct seq_operations seq_ops = {
        .next = scull_seq_next,
        .start = scull_seq_start,
        .stop = scull_seq_stop,
        .show = scull_seq_show
};

static void *scull_seq_start(struct seq_file *s, loff_t *p)
{
        /*
         * The start of the seq_file iterator
         * Shoule return the pointer to the start of the 
         * scull_devices, or NULL if the offset p go out of range
         */
        return *p < scull_nr_devs ? scull_devices + *p : NULL;
}

static void *scull_seq_next(struct seq_file *s, void* v, loff_t *p)
{
        /*
         * Invoking to iterate to the next item,
         * should increment the offset p and return the pointer 
         * to the next scull_devices.
         * The pointer v is the iterator (point to scull_device)
         */
        ++*p;
        return *p < scull_nr_devs ? scull_devices + *p : NULL;
}

static void scull_seq_stop(struct seq_file *s, void *v)
{
        /* Iterator stop, do nothing ... */
}

static int scull_seq_show(struct seq_file *s, void *v)
{
        struct scull_dev *dev = (struct scull_dev *)v;
        struct scull_qset *qs;
        int i, j;

        if (mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;
        seq_printf(s, "Device %i, qset %i, quantum %i, size %li\n", (int)(dev-scull_devices), dev->qset, dev->quantum, dev->size);
        for (qs = dev->data; qs != NULL; qs = qs->next){
                seq_printf(s, "\titem at %p, qset at %p\n", qs, qs->data);
                for (i = 0; i < dev->qset; i++){
                        if (qs->data[i])
                                seq_printf(s, "\t\t%i: %p\n", i, qs->data[i]);
                }
        }
        mutex_unlock(&dev->mutex);
        return 0;
}

static int scull_seq_open(struct inode *inode, struct file* filp)
{
        return seq_open(filp, &seq_ops);
}

struct proc_ops scull_proc_ops = {
        .proc_open = scull_seq_open,
        .proc_read = seq_read,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release
};

static void scull_proc_create(void){
        scull_proc_entry = proc_create_data("scullseq", 0, NULL, &scull_proc_ops, NULL);
}

static void scull_proc_remove(void)
{
        proc_remove(scull_proc_entry);
}
#endif /* SCULL_DEBUG */



int scull_trim(struct scull_dev *dev){
        struct scull_qset *next, *dptr;
        int qset = dev->qset;
        int i;
        
        for(dptr = dev->data; dptr; dptr = next){
                if (dptr->data){
                        for (i = 0; i < qset; i++)
                                kfree(dptr->data[i]);
                        kfree(dptr->data);
                        dptr->data = NULL;
                }
                next = dptr->next;
                kfree(dptr);
        }
        dev->size = 0;
        dev->quantum = scull_quantum;
        dev->qset = scull_qset;
        dev->data = NULL;
        return 0;
}

/*
 * Open and close file
 */

int scull_open(struct inode *inode, struct file *filp){
        struct scull_dev *dev;
        
        /*
         * Macro container_of find the the struct of type (param 2) that hold the (param 1) pointer as member that name as (param 3),
         * Here inode contain the i_cdev is the cdev pointer in the device structure of interest,
         * This is where the container_of come in.
         */
        dev = container_of(inode->i_cdev, struct scull_dev, cdev);
        filp->private_data = dev; /* Which will be used by other menthod (eg read/write,...) */

        /* If the device was opened write-only, trim it to a length of 0. */
        if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
                if (mutex_lock_interruptible(&dev->mutex))
                        return -ERESTARTSYS;
                scull_trim(dev);
                mutex_unlock(&dev->mutex);
        }
        return 0;
}

int scull_release(struct inode *inode, struct file* filp)
{
        /* Do nothing? */
        return 0;
}

/*
 * To follow the list
 * kmalloc and assign 0 if try to read/write to uninitiated memory
 */

struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
        struct scull_qset *qs = dev->data;

        /* Allocate the first qset explicitly if need be. */
        if (!qs){
                qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
                if (qs == NULL)
                        return NULL;
                memset(qs, 0, sizeof(struct scull_qset));
        }

        /* Follow the list. */
        while(n--){
                if(!qs->next){
                        qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
                        if (qs->next == NULL)
                                return NULL;
                        memset(qs->next, 0, sizeof(struct scull_qset));
                }
                qs = qs->next;
        }
        return qs;
}


ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
        struct scull_dev *dev = filp->private_data;
        struct scull_qset *dptr;
        int quantum = dev->quantum, qset = dev->qset;
        int itemsize = quantum * qset;
        int item, s_pos, q_pos, rest;
        ssize_t retval = 0;

        if(mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;
        if (*f_pos >= dev->size)
                goto out;
        if (*f_pos + count >= dev->size)
                count = dev->size - *f_pos;

        /* Find in the list item , qset index, and offset int quantum */
        item = (long)*f_pos / itemsize;
        rest = (long)*f_pos % itemsize;
        s_pos = rest / quantum;
        q_pos = rest % quantum;

        /* Trace to the reading position */
        dptr = scull_follow(dev, item);

        if (dptr == NULL|| !dptr->data || !dptr->data[s_pos])
                goto out;
        
        /* read only to the end of this quantum, it will retry to read again until the number of read data is satified*/
        if (count > quantum - q_pos)
                count = quantum - q_pos;

        if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)){
                retval = -EFAULT;
                goto out;
        }
        *f_pos += count;
        retval = count;
        PDEBUG("dptr now at %p, compare to dev->data %p, userspace pointer at %p, is scull_devices pointer still the same %p", dptr, dev->data, buf, scull_devices);

out:
        mutex_unlock(&dev->mutex);
        return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
        struct scull_dev *dev = filp->private_data;
        struct scull_qset *dptr;
        int quantum = dev->quantum, qset = dev->qset;
        int itemsize = quantum * qset;
        int item, s_pos, q_pos, rest;
        ssize_t retval = -ENOMEM;

        if(mutex_lock_interruptible(&dev->mutex))
                return -ERESTARTSYS;

        item = (long)*f_pos / itemsize;
        rest = (long)*f_pos % itemsize;
        s_pos = rest / quantum;
        q_pos = rest % quantum;

        dptr = scull_follow(dev, item);
        
        if (dptr == NULL)
                goto out;
        if (!dptr->data){
                dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
                if (!dptr->data)
                        goto out;
                memset(dptr->data, 0, qset * sizeof(char *));
        }
        if (!dptr->data[s_pos]){
                dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
                if (!dptr->data[s_pos])
                        goto out;
        }
        PDEBUG("dptr now at %p, compare to dev->data %p, userspace pointer at %p, is scull_devices pointer still the same %p", dptr, dev->data, buf, scull_devices);

        if (count > quantum - q_pos)
                count = quantum - q_pos;


        
        if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)){
                retval = -EFAULT;
                goto out;
        }
        *f_pos += count;
        retval = count;

        PDEBUG("dptr now at %p, compare to dev->data %p, userspace pointer at %p, is scull_devices pointer still the same %p", dptr, dev->data, buf, scull_devices);
        /* Update the size of device. */
        if(dev->size < *f_pos)
                dev->size = *f_pos;
out:
        mutex_unlock(&dev->mutex);
        return retval;
}

/*
 * The "extended" operations -- only seek.
 */

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
	struct scull_dev *dev = filp->private_data;
	loff_t newpos;

	switch(whence) {
	  case 0: /* SEEK_SET */
		newpos = off;
		break;

	  case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	  case 2: /* SEEK_END */
		newpos = dev->size + off;
		break;

	  default: /* can't happen */
		return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
	filp->f_pos = newpos;
	return newpos;
}

struct file_operations scull_fops = {
        .owner = THIS_MODULE,
        .llseek = scull_llseek,
        .read = scull_read,
        .write = scull_write,
        .open = scull_open,
        .release = scull_release,
};

void scull_cleanup_module(void)
{
        int i;
        dev_t devno = MKDEV(scull_major, scull_minor);

        /* Get rid of the char dev entries. */
        if (scull_devices) {
                for (i = 0; i < scull_nr_devs; i++){
                        scull_trim(scull_devices + i);
                        cdev_del(&scull_devices[i].cdev);
                }
                kfree(scull_devices);
        }
#ifdef SCULL_DEBUG
        scull_proc_remove();
#endif
        unregister_chrdev_region(devno, scull_nr_devs);

}


static void scull_setup_cdev(struct scull_dev *dev, int index)
{
        int err, devno = MKDEV(scull_major, scull_minor + index);
        cdev_init(&dev->cdev, &scull_fops);
        dev->cdev.owner = THIS_MODULE;
        dev->cdev.ops = &scull_fops;
        err = cdev_add (&dev->cdev, devno, 1);

        if (err)
                printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

int scull_init_module(void)
{
        int res, i;
        dev_t dev = 0;
        /*
         * Get a range of minor numbers to work with, asking for a dynamic major
         * unless major number was assign at load time.
         */
        if (scull_major) {
                dev = MKDEV(scull_major, scull_minor);
                res = register_chrdev_region(dev, scull_nr_devs, "scull");
        } else {
                res = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
                scull_major = MAJOR(dev);
        }
        if (res < 0) {
                printk(KERN_WARNING "scull: cannot get major %d\n", scull_major);
                return res;
        }

        /*
         * Allocate the devices memory. This must be dynamic as the device number
         * can be specified at load time
         */
        scull_devices = kmalloc (scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
        if (!scull_devices) {
                res = -ENOMEM;
                goto fail;
        }
        memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

        /* Initialize each device */
        for (i = 0; i < scull_nr_devs; i++){
                scull_devices[i].quantum = scull_quantum;
                scull_devices[i].qset = scull_qset;
                mutex_init(&scull_devices[i].mutex);
                scull_setup_cdev(&scull_devices[i], i);
        }
#ifdef SCULL_DEBUG
        scull_proc_create();
#endif
       
        return 0;

fail:
        scull_cleanup_module();
        return res;
}
module_init(scull_init_module);
module_exit(scull_cleanup_module);
