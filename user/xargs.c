// user/xargs.c

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAX_LINE_LEN 512

int main(int argc, char *argv[]) {
    char *command;
    char *child_argv[MAXARG]; // 用于 exec 的参数列表
    char line_buf[MAX_LINE_LEN]; // 存储从标准输入读取的一行
    char ch;
    int char_idx = 0; // 当前行缓冲区的索引
    int arg_idx = 0;  // child_argv 的索引

    // 1. 检查参数数量
    if (argc < 2) {
        fprintf(2, "Usage: xargs <command> [args...]\n");
        exit(1);
    }

    // 2. 准备 exec 所需的参数列表
    // a. 将 xargs 的参数（除了第一个"xargs"本身）拷贝到 child_argv
    command = argv[1]; // 要执行的命令
    for (arg_idx = 1; arg_idx < argc; arg_idx++) {
        child_argv[arg_idx - 1] = argv[arg_idx];
    }
    // 注意：此时 child_argv 的最后一个参数位置是空的，留给从 stdin 读取的行

    // 3. 循环从标准输入逐字符读取
    while (read(0, &ch, 1) > 0) {
        if (ch == '\n') {
            // 遇到换行符，说明一行读取完毕
            line_buf[char_idx] = '\0'; // 将行缓冲区变为一个合法的 C 字符串

            // a. fork 一个子进程
            if (fork() == 0) {
                // --- 子进程 ---
                // b. 将读取到的行作为最后一个参数
                child_argv[argc - 1] = line_buf;
                // c. exec 的 argv 数组必须以空指针结尾
                child_argv[argc] = 0;

                // d. 执行命令
                exec(command, child_argv);

                // 如果 exec 返回，说明它执行失败了
                fprintf(2, "xargs: exec %s failed\n", command);
                exit(1);
            } else {
                // --- 父进程 ---
                // 等待子进程执行完毕
                wait(0);
            }
            
            // 为读取下一行做准备，重置行缓冲区索引
            char_idx = 0;
        } else {
            // 未遇到换行符，将字符加入行缓冲区
            if (char_idx < MAX_LINE_LEN - 1) {
                line_buf[char_idx++] = ch;
            }
        }
    }

    // 4. 处理最后一行（如果文件不是以换行符结尾）
    if (char_idx > 0) {
        line_buf[char_idx] = '\0';
        if (fork() == 0) {
            child_argv[argc - 1] = line_buf;
            child_argv[argc] = 0;
            exec(command, child_argv);
            fprintf(2, "xargs: exec %s failed\n", command);
            exit(1);
        } else {
            wait(0);
        }
    }

    exit(0);
}