# Oringinal xv6 README

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)). See also https://pdos.csail.mit.edu/6.828/, which
provides pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Silas Boyd-Wickizer, Anton Burtsev, Dan Cross, Cody Cutler, Mike CAT,
Tej Chajed, Asami Doi, eyalz800, , Nelson Elhage, Saar Ettinger, Alice
Ferrazzi, Nathaniel Filardo, Peter Froehlich, Yakir Goaron,Shivam
Handa, Bryan Henry, jaichenhengjie, Jim Huang, Alexander Kapshuk,
Anders Kaseorg, kehao95, Wolfgang Keller, Jonathan Kimmitt, Eddie
Kohler, Austin Liew, Imbar Marinescu, Yandong Mao, Matan Shabtay,
Hitoshi Mitake, Carmi Merimovich, Mark Morrissey, mtasm, Joel Nider,
Greg Price, Ayan Shafqat, Eldar Sehayek, Yongming Shen, Fumiya
Shigemitsu, Takahiro, Cam Tenny, tyfkda, Rafael Ubal, Warren Toomey,
Stephen Tu, Pablo Ventura, Xi Wang, Keiichi Watanabe, Nicolas
Wolovick, wxdao, Grant Wu, Jindong Zhang, Icenowy Zheng, and Zou Chang
Wei.

The code in the files that constitute xv6 is
Copyright 2006-2020 Frans Kaashoek, Robert Morris, and Russ Cox.

ERROR REPORTS

Please send errors and suggestions to Frans Kaashoek and Robert Morris
(kaashoek,rtm@mit.edu). The main purpose of xv6 is as a teaching
operating system for MIT's 6.S081, so we are more interested in
simplifications and clarifications than new features.

BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu. Once they are installed, and in your shell
search path, you can run "make qemu".


# lab1 README

本实验包含使用xv6操作系统的基础环境搭建与启动。学生需要使用xv6系统调用在编写用户空间内的程序。

主要涉及的内容如下：

## 环境搭建与xv6启动

需要首先启动xv6操作系统，具体步骤请参考[启动xv6](./notes/lab1/1%20启动xv6.md)。

你需要完成：

1. 使用WSL搭建xv6环境，安装工具链
2. 使用MIT官方提供的QEMU镜像，解决QEMU版本兼容性问题，成功启动xv6操作系统
3. 学会退出QEMU模拟器（Ctrl-A X）
4. 学会在xv6中使用基本命令，如ls、cat、echo等

## 1 sleep

本实验要求你实现xv6中的sleep系统调用。具体请参考[sleep](./notes/lab1/2%20sleep.md)。

你需要完成：

1. 理解xv6系统调用的实现流程
2. 学习xv6的进程调度机制
3. 学习xv6中进程的创建与销毁
4. 使用封装过的sleep系统调用
5. 编写用户空间程序，测试sleep系统调用的正确性
6. 大致了解Makefile的工作原理，添加新编写的sleep用户程序到Makefile中
7. 学会使用GDB调试xv6程序

## 2 pingpong

本实验要求你了解并利用管道pipe实现xv6中的进程间通信（IPC）。具体请参考[pingpong](./notes/lab1/3%20pingpong.md)。

你需要完成：

1. 理解xv6中管道的实现原理
2. 学会使用pipe系统调用创建管道
3. 理解当fork时管道文件描述符的继承机制
4. 学会使用read和write系统调用进行管道通信
5. 编写用户空间程序，实现pingpong进程间通信
6. 在完成这一切后，对IPC有一个初步的了解

## 3 primes

本实验要求你使用管道和进程创建实现一个简单的素数筛选器（埃拉托斯特尼筛法），借助此理解进程间通信与并发。具体请参考[primes](./notes/lab1/4%20primes.md)。

你需要完成：

1. 理解素数筛选器的工作原理（阅读实验页给出的论文，或者直接让AI告诉你原理）
2. 学会使用管道和进程创建实现并发程序，这要求学生深入理解管道的工作机制
3. 借助简单的递归思想，实现一个多级的素数筛选器
4. 学习wait系统调用，理解进程的等待与回收，体悟父进程等待子进程退出的过程
5. 深入理解C语言中变量的作用域与生命周期
6. 通过这个实验，进一步加深对进程间通信与并发的理解

## 4 find

本实验要求学生一定程度了解xv6的文件系统，实现文件目录的遍历，并实现一个简单的find命令。具体请参考[find](./notes/lab1/5%20find.md)。

你需要完成：
1. 理解xv6文件系统的基本结构与工作原理
2. 学习xv6中目录与文件的表示方法
3. 学会使用xv6的文件系统调用，如open、read、write、close等
4. 阅读`ls.c`源码，理解如何进行目录遍历
5. 对xv6的用户库`ulib.c`实现有一定了解，例如memmove函数的实现
6. 编写用户空间程序，实现一个简单的find命令，能够在xv6文件系统中查找指定文件
7. 通过这个实验，进一步加深对xv6文件系统的理解

## 5 xargs

本实验要求学生实现xv6中的xargs命令，理解命令行参数的传递与处理，并对字符串进行操作。具体请参考[xargs](./notes/lab1/6%20xargs.md)。

你需要完成：
1. 理解xargs命令的功能与用途
2. 学习xv6中命令行参数的传递机制
3. 实现读取单行的输入，并将其拆分为多个参数
4. 深入理解字符串指针数组的使用，理解变量的声明周期
5. 在处理字符串时，可以使用FSM（有限状态机）的方法来简化逻辑。
6. 进一步体会内核编写对于边界条件的严格处理
7. 编写用户空间程序，实现xargs命令，能够将输入的参数传递给指定的命令执行
8. xv6的echo命令实现的很简略，当你需要获得多行数据用于测试时，最好使用ls，find等命令来生成输入

## 6

希望这个实验能激发你对操作系统的兴趣，理解操作系统的基本概念与实现方法。祝你实验顺利！

JUST FOR FUN!
