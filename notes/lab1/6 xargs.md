这绝对是耗时最长的一个实验，重点在字符串处理的rdline函数中，该函数从标准读端读入一行并按空格拆分为参数附加在xargs命令之后。

```c
#include "kernel/param.h"

#include "kernel/types.h"

#include "user.h"

  

int rdline(char *argn[])

{

  // when you define a char *argn[],

  // you only space for storing character pointers is allocated,

  // no space is allocated for the individual strings themselves.

  // so we need set a static buffer.

  static char buf[512];

  char *p = buf, *head = buf;

  int size = 0;

  while (read(0, p, sizeof(char)) > 0)

  {

    // length ensures the safety of buf

    if (p >= buf + 511)

    {

      fprintf(2, "xargs: lines too long\n");

      break;

    }

  

    if (size >= MAXARG - 1)

    {

      fprintf(2, "xargs: too many arguments\n");

      break;

    }

  

    if (*p == '\n')

    {

      *p = 0;

      if (p > head) // only add parameters if you actually have content

        argn[size++] = head;

      argn[size] = 0;

      return 1;

    }

  

    if (*p == ' ')

    {

      if (p == head) // if it's currently a continuous space (p==head), continue

        continue;

      *p++ = 0;            // seal current word

      argn[size++] = head; // save the param

      head = p;            // head points to the new location

      continue;            // skip the p++ below

    }

    p++;

  }

  

  // handle EOF (end of file without newline)

  if (size > 0 || p > head)

  {

    if (p > head)

    {

      *p = 0;

      argn[size++] = head;

    }

    argn[size] = 0;

    // returns 1 only if the parameter is actually read,

    // otherwise (empty file) returns 0

    return size > 0;

  }

  return 0;

}

  

void concat(char *argv[], char *argn[], char *new_args[], int argc)

{

  // copy the fixed parameters after xargs

  // note: start copying from argv[1] (skipping "xargs" itself)

  for (int i = 1; i < argc; i++)

  {

    new_args[i - 1] = argv[i];

  }

  

  // copy parameters read from stdin

  int j = 0, k = argc - 1;

  while (argn[j] != 0)

  {

    if (k >= MAXARG - 1)

    {

      fprintf(2, "xargs: too many arguments\n");

      exit(2);

    }

    new_args[k++] = argn[j++];

  }

  new_args[k] = 0;

}

  

int main(int argc, char *argv[])

{

  char *argn[MAXARG];

  while (rdline(argn))

  {

    if (fork() == 0)

    {

      char *new_args[MAXARG];

      concat(argv, argn, new_args, argc);

      exec(argv[1], new_args);

      exit(0);

    }

    else

    {

      wait(0);

    }

  }

  exit(0);

}
```

rdline(): 从标准输入中读入一行并进行参数拆分
concat(): 拼接原有命令行参数和新命令行参数

## 1. rdline()：对C字符串操作和生命周期的完整实践

```c
int rdline(char *argn[])

{

  // when you define a char *argn[],

  // you only space for storing character pointers is allocated,

  // no space is allocated for the individual strings themselves.

  // so we need set a static buffer.

  static char buf[512];

  char *p = buf, *head = buf;

  int size = 0;

  while (read(0, p, sizeof(char)) > 0)

  {

    // length ensures the safety of buf

    if (p >= buf + 511)

    {

      fprintf(2, "xargs: lines too long\n");

      break;

    }

  

    if (size >= MAXARG - 1)

    {

      fprintf(2, "xargs: too many arguments\n");

      break;

    }

  

    if (*p == '\n')

    {

      *p = 0;

      if (p > head) // only add parameters if you actually have content

        argn[size++] = head;

      argn[size] = 0;

      return 1;

    }

  

    if (*p == ' ')

    {

      if (p == head) // if it's currently a continuous space (p==head), continue

        continue;

      *p++ = 0;            // seal current word

      argn[size++] = head; // save the param

      head = p;            // head points to the new location

      continue;            // skip the p++ below

    }

    p++;

  }
```

三个要点：

1. buf缓冲区必须声明为**静态**，否则无法在主函数中读取这段内存，我们在主函数中声明的argn只是一个指针数组，没有开辟对应字符串的空间，所以需要利用buf开辟空间。
2. 作为底层代码，必须严格检查边界，包括但不限于数组溢出、段错误。
3. 使用head标记字符头，p作为flag填充buf，注意**静态数组的数组名不能做算数运算**，它始终保持在开头，不能像我们声明的p一样移动。数组名和指针还是有区别的。

## 2. concat()：对参数进行拼接

 ```c
 void concat(char *argv[], char *argn[], char *new_args[], int argc)

{

  // copy the fixed parameters after xargs

  // note: start copying from argv[1] (skipping "xargs" itself)

  for (int i = 1; i < argc; i++)

  {

    new_args[i - 1] = argv[i];

  }

  

  // copy parameters read from stdin

  int j = 0, k = argc - 1;

  while (argn[j] != 0)

  {

    if (k >= MAXARG - 1)

    {

      fprintf(2, "xargs: too many arguments\n");

      exit(2);

    }

    new_args[k++] = argn[j++];

  }

  new_args[k] = 0;

}
 ```

要点就是注意检测参数大小是否溢出。