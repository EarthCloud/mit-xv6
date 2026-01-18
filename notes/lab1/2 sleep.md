一个简单的使用命令行参数的实例：

```c
#include "kernel/types.h"

#include "user.h"

  

int main(int argc, char *argv[])

{

    if (argc < 2){

        fprintf(2, "usage: sleep time\n");

        exit(1);

    }

    sleep(atoi(argv[argc - 1]));

    exit(0);

}
```

其中sleep函数已经在`user.h`中定义过了，只需调用它即可，注意命令行参数是字符串格式，需要用atoi转换为数字，该函数已被定义和实现过，可以直接使用。

## 判题

直接运行`./grade-lab-util`并附带参数即可

```text
earthcloud@C:~/mit-xv6/xv6-labs-2020$ ./grade-lab-util sleep
make: 'kernel/kernel' is up to date.
== Test sleep, no arguments == sleep, no arguments: OK (1.3s) 
== Test sleep, returns == sleep, returns: OK (0.7s) 
== Test sleep, makes syscall == sleep, makes syscall: OK (0.9s) 
```



注意可能会出现以下问题

```text
‘python’: No such file or directory
```

这是因为你的 WSL (Ubuntu) 系统里默认只安装了 **Python 3**（命令是 `python3`），但是 MIT 的打分脚本里写的命令是 `python`（它以为你还有老的 Python 2 或者做了映射）。

简单来说，系统听不懂 `python` 是谁，它只认识 `python3`。

你可以通过以下两种方法的任意一种来解决：

### 方法一：安装自动映射包（最推荐，Ubuntu 标准做法）

Ubuntu 提供了一个专门的包来解决这个问题，它会自动把 `python` 这个命令指向 `python3`。

在终端运行：

Bash

```
sudo apt update
sudo apt install python-is-python3
```

安装完后，再次运行 `./grade-lab-util sleep` 应该就好了。