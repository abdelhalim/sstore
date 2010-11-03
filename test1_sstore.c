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




int main() {

  unsigned int index = 3;
  void* data;
  

 test_write(3, 25, "11111222223333344444data\0", 0);

 /* write same data to different index */
 test_write(4, 25, "11111222223333344444data\0", 0);


  /* Now testing the read */
  
  data = (char *) malloc(25 * sizeof (char));

  test_read(3, 25, data, 0);
  printf("Data: %s\n", data);

  printf("Delete blob... \n");
  test_del(3, 0);

  test_read(3, 25, data, 0);
  printf("Data: %s\n", data);
  
  free(data);
  

}
