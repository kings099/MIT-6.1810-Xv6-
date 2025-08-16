#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  printf("Testing symlink system call...\n");
  
  int fd = open("testfile", O_CREATE | O_WRONLY);
  if(fd < 0) {
    printf("Failed to create testfile\n");
    exit(1);
  }
  write(fd, "hello", 5);
  close(fd);
  
  printf("Created testfile\n");
  
  int result = symlink("testfile", "testlink");
  if(result < 0) {
    printf("symlink failed\n");
    exit(1);
  }
  
  printf("Created symlink successfully\n");
  
  fd = open("testlink", O_RDONLY);
  if(fd < 0) {
    printf("Failed to open testlink\n");
    exit(1);
  }
  
  char buf[10];
  int n = read(fd, buf, 5);
  buf[n] = '\0';
  printf("Read from testlink: %s\n", buf);
  close(fd);
  
  printf("Test passed!\n");
  exit(0);
}
