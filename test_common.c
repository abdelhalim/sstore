/*
 * Copyright 2010 Abdelhalim Ragab (abdelhalim@r8t.org)
 *
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "sstore.h"


int sstore_dev = -1;

/* test write operation */

void test_write(int index, int size, void *data) {

  int written = 0;
  struct data_buffer buf;

  printf("Opening sstore0 ..\n"); 
  sstore_dev = open("/dev/sstore0", O_RDWR, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");
  buf.index = index;
  buf.size = size;
  buf.data = data;
  
  written = write(sstore_dev, &buf, sizeof (struct data_buffer));
  if (written < 0)
    perror("write");

  close(sstore_dev);

}

/* test read() operation */

void test_read(int index, int size, void* data) {

  struct data_buffer buf;

  printf("Opening sstore0 ..\n"); 
  sstore_dev = open("/dev/sstore0", O_RDONLY, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");

  buf.index = index;
  buf.size = size;
  buf.data = data;
  read(sstore_dev, &buf, sizeof (struct data_buffer));

  close(sstore_dev);
}

/* test delete ioctl */

void test_del(int index) {
  printf("Opening sstore0 ..\n"); 
  sstore_dev = open("/dev/sstore0", O_RDWR, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");

  ioctl(sstore_dev, SSTORE_IOCREMOVE, &index);

  close(sstore_dev);

}


