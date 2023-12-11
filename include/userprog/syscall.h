#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "include/lib/user/syscall.h"
#include "threads/synch.h"

void syscall_init (void);

// syscall Functions
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

int dup2(int oldfd, int newfd);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
