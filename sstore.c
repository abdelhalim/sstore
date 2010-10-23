#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/moduleparam.h>


static int num_blobs = 1;
static int blob_size = 1;
module_param(num_blobs, int, S_IRUGO);
module_param(blob_size, int, S_IRUGO);


#define NUM_MINOR_DEVICES          2


struct blob {
  char *data;
  int  index;
};

/* Per-device structure */
struct sstore_dev {
  struct blob *data;
  unsigned short current_pointer; /* Current pointer */
  unsigned int size;              /* Size */
  int store_number;               /* store number */
  char name[10];		  /* Name */
  struct cdev cdev;               /* The cdev structure */
  /* ... */                       /* Mutexes, spinlocks, wait
                                     queues, .. */
} *sstore_devp[NUM_MINOR_DEVICES];



int sstore_open(struct inode *inode, struct file *file);
int sstore_release(struct inode *inode, struct file *file);
ssize_t sstore_read(struct file *file, char *buf, size_t count, loff_t *ppos);
ssize_t sstore_write(struct file *file, const char *buf,
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

  // Only root is allowed
  if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

  printk(KERN_DEBUG "SStore device opened\n"); 

  dev = container_of(inode->i_cdev, struct sstore_dev, cdev);
  file->private_data = dev; /* to be used by other methods */

  // Do we need trim function

  return 0;
}

/*
 * Release sstore
 */
int
sstore_release(struct inode *inode, struct file *file)
{
  return 0;
}

/*
 * Read from a sstore at given index
 * If the read index is past end-of-store then block until
 * a new record is written at its index.
 */
ssize_t
sstore_read(struct file *file, char *buf,
          size_t count, loff_t *ppos)
{
  struct sstore_dev *dev = file->private_data;
  
  return 0;
}

/*
 * Write to a sstore at a given index
 */
ssize_t
sstore_write(struct file *file, const char *buf,
           size_t count, loff_t *ppos)
{
  struct sstore_dev *dev = file->private_data;

  return 0;
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
  return 0;
}


module_init(sstore_init);
module_exit(sstore_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
