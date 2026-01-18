```c
#include "kernel/stat.h"

#include "kernel/types.h"

#include "kernel/fs.h"

#include "user.h"

  

char *fmtname(char *path)

{

  char *p;

  // Find first character after last slash.

  for (p = path + strlen(path); p >= path && *p != '/'; p--)

    ;

  p++;

  return p;

}

  

void find(char *path, char *target)

{

  char buf[512], *p;

  int fd;

  struct stat st;

  struct dirent de;

  

  if ((fd = open(path, 0)) < 0)

  {

    fprintf(2, "find: cannot open %s\n", path);

    return;

  }

  

  if (fstat(fd, &st) < 0)

  {

    fprintf(2, "find:cannot stat %s\n", path);

    // never allowed leak fd

    close(fd);

    return;

  }

  

  switch (st.type)

  {

  case T_FILE:

    if (!strcmp(fmtname(path), target))

      fprintf(1, "%s\n", path);

    break;

  

  case T_DIR:

    // if the whole length(path + '/' + DIRSIZ + '\0') is larger than buf, break;

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)

    {

      printf("find: path too long\n");

      break;

    }

    strcpy(buf, path);

    p = buf + strlen(buf);

    *p++ = '/';

    while (read(fd, &de, sizeof(de)) == sizeof(de))

    {

      if (de.inum == 0)

        continue;

      if ((!strcmp(de.name, ".")) || (!strcmp(de.name, "..")))

        continue;

      memmove(p, de.name, DIRSIZ);

      p[DIRSIZ] = 0;

      find(buf, target);

    }

    break;

  }

  close(fd);

}

  

int main(int argc, char *argv[])

{

  if (argc < 3)

  {

    fprintf(2, "usage: find <path> <filename>\n");

    exit(1);

  }

  find(argv[1], argv[2]);

  exit(0);

}
```

基本就是对ls的模仿
## 1. 文件状态结构体

```c
#define T_DIR     1   // Directory

#define T_FILE    2   // File

#define T_DEVICE  3   // Device

  

struct stat {

  int dev;     // File system's disk device

  uint ino;    // Inode number

  short type;  // Type of file

  short nlink; // Number of links to file

  uint64 size; // Size of file in bytes

};
```