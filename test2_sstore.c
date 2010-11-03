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



int main() {

  
  test_write(3, 25, "11111222223333344444data\0", 0);

  test_write(-1, 25, "11111222223333344444data\0", 0);

  test_write(10, 25, "11111222223333344444data\0", 1);

  test_write(3, 0, "11111222223333344444data\0", 1);
  test_write(3, 1000, "11111222223333344444data\0", 1);
 



}
