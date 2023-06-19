#ifndef _SCULL_H_
#define _SCULL_H_

#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/cdev.h>

#undef PDEBUG
#ifdef SCULL_DEBUG
#ifdef __KERNEL__
#define PDEBUG(fmt, args...) printk(KERN_DEBUG "scull: " fmt, ## args)
#else
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#endif
#else
#define PDEBUG(fmt, args...)
#endif

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0 /* dynamic major by default */
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4 /* scull0 to scull3 */
#endif

/*
 * The bear device is a variable-length region of memory .Use a 
 * linked list of indirect blocks.
 *
 * "scull_dev->data" point to an array of pointers, each pointer
 * refer to a memory area of SCULL_QUANTUM bytes long.
 *
 * The array "quantum set" (qset) is SCULL_QSET long.
 * 
 * In hindsight scull_dev->data point to first qset
 * qset->next point to next qset,
 * uset->data point to pointer field of quantum of memory
 */

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET 
#define SCULL_QSET 1000
#endif

/*
 * The scull quantum of data structure
 */

struct scull_qset {
        void** data;                    /* note: this is pointer to a array of pointer (1000) that point to an area of memory of 4000 bytes */
        struct scull_qset* next;        /* Quantum data make a linked list */
};

/*
 * Scull device structure
 */

struct scull_dev {
        struct scull_qset* data;        /* Pointer to the first quantum set of data */ 
        int quantum;                    /* Size of a each quantum */
        int qset;                       /* Size of the quantum array(table of pointer) */
        unsigned long size;
        struct cdev cdev;
        struct mutex mutex;
};


/*
 * Split minors in two parts
 */

#define TYPE(minor)     (((minor) >> 4) & 0xf)
#define NUM(minor)      ((minor) & 0xf)

/*
 * The different configurable parameters
 */

extern int scull_major;
extern int scull_nr_devs;
extern int scull_quantum;
extern int scull_qset;


/*
 * Prototypes for shared functions
 */

int     scull_trim(struct scull_dev* dev);
ssize_t scull_read(struct file* filp, char __user *buf, size_t count, loff_t*  f_pos);
ssize_t scull_write(struct file* filp, const char __user *buf, size_t count, loff_t* f_pos);
#endif
