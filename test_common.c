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

void test_write(int index, int size, void *data, char need_close) {

  int written = 0;
  struct data_buffer buf;

  sstore_dev = open("/dev/sstore0", O_RDWR, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");
  buf.index = index;
  buf.size = size;
  buf.data = data;
  
  printf("write to sstore0 index:%i size:%i ..\n", index, size); 
  written = write(sstore_dev, &buf, sizeof (struct data_buffer));
  if (written < 0)
    perror("write");

  if (need_close)
    close(sstore_dev);

}

/* test read() operation */

void test_read(int index, int size, void* data, char need_close) {

  struct data_buffer buf;

  sstore_dev = open("/dev/sstore0", O_RDONLY, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");

  buf.index = index;
  buf.size = size;
  buf.data = data;
  printf("read from sstore0 index:%i size:%i ..\n", index, size); 
  read(sstore_dev, &buf, sizeof (struct data_buffer));

  if (need_close)
    close(sstore_dev);
}

/* test delete ioctl */

void test_del(int index, char need_close) {
  sstore_dev = open("/dev/sstore0", O_RDWR, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");


  printf("delete from sstore0 index:%i..\n", index); 
  ioctl(sstore_dev, SSTORE_IOCREMOVE, &index);

  if (need_close)
   close(sstore_dev);

}


