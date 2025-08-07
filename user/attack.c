#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // 1. 使用 sbrk() 获取可预测的、从页面起始位置开始的内存指针。
  if(argc != 1){
    printf("Usage: secret the-secret\n");
    exit(1);
  }
  char *end = sbrk(PGSIZE*32);
  end = end + 8 * PGSIZE;
  fprintf(2, end+16, 8);
  exit(1);
}
