#include "userprog/syscall.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "lib/stdio.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "intrinsic.h"
typedef int pid_t;


void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
static int fdt_add_fd(struct file *f);
struct file *fdt_get_file(int fd);
static void fdt_remove_fd(int fd);
void halt(void);
void exit(int status);
pid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *cmd_line);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

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
	check_address(f->rsp);
	struct thread *curr = thread_current();
	memcpy(&curr->tf, f, sizeof(struct intr_frame));
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		// f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		// f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		// f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		// f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE: /* Delete a file. */
		// f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN: /* Open a file. */
		// f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE: /* Obtain a file's size. */
		// f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ: /* Read from a file. */
		// f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE: /* Write to a file. */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK: /* Change position in a file. */
		// seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL: /* Report current position in a file. */
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE: /* Close a file. */
		// close(f->R.rdi);
		break;
	default:
		thread_exit();
	}
}

/* to make sure the address is pointing the User area and make sure the allocated page does exist */
void check_address(void *addr)
{
	struct thread *curr = thread_current();
	if (!is_kernel_vaddr(addr))
	{
		exit(-1);
	}
}

/* fd table에 인자로 들어온 파일 객체를 저장하고 fd를 생성한다 */
static int fdt_add_fd(struct file *f)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;

	while (curr->next_fd < FDCOUNT_LIMIT && fdt[curr->next_fd])
	{
		curr->next_fd++;
	}

	if (curr->next_fd > FDCOUNT_LIMIT)
	{
		return -1; // when fdt is full
	}
	fdt[curr->next_fd] = f; // fdt
	return curr->next_fd;
}

/* fd table에서 인자로 들어온 fd를 검색하여 찾은 파일 객체를 리턴한다 */
struct file *fdt_get_file(int fd) // 여기 static 밑줄가서 지움
{
	struct thread *curr = thread_current();
	if (fd < STDIN_FILENO || fd > FDCOUNT_LIMIT) // 실패
	{
		return NULL;
	}
	return curr->fdt[fd]; // 성공
}

/* fd table에서 인자로 들어온 fd를 제거한다 */
static void fdt_remove_fd(int fd)
{
	struct thread *curr = thread_current();

	if (fd < STDIN_FILENO || fd > FDCOUNT_LIMIT)
		return; // 실패

	curr->fdt[fd] = NULL; // 성공시 제거
}

void halt(void)
{
	power_off();
	// thread_exit();
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

int wait(tid_t tid)
{
	return process_wait(tid);
}

// 현재 실행중인 프로세스를 cmd_line에 지정된 실행파일로 변경하고 인수들을 전달한다.
int exec(const char *cmd_line)
{
	check_address(cmd_line);

	int size = strlen(cmd_line) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);

	if (fn_copy == NULL)
		exit(-1);
	strlcpy(fn_copy, cmd_line, size);

	if (process_exec(fn_copy) == -1)
		return -1; // 여기서 process_exec이 실행된다

	return 0;
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file)
{
	check_address(file);
	struct file *target_file = filesys_open(file);
	if (target_file == NULL)
	{
		return -1;
	}
	int fd = fdt_add_fd(target_file);
	if (fd == -1)
	{
		file_close(target_file);
	}
	return fd;
}

int filesize(int fd)
{
	struct file *find_file = fdt_get_file(fd);
	if (find_file == NULL)
		return -1;
	return file_length(find_file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	int read_bytes = -1;

	if (fd == STDIN_FILENO)
	{
		int i;
		unsigned char *buf = buffer;

		for (i = 0; i < size; i++)
		{
			char c = input_getc();
			*buf++ = c;
			if (c == '\0')
				break;
		}
		return i;
	}
	else
	{
		struct file *file = fdt_get_file(fd);
		if (file != NULL && fd != STDOUT_FILENO)
		{
			lock_acquire(&filesys_lock);
			read_bytes = file_read(file, buffer, size);
			lock_release(&filesys_lock);
		}
	}
	return read_bytes;
}

int write(int fd, const void *buffer, unsigned size)
{
	int file_size;
	check_address(buffer);
	int write_bytes = -1;
	if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		return size;
	}
	else
	{
		struct file *file = fdt_get_file(fd);
		if (file != NULL && fd != STDIN_FILENO)
		{
			lock_acquire(&filesys_lock);
			write_bytes = file_write(file, buffer, size);
			lock_release(&filesys_lock);
		}
	}
	return write_bytes;
}

void seek(int fd, unsigned position)
{
	struct file *seek_file = fdt_get_file(fd);
	if (fd < STDOUT_FILENO || seek_file == NULL)
		return;

	return file_seek(seek_file, position);
}

unsigned tell(int fd)
{
	struct file *tell_file = fdt_get_file(fd);
	if (fd < STDOUT_FILENO || tell_file == NULL)
		return;

	return file_seek(tell_file);
}

void close(int fd)
{
	struct thread *curr = thread_current();
	struct file *close_file = fdt_get_file(fd);
	if (fd < STDOUT_FILENO || close_file == NULL || close_file < 2)
	{
		return;
	}
	fdt_remove_fd(fd);
	file_close(close_file);
	curr->fdt[fd] = NULL;
}
