#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/init.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/process.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

struct lock filesys_lock;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	default:
		exit(-1);
		break;
	}

	// printf("system call!\n");
	// thread_exit();
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	unsigned char *buf = buffer;
	int read_size;
	struct thread *curr = thread_current();

	if (fd == 1)
	{
		return -1;
	}
	else if (fd == 0)
	{
		char key;
		for (read_size = 0; read_size < size; read_size++)
		{
			key = input_getc();
			*buf++ = key;
			if (key == "\n")
			{
				break;
			}
		}
	}
	else
	{
		struct file *file = process_get_file(fd);
		if (file == NULL)
		{
			return -1;
		}
		lock_acquire(&filesys_lock);
		read_size = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return read_size;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int write_size;

	if (fd == 0)
	{
		return -1;
	}
	else if (fd == 1)
	{
		putbuf(buffer, size);
		write_size = size;
	}
	else
	{
		struct file *file = process_get_file(fd);
		if (file == NULL)
		{
			return -1;
		}
		lock_acquire(&filesys_lock);
		write_size = file_write(file, buffer, size);
		lock_release(&filesys_lock);
	}
}

void check_address(void *addr)
{
	struct thread *curr = thread_current();
	if (is_kernel_vaddr(addr) || pml4_get_page(curr->pml4, addr) == NULL)
	{
		exit(-1);
	}
}
