#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "intrinsic.h"
#include "syscall.h"
#include <sys/types.h>

// https://olive-su.tistory.com/443?category=1079326

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

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
	
	lock_init(&filesys_lock); // for reading and writing
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	struct thread *curr = thread_current();
	memcpy(curr->tf, f, sizeof(struct intr_frame));
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(curr->exit_flag);
		break;
	case SYS_FORK:
		fork(curr, f);
		break;
	case SYS_WAIT:
		wait()
		break;
	default:
		thread_exit();
	}
}

/* to make sure the address is pointing the User area and make sure the allocated page does exist */
void check_address(void *addr)
{
	struct thread *curr = thread_current();
	if (is_kernel_vaddr(addr) || pml4_get_page(curr->pml4, addr) == NULL) exit(-1);
}

void halt()
{
	power_off();
	thread_exit();
}

void exit(int status)
{
	/* return status to the kernel if the user process's parent was waiting.
	 * the status 0 means success
	 *
	 * 1. 부모에게 알릴 내 익싯 상태 값 스레드 구조체에 넣고 초기화
	 * 2. 상태값 현재 스레드의 exit_flag에 업데이트
	 * 3. thread_exit */

	struct thread *curr = thread_current();
	curr->exit_status = status;
	curr->exit_flag = true;
	thread_exit();
}

pid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

int wait(tid_t pid)
{
	process_wait(pid);
}

int exec (const char *cmd_line)

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}




