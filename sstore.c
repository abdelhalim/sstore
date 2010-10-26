#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h> /* copy_from_user & copy_to_user */

#include "sstore.h"

#define DEBUG

static int num_blobs = 5;
static int max_size = 10;
module_param(num_blobs, int, S_IRUGO);
module_param(max_size, int, S_IRUGO);


#define NUM_MINOR_DEVICES          2

static atomic_t sstore_closed = ATOMIC_INIT(1);


struct blob {
  char *data;
  int  size;
  char isValid;
};

struct data_buffer {
    int index;      /* index into the blob list */
    int size;       /* size of the data transfer */
    char *data;     /* where the data being transfered resides */
};


/* Per-device structure */
struct sstore_dev {
  void **data;
  unsigned short current_pointer; /* Current pointer */
  unsigned int size;              /* Size */
  int store_number;               /* store number */
  int nreaders, nwriters;	  /* number of readers/writers */
  char name[10];		  /* Name */
  struct cdev cdev;               /* The cdev structure */
  /* ... */                       /* Mutexes, spinlocks, wait
                                     queues, .. */
} *sstore_devp[NUM_MINOR_DEVICES];



int sstore_open(struct inode *inode, struct file *file);
int sstore_release(struct inode *inode, struct file *file);
ssize_t sstore_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
ssize_t sstore_write(struct file *file, const char __user *buf,
           size_t count, loff_t *ppos);
static loff_t sstore_llseek(struct file *file, loff_t offset, int orig);
static int sstore_ioctl(struct inode *inode, struct file *file,
           unsigned int cmd, unsigned long arg);

/* File operations structure. Defined in linux/fs.h */
static struct file_operations sstore_fops = {
  .owner    =   THIS_MODULE,        /* Owner */
  .open     =   sstore_open,        /* Open method */
  .release  =   sstore_release,     /* Release method */
  .read     =   sstore_read,        /* Read method */
  .write    =   sstore_write,       /* Write method */
  .llseek   =   sstore_llseek,      /* Seek method */
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
    printk(KERN_DEBUG "Can't register device\n"); return -1;
  }

  /* Populate sysfs entries */
  sstore_class = class_create(THIS_MODULE, DEVICE_NAME);

  for (i=0; i<NUM_MINOR_DEVICES; i++) {
    /* Allocate memory for the per-device structure */
    sstore_devp[i] = kmalloc(sizeof(struct sstore_dev), GFP_KERNEL);
    if (!sstore_devp[i]) {
      printk("Bad Kmalloc\n"); return -ENOMEM;
    }

    sprintf(sstore_devp[i]->name, "sstore%d", i);

    /* Fill in the bank number to correlate this device
       with the corresponding sstore number */
    sstore_devp[i]->store_number = i;

    /* Connect the file operations with the cdev */
    cdev_init(&sstore_devp[i]->cdev, &sstore_fops);
    sstore_devp[i]->cdev.owner = THIS_MODULE;

    /* Connect the major/minor number to the cdev */
    ret = cdev_add(&sstore_devp[i]->cdev, (sstore_dev_number + i), 1);
    if (ret) {
      printk("Bad cdev\n");
      return ret;
    }

    /* Send uevents to udev, so it'll create /dev nodes */
    device_create(sstore_class, NULL, MKDEV(MAJOR(sstore_dev_number), i),
                  "sstore%d", i);
  }

  printk("SStore Driver Initialized.\n");
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
  return;
}

/*
 * Open sstore
 */
int
sstore_open(struct inode *inode, struct file *file)
{

  struct sstore_dev *dev; /* device information */

#ifndef DEBUG
  // Only root is allowed
  if (!capable(CAP_SYS_ADMIN))
        return -EPERM;
#endif 

  printk(KERN_DEBUG "SStore device opened\n"); 

  dev = container_of(inode->i_cdev, struct sstore_dev, cdev);
  dev->data = NULL;
  file->private_data = dev; /* to be used by other methods */

  /* check if this is the first time to open the device*/
  if (atomic_dec_and_test(&sstore_closed)) {
 	 
    /* Allocate memory for an array of pointers to the blobs */
    dev->data = kmalloc(num_blobs * sizeof(struct blob *), GFP_KERNEL);
    if (!dev->data) {
	printk(KERN_DEBUG "Couldn't allocate memory for the sstore blobs\n");
	/* TODO should I return with error? */
    }
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
  printk(KERN_DEBUG "SStore device released\n"); 

  /* TODO do we need to check for open count? */
  atomic_inc(&sstore_closed); /* release the device */
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
  
  printk(KERN_DEBUG "SStore Read\n"); 

  k_buf = kmalloc (sizeof (struct data_buffer), GFP_KERNEL);
  if (!k_buf)
    printk("Bad kmalloc\n");
  
  if(copy_from_user(k_buf, u_buf, sizeof (struct data_buffer))) {
    printk("Copy from user\n");
  }

#ifdef DEBUG
  printk("Index: %d\n", k_buf->index);
  printk("Size : %d\n", k_buf->size);
#endif
  /* check if index is valid */
  if(k_buf-> index < 0
    || k_buf->index > num_blobs) {
    printk(KERN_INFO "Invalid \"index\" in the read request\n");
    return -EINVAL;
  }

  if(k_buf->size <= 0
    || k_buf->size > max_size) {
    printk(KERN_DEBUG "Invalid \"size\" in the read request\n");
    return -EINVAL;
  }

  /* TODO mutex */
  blob = dev->data[k_buf->index];

  if (blob->isValid) {

#ifdef DEBUG
  printk("User Data: %s\n", blob->data);
#endif

    if(copy_to_user(k_buf->data, blob->data, k_buf->size)) {
      printk("Copy from user\n");
    }

  } else {
    printk("Invalid Index\n");
    /* TODO sleep & wait for data */
  }

  return 0;
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

  printk(KERN_DEBUG "SStore Write\n"); 

  k_buf = kmalloc (sizeof (struct data_buffer), GFP_KERNEL);
  if (!k_buf)
    printk("Bad kmalloc\n");
  
  if(copy_from_user(k_buf, u_buf, sizeof (struct data_buffer))) {
    printk(KERN_DEBUG "Problem copying from user space\n");
  }

#ifdef DEBUG
  printk("Index: %d\n", k_buf->index);
  printk("Size : %d\n", k_buf->size);
#endif

  /* check if index value is valid */
  if (k_buf->index < 0 
      || k_buf->index > num_blobs) {
    printk(KERN_INFO "Invalid \"index\" in the write request"); 
    return -EINVAL;
  }

  if (k_buf->size < 0 
     || k_buf->size > max_size) {
    printk(KERN_DEBUG "Invalid \"size\" in the write request.\n");
    return -EINVAL;
  } 

  blob = kmalloc(sizeof (struct blob), GFP_KERNEL);
  if (blob) {
    blob->data = kmalloc(k_buf->size, GFP_KERNEL);
    if (!blob->data) {
      printk("Bad kmalloc\n");
      return -ENOMEM;
    }

    if(copy_from_user(blob->data, k_buf->data, k_buf->size)) {
      printk(KERN_DEBUG "Problem copying from user space\n");
    }

    /* blob->data = k_buf->data; */
    blob->isValid = 1;		/* valid data */

    /* TODO mutex */
    dev->data[k_buf->index] = blob;

    bytes_written = k_buf->size;

#ifdef DEBUG
  printk("User Data: %s\n", blob->data);
#endif
  }

  return bytes_written;
}
/*
 * Seek 
 */
static loff_t
sstore_llseek(struct file *file, loff_t offset,
            int orig)
{
  return 0;
}

/*
 * Ioctls 
 */
static int
sstore_ioctl(struct inode *inode, struct file *file,
           unsigned int cmd, unsigned long arg)
{
  int retval = 0;
  
  /* extract the type and make sure we have correct cmd */
  if (_IOC_TYPE(cmd) != SSTORE_IOC_MAGIC) return -ENOTTY;
  /* TODO any extra checks for the cmd? */

  switch (cmd) {
    case SSTORE_IOCREMOVE:
      printk(KERN_DEBUG "sstore: Remove blob");
      break;
    default:
      return -ENOTTY;

  }

  return retval;
}


module_init(sstore_init);
module_exit(sstore_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
