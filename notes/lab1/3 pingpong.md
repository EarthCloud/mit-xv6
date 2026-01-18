```c
#include "user.h"

  

int main()

{

    // p1: parent writes, child reads;

    // p2: child writes, parent reads.

    int p1[2], p2[2];

    pipe(p1);

    pipe(p2);

    if (fork() == 0)

    {

        // if it's child process

        // first close unnecessary pipe ends

        close(p1[1]);

        close(p2[0]);

  

        // read from p1

        char a[10];

        read(p1[0], a, sizeof(a));

        close(p1[0]);

        fprintf(1, "%d: received ping\n", getpid());

  

        // write to parent process

        write(p2[1], "hina", 4);

        close(p2[1]);

        exit(0);

    }

    else

    {

        // first close unnecessary pipe ends

        close(p1[0]);

        close(p2[1]);

  

        // write to child process

        write(p1[1], "youmiya", 7);

        close(p1[1]);

  

        // blocked to wait for reading

        char a[10];

        read(p2[0], a, sizeof(a));

        fprintf(1, "%d: received pong\n", getpid());

        close(p2[0]);

        exit(0);

    }

}
```

注释都在上面了。写入一个字节的意思是随便写点什么让管道中有东西（否则一直阻塞等待）。刚开始的时候我只开了一个管道，导致父进程在写入后会直接读取自己的内容，没有阻塞等待子进程写入，这是一个逻辑上的错误。后来开了两个管道，才完成了任务。为了防止进程阻塞，请及时不需要和使用过的关闭读端和写端。这个例子演示了进程间是如何通过管道通讯的。很有启发性。

然后注意一下题目描述，输出是不需要加尖括号的，它表示占位符，有猪。

仍然是复习一下下面的这几个函数引用：

### 1. read

```c
int read(int fd, void *buf, int n)
```

从文件描述符fd中读取最多n字节的内容到buf中。

### 2. write

```c
int write(int fd, const void *buf, int n)
```

从buf中读取n字节到文件描述符fd中。

0为标准读取，1为标准写入，2为标准错误。

### 3. fprintf

```c
void fprintf(int fd, const char *format, ...)
```

向文件描述符fd中写入字符串，其余参数用法类似于printf。