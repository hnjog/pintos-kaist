#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void close (int fd); // for calling close system call on userprog/process.c

struct lock filesys_lock; // lock for occupying a file when read, write

#endif /* userprog/syscall.h */
