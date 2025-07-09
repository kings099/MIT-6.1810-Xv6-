// user/primes.c (使用 dup 替代 dup2 的最终版本)

#include "kernel/types.h"
#include "user/user.h"

void sieve()__attribute__((noreturn));

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        // --- 子进程 ---
        close(p[1]); // 关闭管道的写入端
        
        // 使用 close 和 dup 替代 dup2
        close(0);    // 关闭标准输入
        dup(p[0]);   // 将管道的读取端复制到标准输入 (文件描述符 0)
        
        close(p[0]); // 标准输入已经指向了管道，原来的管道文件描述符可以关闭
        sieve();     // 启动筛选过程
    } else {
        // --- 父进程 (main) ---
        close(p[0]); // 关闭管道的读取端
        for (int i = 2; i <= 280; i++) {
            if (write(p[1], &i, sizeof(int)) != sizeof(int)) {
                fprintf(2, "primes: main write error\n");
                exit(1);
            }
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}

void sieve() {
    int prime, num;

    if (read(0, &prime, sizeof(int)) == 0) {
        exit(0); 
    }
    printf("prime %d\n", prime);

    int p_right[2];
    if (pipe(p_right) < 0) {
        exit(0);
    }

    if (fork() == 0) {
        // --- 子进程 (下一个筛选阶段) ---
        close(p_right[1]);    // 关闭新管道的写入端
        
        // **【修改处】** 使用 close 和 dup 替代 dup2
        close(0);             // 关闭当前的标准输入 (它继承自父进程的管道)
        dup(p_right[0]);      // 将新管道的读取端复制到标准输入
        
        close(p_right[0]);    // 关闭原来的文件描述符
        sieve();              // 递归
    } else {
        // --- 父进程 (当前筛选阶段) ---
        close(p_right[0]); // 关闭新管道的读取端

        while (read(0, &num, sizeof(int)) > 0) {
            if (num % prime != 0) {
                if (write(p_right[1], &num, sizeof(int)) != sizeof(int)) {
                    fprintf(2, "primes: sieve write error\n");
                    exit(1);
                }
            }
        }
        close(p_right[1]);
        wait(0);
    }
    exit(0);
}