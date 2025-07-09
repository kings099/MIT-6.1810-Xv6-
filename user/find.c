// user/find.c

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

/**
 * @brief 递归查找函数
 * @param path 当前要查找的目录路径
 * @param filename 要查找的目标文件名
 */
void find(char *path, char *filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 1. 打开当前路径对应的目录文件
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 2. 通过文件描述符获取文件状态
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    // 3. 检查打开的是否为目录
    if (st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }

    // 4. 构造路径字符串，为遍历做准备
    // 保证路径长度不会溢出
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        close(fd);
        return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/'; // 在路径末尾加上斜杠

    // 5. 循环读取目录中的条目
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        // dirent中的inum为0表示该条目无效
        if (de.inum == 0) {
            continue;
        }

        // 跳过 "." 和 ".."，防止无限递归
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
            continue;
        }

        // 将当前条目名追加到路径末尾，形成完整路径
        strcpy(p, de.name);

        // 获取这个完整路径的状态
        if (stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }

        // 6. 根据文件类型进行处理
        switch (st.type) {
        case T_FILE:
            // 如果是文件，则比较文件名是否与目标文件名相同
            if (strcmp(de.name, filename) == 0) {
                printf("%s\n", buf);
            }
            break;

        case T_DIR:
            // 如果是目录，则递归调用 find
            find(buf, filename);
            break;
        }
    }

    // 7. 关闭文件描述符，释放资源
    close(fd);
}


int main(int argc, char *argv[]) {
    // 检查命令行参数
    if (argc < 3) {
        fprintf(2, "Usage: find <directory> <filename>\n");
        exit(1);
    }
    
    // 启动查找
    find(argv[1], argv[2]);

    exit(0);
}