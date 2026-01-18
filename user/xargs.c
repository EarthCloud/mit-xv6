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
      *p++ = 0;            // seal current word
      argn[size++] = head; // save the param
      head = p;            // head points to the new location
      continue;            // skip the p++ below
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
