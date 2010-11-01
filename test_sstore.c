/*
 * Abdelhalim Ragab
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

struct data_buffer {
  int index;
  int size;
  char *data;
};

int sstore_dev = -1;

int main() {

  struct data_buffer buf;
  int written = 0;
  unsigned int index = 3;
  

  printf("Opening sstore0 ..\n"); 
  sstore_dev = open("/dev/sstore0", O_RDWR, S_IRWXU);
  if (sstore_dev < 0)
	perror("opening sstore0");

  buf.index = index;
  buf.size = 25;
  buf.data = (char *) malloc(25 * sizeof (char));
  if (!buf.data)
     printf("Error allocating memory\n");
  
  strncpy(buf.data, "11112222333344445555data\0", 25);
  perror("strncpy");
  written = write(sstore_dev, &buf, sizeof (struct data_buffer));
  if (written < 0)
    perror("write");

  free(buf.data);
  buf.data = NULL;

  /* Now testing the read */
  printf("Reading Data... \n");
  buf.data = (char *) malloc(25 * sizeof (char));
  read(sstore_dev, &buf, sizeof (struct data_buffer));
  printf("Data: %s\n", buf.data);


  ioctl(sstore_dev, SSTORE_IOCREMOVE, &index);

  printf("Reading Data... \n");
  buf.data = (char *) malloc(25 * sizeof (char));
  read(sstore_dev, &buf, sizeof (struct data_buffer));
  printf("Data: %s\n", buf.data);
  
  /*close(sstore_dev); */
  

}
