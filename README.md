# Lab: page tables

https://pdos.csail.mit.edu/6.S081/2020/labs/pgtbl.html

<br>

做这次实验需要对 xv6 的页表机制和代码有**深刻**的理解，应先阅读 xv6 book 的第三章，见[笔记](Note- xv6 book Chapter 3.md)。

<br>

## Print a page table

任务：定义一个函数 vmprint()，它接受一个 pagetable_t 参数，按格式打印出该页表。在 exec.c 返回之前，如果当前进程是 1 号进程，则调用 vmprint() 打印其页表。

根据提示，我们直接照抄 freewalk 的代码结构，用 kernel/riscv.h 最后定义的那几个宏即可。



## A kernel page table per process

