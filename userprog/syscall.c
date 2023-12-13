#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "include/threads/init.h"
#include "include/threads/vaddr.h"
#include "include/filesys/filesys.h"
#include "include/filesys/file.h"
#include "include/devices/input.h"
#include "string.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void syscall_init (void);

// syscall Functions
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
tid_t fork (const char *thread_name, struct intr_frame* f);
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

// util func
// 주소 값이 '유저 영역' 값인지 확인하고 벗어난 영역이라면 프로세스 종료
void check_address(void* addr)
{
	if(addr == NULL ||
	 is_user_vaddr(addr) == false ||
	 pml4_get_page(thread_current()->pml4,addr) == NULL) // addr에 해당하는 물리주소 검색 실패 시
	{
		exit(-1);
	}
}

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	lock_init(&filesys_lock);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// 시스템 콜이 호출된 시점의 인터럽트 프레임을 스택에 push하고,
	// 시스템 핸들러로 제어권을 옮기는 상태이다
	// (by systemcall_entry)
	if (f == NULL)
	{
		printf("Is Wrong Data forwarded!\n");
		thread_exit();
		return;
	}

	struct thread* curr = thread_current();

	check_address((void*)(f->rsp));

	switch (f->R.rax)
	{
	case SYS_HALT:
		{
			halt();
		}
		break;
	case SYS_EXIT:
		{
			int status = (int)(f->R.rdi);
			exit(status);
		}
		break;
	case SYS_FORK:
		{
			char* thread_name = (char*)(f->R.rdi);
			f->R.rax = fork(thread_name,f);
		}
		break;
	case SYS_EXEC:
		{
			char* file_name = (char*)(f->R.rdi);
			f->R.rax = exec(file_name);
		}
		break;
	case SYS_WAIT:
		{
			f->R.rax = wait(f->R.rdi);
		}
		break;
	case SYS_CREATE:
		{
			char *file = (char*)(f->R.rdi);
			unsigned initial_size = (unsigned)(f->R.rsi);
			f->R.rax = create(file,initial_size);
		}
		break;
	case SYS_REMOVE:
		{
			char *file = (char*)(f->R.rdi);
			f->R.rax = remove(file);
		}
		break;
	case SYS_OPEN:
		{
			char* file_name = (char*)(f->R.rdi);
			f->R.rax = open(file_name);
		}
		break;
	case SYS_FILESIZE:
		{
			int fd = (int)(f->R.rdi);
			f->R.rax = filesize(fd);
		}
		break;
	case SYS_READ:
		{
			int fd = (int)(f->R.rdi);
			void* buffer = (void*)(f->R.rsi);
			unsigned size = (unsigned)(f->R.rdx);
			f->R.rax = read(fd,buffer,size);
		}
		break;
	case SYS_WRITE:
		{
			int fd = (int)(f->R.rdi);
			void* buffer = (void*)(f->R.rsi);
			unsigned size = (unsigned)(f->R.rdx);
			f->R.rax = write(fd,buffer,size);
		}
		break;
	case SYS_SEEK:
		{
			int fd = (int)(f->R.rdi);
			unsigned position = (unsigned)(f->R.rsi);
			seek(fd,position);
		}
		break;
	case SYS_TELL:
		{
			int fd = (int)(f->R.rdi);
			f->R.rax = tell(fd);
		}
		break;
	case SYS_CLOSE:
		{
			int fd = (int)(f->R.rdi);
			close(fd);
		}
		break;

	default:
		{
			printf("Is Wrong Rax Data forwarded!\n");
			thread_exit();
			return;
		}
		break;
	}
}

void halt (void)
{
	power_off();
}

int exec (const char *file)
{
	check_address(file);
	size_t fileNameLen = strlen(file);

	char* copy_fn = palloc_get_page(PAL_ZERO);
	if(copy_fn == NULL)
	{
		exit(-1);
	}

	strlcpy(copy_fn,file,fileNameLen+1);

	if(process_exec(copy_fn) == -1)
	{
		return -1;
	}
	
	NOT_REACHED();
	return 0;
}

int wait(tid_t pid)
{
	process_wait(pid);
}

void exit (int status)
{
	struct thread *curr = thread_current ();
	curr->exit_Status = status;
	printf ("%s: exit(%d)\n", curr->name,status);
	thread_exit();
}

tid_t fork (const char *thread_name, struct intr_frame* f)
{
	check_address(thread_name);
	return process_fork(thread_name,f);
}

bool create (const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file,initial_size);
}

bool remove (const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open (const char *file)
{
	check_address(file);
	struct file* targetFile = filesys_open(file);

	if(targetFile == NULL)
	{
		return -1;
	}

	int fd = search_nextFD(targetFile);

	if(fd == -1)
	{
		file_close(targetFile);
	}

	return fd;
}

int filesize (int fd)
{
	struct thread* curr = thread_current();

	if(fd >= MAX_FD_VALUE || fd < 0)
	{
		return -1;
	}

	struct file* targetFile = curr->fdt[fd];

	if(targetFile == NULL)
	{
		return -1;
	}

	return file_length(targetFile);
}
int read (int fd, void *buffer, unsigned length)
{
	// 들어와있는 f->rsp 자체는 유효한 주소지만
	// buffer 의 위치와
	// buffer 가 끝나는 부분이 유효한 주소인지 확인한다
	check_address(buffer);
	check_address(buffer + length);

	int reads = 0;

	// read 에서 stdout를 찾고 있다
	if(fd == 1)
	{
		return -1;
	}

	if(fd == 0)
	{
		unsigned char * buf = buffer;
		for(reads = 0; reads < length;reads++)
		{
			char c = input_getc();
			*buf++ = c;
			if(c == '\0')
				break;
		}
	}
	else
	{
		struct thread* curr = thread_current();

		if(fd < 0 || fd >= MAX_FD_VALUE)
		{
			return -1;
		}

		struct file* targetFile = curr->fdt[fd];
		if(targetFile == NULL)
		{
			return -1;
		}

		lock_acquire(&filesys_lock);
		reads = file_read(targetFile,buffer,length);
		lock_release(&filesys_lock);
	}

	return reads;
}
int write (int fd, const void *buffer, unsigned length)
{
	check_address(buffer);
	check_address(buffer + length);

	if(fd == 0)
	{
		return -1;
	}

	int writes = 0;

	if(fd == 1)
	{
		putbuf(buffer,length);
		writes = length;
	}
	else
	{
		struct thread* curr = thread_current();

		if(fd < 0 || fd >= MAX_FD_VALUE)
		{
			return -1;
		}

		struct file* targetFile = curr->fdt[fd];
		if(targetFile == NULL)
		{
			return -1;
		}

		lock_acquire(&filesys_lock);
		writes = file_write(targetFile,buffer,length);
		lock_release(&filesys_lock);
	}

	return writes;
}
void seek (int fd, unsigned position)
{
	struct thread* curr = thread_current();

	if (fd < 2 || fd >= MAX_FD_VALUE)
	{
		return;
	}

	struct file *targetFile = curr->fdt[fd];
	if (targetFile == NULL)
	{
		return;
	}

	file_seek(targetFile,position);
}

unsigned tell (int fd)
{
	struct thread* curr = thread_current();

	if (fd < 2 || fd >= MAX_FD_VALUE)
	{
		return 0;
	}

	struct file *targetFile = curr->fdt[fd];
	if (targetFile == NULL)
	{
		return 0;
	}

	return file_tell(targetFile);
}

void close (int fd)
{
	if (fd < 2 || fd >= MAX_FD_VALUE)
	{
		return;
	}

	struct thread* curr = thread_current();

	struct file *targetFile = curr->fdt[fd];
	if (targetFile == NULL)
	{
		return;
	}

	curr->fdt[fd] = NULL;
	file_close(targetFile);
}
