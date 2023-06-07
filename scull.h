#ifndef _SCULL_H_
#define _SCULL_H_

#include <linux/ioctl.h>
#include <linux/mutex.h>

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
        void** data;
        struct scull_qset* next;
};

/*
 * Scull device structure
 */

struct scull_dev {
        struct scull_qset* data; 
        
};
#endif
