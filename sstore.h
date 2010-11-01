#ifndef SSTORE_H
#define SSTORE_H

#include <linux/ioctl.h>

#define SSTORE_IOC_MAGIC 'v'

/* Remove blob from the sstore */
#define SSTORE_IOCREMOVE _IOW(SSTORE_IOC_MAGIC, 1, int)


#endif
