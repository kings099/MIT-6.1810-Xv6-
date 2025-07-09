#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int p_to_c[2]; // Pipe for parent to child
    int c_to_p[2]; // Pipe for child to parent
    char buf[1];   // Buffer to hold the byte

    // 1. Create two pipes
    pipe(p_to_c);
    pipe(c_to_p);

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    } else if (pid == 0) {
        // 子进程
        close(p_to_c[1]); // Child doesn't write to parent-to-child pipe
        close(c_to_p[0]); // Child doesn't read from child-to-parent pipe

        if (read(p_to_c[0], buf, 1) == 1) {
            printf("%d: received ping\n", getpid());
        }

        write(c_to_p[1], buf, 1);

        close(p_to_c[0]);
        close(c_to_p[1]);
        exit(0);

    } else {
        // 父进程
        // Close unused pipe ends
        close(p_to_c[0]); // Parent doesn't read from parent-to-child pipe
        close(c_to_p[1]); // Parent doesn't write to child-to-parent pipe

        write(p_to_c[1], "x", 1); // Send a byte

        if (read(c_to_p[0], buf, 1) == 1) {
            printf("%d: received pong\n", getpid());
        }

        close(p_to_c[1]);
        close(c_to_p[0]);
        exit(0);
    }
}