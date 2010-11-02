/*
 * sstore.h
 *
 * Copyright (C) 2010 Abdelhalim Ragab <abdelhalim@r8t.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#ifndef SSTORE_H
#define SSTORE_H

#include <linux/ioctl.h>

/* IOCTL operations */
#define SSTORE_IOC_MAGIC 'v'

/* Remove blob from the sstore */
#define SSTORE_IOCREMOVE _IOW(SSTORE_IOC_MAGIC, 1, int)

/* data structure used for read/write operations */
struct data_buffer {
    int index;      /* index into the blob list */
    int size;       /* size of the data transfer */
    char *data;     /* where the data being transfered resides */
};

#endif
