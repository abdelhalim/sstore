/*
 * sstore.c
 *
 * Copyright (C) 2010 Abdelhalim Ragab <abdelhalim@r8t.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */


#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h> /* copy_from_user & copy_to_user */
#include <linux/proc_fs.h>
#include <linux/wait.h>
#include <linux/sched.h> /* current and everthing */
#include <linux/timer.h> /* timer */
#include <linux/kthread.h> /* kthread stuff */


#include "sstore.h"

#define DEBUG

static int max_num_blobs = 5;
static int max_blob_size = 64;
module_param(max_num_blobs, int, S_IRUGO);
module_param(max_blob_size, int, S_IRUGO);

/* statisics are cleared ever 'clear_time' seconds */
static int clear_time = 60; 

struct timer_list clear_timer;
static struct task_struct *clear_thread_ptr;
static wait_queue_head_t clear_thread_wait;
static char timer_off;




/* /proc sstore directory, create in the init, 
   and removed in the release */
struct proc_dir_entry *sstore_proc;

struct blob {
  char *data;
  int  size;
};



/* Per-device structure */
struct sstore_dev {
  void **data;
  unsigned short current_pointer; /* Current pointer */
  unsigned int size;              /* Size */
  int store_number;               /* store number */
  int nreads, nwrites;	          /* number of reads/writes */
  char name[10];		  /* Name */
  struct cdev cdev;               /* The cdev structure */
  struct mutex sstore_mutex;
  wait_queue_head_t wq;
  atomic_t refcount;
} *sstore_devp[NUM_MINOR_DEVICES];



int sstore_open(struct inode *inode, struct file *file);
int sstore_release(struct inode *inode, struct file *file);
ssize_t sstore_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
ssize_t sstore_write(struct file *file, const char __user *buf,
           size_t count, loff_t *ppos);
static int sstore_ioctl(struct inode *inode, struct file *file,
           unsigned int cmd, unsigned long arg);

/* operation prototype for the /proc fs */
int sstore_read_procmem(char *buf, char **start, off_t offset,
                       int count, int *eof, void *data);
int sstore_read_procstats(char *buf, char **start, off_t offset,
                       int count, int *eof, void *data);

static void sstore_clear_statistics(unsigned long params); 
static int clear_thread(void *dummy);

/* File operations structure. Defined in linux/fs.h */
static struct file_operations sstore_fops = {
  .owner    =   THIS_MODULE,        /* Owner */
  .open     =   sstore_open,        /* Open method */
  .release  =   sstore_release,     /* Release method */
  .read     =   sstore_read,        /* Read method */
  .write    =   sstore_write,       /* Write method */
  .ioctl    =   sstore_ioctl,       /* Ioctl method */
};

static dev_t sstore_dev_number;   /* Allotted device number */
struct class *sstore_class;       /* Tie with the device model */

#define DEVICE_NAME             "sstore"

/*
 * Driver Initialization
 */
int __init
sstore_init(void)
{
  int i, ret;

  /* Request dynamic allocation of a device major number */
  if (alloc_chrdev_region(&sstore_dev_number, 0,
                          NUM_MINOR_DEVICES, DEVICE_NAME) < 0) {
    printk(KERN_DEBUG "sstore: Can't register device\n"); return -1;
  }

  /* Populate sysfs entries */
  sstore_class = class_create(THIS_MODULE, DEVICE_NAME);

  for (i=0; i<NUM_MINOR_DEVICES; i++) {
    /* Allocate memory for the per-device structure */
    sstore_devp[i] = kmalloc(sizeof(struct sstore_dev), GFP_KERNEL);
    if (!sstore_devp[i]) {
      printk("sstore: Bad Kmalloc\n"); 
      return -ENOMEM;
    }

    /* ref count */
    atomic_set(&sstore_devp[i]->refcount, 1);

    /* sstore storage */
    sstore_devp[i]->data = NULL;

    sprintf(sstore_devp[i]->name, "sstore%d", i);

    /* Fill in the bank number to correlate this device
       with the corresponding sstore number */
    sstore_devp[i]->store_number = i;

    /* initialize the mutex */
    mutex_init(&sstore_devp[i]->sstore_mutex);

    /* initialize wait queues */
    init_waitqueue_head(&sstore_devp[i]->wq);

    /* initialize number of read/write operations to 0 */
    sstore_devp[i]->nreads = 0;
    sstore_devp[i]->nwrites = 0;

    /* Connect the file operations with the cdev */
    cdev_init(&sstore_devp[i]->cdev, &sstore_fops);
    sstore_devp[i]->cdev.owner = THIS_MODULE;

    /* Connect the major/minor number to the cdev */
    ret = cdev_add(&sstore_devp[i]->cdev, (sstore_dev_number + i), 1);
    if (ret) {
      printk("sstore: Bad cdev\n");
      return ret;
    }

    /* Send uevents to udev, so it'll create /dev nodes */
    device_create(sstore_class, NULL, MKDEV(MAJOR(sstore_dev_number), i),
                  "sstore%d", i);
  }


  /* */
  init_waitqueue_head(&clear_thread_wait);
  clear_thread_ptr = kthread_run(clear_thread, NULL, "clear_thread");
  if(IS_ERR(clear_thread_ptr)) {
    printk(KERN_DEBUG "sstore: error creating thread\n");
  }

  /* initialize the timer responsible for clearing statistic */
  init_timer(&clear_timer); 
  clear_timer.expires = jiffies + clear_time*HZ; 
  clear_timer.function = sstore_clear_statistics;
  clear_timer.data = 0;
  add_timer(&clear_timer);
  

  sstore_proc = proc_mkdir("sstore", NULL);
  create_proc_read_entry("data", 0, sstore_proc, 
                         sstore_read_procmem, NULL);
  
  create_proc_read_entry("stats", 0, sstore_proc, 
                         sstore_read_procstats, NULL);

  printk("sstore: SStore Driver Initialized.\n");
  return 0;
}


/*
 * clear statistics
 */
static void sstore_clear_statistics(unsigned long params) 
{
  printk(KERN_ALERT "sstore: clearing sstore statistics\n");
  wake_up_interruptible(&clear_thread_wait);
  timer_off = 1;

  mod_timer(&clear_timer, jiffies + clear_time*HZ); 
}

/* clear sstore statistics */

void clear_statistics() {
  int i;
  for (i=0; i<NUM_MINOR_DEVICES; i++) {
    mutex_lock(&sstore_devp[i]->sstore_mutex);
    sstore_devp[i]->nreads  = 0;
    sstore_devp[i]->nwrites = 0;
    mutex_unlock(&sstore_devp[i]->sstore_mutex);
  }
}

static int
clear_thread(void *dummy) 
{
  int rc;

  while (1) {
    rc = wait_event_interruptible(clear_thread_wait,
             timer_off || kthread_should_stop());

    if (kthread_should_stop() || rc == -ERESTARTSYS) {
      break;
    }
    
    clear_statistics();
    timer_off = 0;
  }
  return 0;
}


/* Driver Exit */
void __exit
sstore_cleanup(void)
{
  int i;

  /* Release the major number */
  unregister_chrdev_region((sstore_dev_number), NUM_MINOR_DEVICES);

  /* Release I/O region */
  for (i=0; i<NUM_MINOR_DEVICES; i++) {
    device_destroy (sstore_class, MKDEV(MAJOR(sstore_dev_number), i));
    /*release_region(addrports[i], 2); */
    cdev_del(&sstore_devp[i]->cdev);
    kfree(sstore_devp[i]);
  }
  /* Destroy sstore_class */
  class_destroy(sstore_class);

  kthread_stop(clear_thread_ptr);
  del_timer_sync(&clear_timer);

  
  /* clean all /proc entries */
  remove_proc_entry("data", sstore_proc);
  remove_proc_entry("stats", sstore_proc);
  remove_proc_entry("sstore", NULL);

  return;
}

/*
 * Open sstore
 */
int
sstore_open(struct inode *inode, struct file *file)
{

  struct sstore_dev *dev; /* device information */

  // Only root is allowed
  if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

  printk(KERN_DEBUG "sstore: SStore device opened\n"); 

  dev = container_of(inode->i_cdev, struct sstore_dev, cdev);
  file->private_data = dev; /* to be used by other methods */

  /* check if this is the first time to open the device*/
  if (atomic_dec_and_test(&dev->refcount)) {
    printk(KERN_DEBUG "sstore: first device open, init memory ...\n"); 
 	 
    mutex_lock(&dev->sstore_mutex);

    /* Allocate memory for an array of pointers to the blobs */
    dev->data = kzalloc(max_num_blobs * sizeof(struct blob *), GFP_KERNEL);
    if (!dev->data) {
	printk(KERN_DEBUG "sstore: Couldn't allocate memory for the sstore blobs\n");
       return -ENOMEM;
    }
    mutex_unlock(&dev->sstore_mutex);
  } 

  // Do we need to reset anything?

  return 0;
}

/*
 * Release sstore
 */
int
sstore_release(struct inode *inode, struct file *file)
{
  struct sstore_dev *dev = file->private_data;

  printk(KERN_DEBUG "sstore: SStore device released\n"); 

  /* TODO do we need to check for open count? */
  atomic_inc(&dev->refcount); /* release the device */
  return 0;
}

/*
 * Read from a sstore at given index
 * If the read index is past end-of-store then block until
 * a new record is written at its index.
 */
ssize_t
sstore_read(struct file *file, char __user *u_buf,
          size_t count, loff_t *ppos)
{
  struct sstore_dev *dev = file->private_data;
  struct data_buffer *k_buf; /* has the index, size, 
			    and data to be written */
  ssize_t bytes_read = 0; /* Hmm, what about count arg */
  struct blob *blob;
  
  printk(KERN_DEBUG "sstore: read\t"); 

  k_buf = kmalloc (sizeof (struct data_buffer), GFP_KERNEL);
  if (!k_buf) {
    printk("sstore: Bad kmalloc\n");
    return -ENOMEM;
  }
  
  if(copy_from_user(k_buf, u_buf, sizeof (struct data_buffer))) {
    printk("sstore: Copy from user\n");
    kfree(k_buf);
    return -EFAULT;
  }

#ifdef DEBUG
  printk("index: %d\t", k_buf->index);
  printk("size : %d\n", k_buf->size);
#endif

  /* check if index is valid */
  if(k_buf-> index < 0
    || k_buf->index > max_num_blobs) {
    printk(KERN_INFO "sstore: Invalid \"index\" in the read request\n");
    kfree(k_buf);
    return -EINVAL;
  }

  /* check if the requested size makes sense */
  if(k_buf->size <= 0
    || k_buf->size > max_blob_size) {
    printk(KERN_DEBUG "sstore: Invalid \"size\" in the read request\n");
    kfree(k_buf);
    return -EINVAL;
  }
#ifdef DEBUG
  printk(KERN_DEBUG "sstore: mutex read\n");
#endif

  mutex_lock(&dev->sstore_mutex);
  blob = dev->data[k_buf->index];

  if (!blob) {
    printk(KERN_INFO "sstore: Invalid Index, sleeping ...\n");
    /* sleep & wait for data */
    mutex_unlock(&dev->sstore_mutex);
    wait_event_interruptible(dev->wq, dev->data[k_buf->index]);
    mutex_lock(&dev->sstore_mutex);
    blob = dev->data[k_buf->index];
  } 
  if (signal_pending(current)) {
    printk(KERN_ALERT "sstore: pid %u got signal.\n", (unsigned) current->pid);
    mutex_unlock(&dev->sstore_mutex);
    kfree(k_buf);
    return -EINTR;
  }

#ifdef DEBUG
  printk("sstore: User Data: %s\n", blob->data);
#endif


  /* make sure the requested size is not larger 
   * than the existing data, if this is the case, then set
   * the requested size to the blob size */
  if (k_buf->size > blob->size) {
    printk(KERN_ALERT "sstore: requested read size is larger than the existing\n");
    k_buf->size = blob->size;
    bytes_read = blob->size;
  } 

  if(copy_to_user(k_buf->data, blob->data, k_buf->size)) {
    printk("sstore: Copy from user\n");
    mutex_unlock(&dev->sstore_mutex);
    kfree(k_buf);
    return -EFAULT;
  }
  /* Increment number of read operations */
  dev->nreads++;

  mutex_unlock(&dev->sstore_mutex);

  kfree(k_buf);

  return bytes_read;
}

/*
 * Write to a sstore at a given index
 */
ssize_t
sstore_write(struct file *file, const char __user *u_buf,
           size_t count, loff_t *ppos)
{
  struct sstore_dev *dev = file->private_data;
  struct data_buffer *k_buf; /* has the index, size, 
			    and data to be written */
  ssize_t bytes_written = 0;

  struct blob *blob;

  printk(KERN_DEBUG "sstore: Write\t"); 

  k_buf = kmalloc (sizeof (struct data_buffer), GFP_KERNEL);
  if (!k_buf) {
    printk("sstore: Bad kmalloc\n");
    return -ENOMEM;
  }
  
  if(copy_from_user(k_buf, u_buf, sizeof (struct data_buffer))) {
    printk(KERN_DEBUG "sstore: Problem copying from user space\n");
    kfree(k_buf);
    return -EFAULT;
  }

#ifdef DEBUG
  printk("index: %d\t", k_buf->index);
  printk("size : %d\n", k_buf->size);
#endif

  /* check if index value is valid */
  if (k_buf->index < 0 
      || k_buf->index > max_num_blobs) {
    printk(KERN_INFO "sstore: Invalid \"index\" in the write request"); 
    kfree(k_buf);
    return -EINVAL;
  }

  if (k_buf->size < 0 
     || k_buf->size > max_blob_size) {
    printk(KERN_DEBUG "sstore: Invalid \"size\" in the write request.\n");
    kfree(k_buf);
    return -EINVAL;
  } 

  blob = kmalloc(sizeof (struct blob), GFP_KERNEL);
  if (blob) {
    blob->data = kmalloc(k_buf->size, GFP_KERNEL);
    if (!blob->data) {
      printk("sstore: Bad kmalloc\n");
      kfree(k_buf);
      return -ENOMEM;
    }

    if(copy_from_user(blob->data, k_buf->data, k_buf->size)) {
      printk(KERN_DEBUG "sstore: Problem copying from user space\n");
      kfree(k_buf);
      kfree(blob);
      return -EFAULT;
    }
    printk(KERN_DEBUG "sstore: Finished copying from user space\n");

    blob->size = k_buf->size;

#ifdef DEBUG
  printk(KERN_DEBUG "sstore: Write mutex\n");
#endif
    mutex_lock(&dev->sstore_mutex);
    dev->data[k_buf->index] = blob;
    bytes_written = k_buf->size;

#ifdef DEBUG
  printk("sstore: User Data: %s\n", blob->data);
#endif
    /* Increment number of write operations */
    dev->nwrites++;

    mutex_unlock(&dev->sstore_mutex);
    wake_up_interruptible(&dev->wq);

  }

  /* free the allocated kernel buffer */
  kfree(k_buf);

  return bytes_written;
}

/*
 * Ioctls 
 */
static int
sstore_ioctl(struct inode *inode, struct file *file,
           unsigned int cmd, unsigned long arg)
{
  int retval = 0;
  unsigned int index;
  struct sstore_dev *dev = file->private_data;
  struct blob* blobp;
  
  /* extract the type and make sure we have correct cmd */
  if (_IOC_TYPE(cmd) != SSTORE_IOC_MAGIC) return -ENOTTY;
  /* TODO any extra checks for the cmd? */


  switch (cmd) {
    case SSTORE_IOCREMOVE:
      printk(KERN_DEBUG "sstore: Remove blob\n");
      retval = get_user(index, (unsigned int __user *) arg);
      if (!retval) { /* success */
        /* make sure the index is within the boundaris */
        if (index < 0 || index > max_num_blobs)
          return -EINVAL;

        blobp = dev->data[index];
        if (blobp) { /* valid blob */
          kfree(blobp->data);
          kfree(blobp);
          printk(KERN_DEBUG "sstore: Freeing blob memory\n");
          dev->data[index] = NULL;
        } else { /* blob @ index is not valid */
          printk(KERN_INFO "sstore: Request to remove invalid entry\n");
          return -ENOTTY;
        }
      }
      
      break;
    default:
      return -ENOTTY;

  }

  return retval;
}

int sstore_read_procmem(char *buf, char **start, off_t offset,
                       int count, int *eof, void *data)
{
  int len = 0;
  int i,j, k;
  struct blob *blobp;
  unsigned short line_width = 16;

  /* TODO should we set limit to the printed output?*/
  for (i = 0; i < NUM_MINOR_DEVICES ; i++) {
    len += sprintf(buf+len, "\nDevice %i:", i);

    mutex_lock(&sstore_devp[i]->sstore_mutex);
    if (sstore_devp[i]->data) {
      for (j = 0; j < max_num_blobs; j++) {
        blobp = sstore_devp[i]->data[j]; 
        if (blobp) {
          len += sprintf(buf+len, "\nblob %i size %i data:", j, blobp->size);
	  for (k = 0; k < blobp->size; k++) {
            if (k%line_width == 0) {
              len += sprintf(buf+len, "\n");
            }
            len += sprintf(buf+len, "%x ", blobp->data[k]);
          }
        } else {
          len += sprintf(buf+len, "\nblob %i has no data\n", j);
	}
      }
    } else {
      len += sprintf(buf+len, "no data device %i\n", i);
    }
    
    mutex_unlock(&sstore_devp[i]->sstore_mutex);
  }

  *eof = 1;
  return len;
}

int sstore_read_procstats(char *buf, char **start, off_t offset,
                       int count, int *eof, void *data)
{
  int len = 0;
  int i;

  for (i = 0; i < NUM_MINOR_DEVICES ; i++) {
    len += sprintf(buf+len, "Device %i: ", i);

    mutex_lock(&sstore_devp[i]->sstore_mutex);
    len += sprintf(buf+len, "reads: %i\t", sstore_devp[i]->nreads);
    len += sprintf(buf+len, "writes: %i\n", sstore_devp[i]->nwrites);

    mutex_unlock(&sstore_devp[i]->sstore_mutex);
  }

  *eof = 1;
  return len;
}

module_init(sstore_init);
module_exit(sstore_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Abdelhalim Ragab (abdelhalim @ r8t.org)");
