/* file.c: Implementation of memory backed file object (mmaped object). */

#include "userprog/process.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */

void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) 
{
	// addr 부터 연속된 user virtual address space에
	// page들을 만들어 offset 부터 length 까지
	// 해당하는 file의 정보를 각 page에 저장

	// 기본적으로 'process'의 'load segment' 와 비슷하게 처리된다

	// 파일을 다시 열어준다
	// 정확히는 내부의 'open_cnt'를 조절하여
	// 누가 함부로 닫지 않도록 해준다
#ifdef VM
	struct file* reopenFile = file_reopen(file);

	void* origin_addr = addr;
	size_t read_bytes = length > file_length(file) ? file_length(file) : length;
	size_t zero_bytes = PGSIZE - read_bytes;

	while (read_bytes > 0 || zero_bytes > 0) 
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct loadArgs* largP = (struct loadArgs*)malloc(sizeof(struct loadArgs));
		if(largP == NULL)
		{
			return false;
		}

		largP->file = reopenFile;
		largP->fileOfs = offset;
		largP->readByte = page_read_bytes;

		void *aux = largP;
		if (!vm_alloc_page_with_initializer (VM_ANON, addr,
					writable, lazy_load_segment, aux))
		{
			free(largP);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		// 나중에 읽어줄 거라
		// 해당 시점에서의 ofs가 따로 필요함
		// '반복문'을 돌면서 page를 따로 할당할 수 있음
		offset += page_read_bytes;
	}

	// 시작 위치
	return origin_addr;
#endif
	return NULL;
}

/* Do the munmap */
void
do_munmap (void *addr) 
{
	struct thread* curr = thread_current();
	while (true)
	{
		struct page* targetPage = spt_find_page(&curr->spt,addr);
		if(targetPage == NULL)
			break;

		struct loadArgs* aux = (struct loadArgs*)targetPage->uninit.aux;

		// dirty check
		if(pml4_is_dirty(curr->pml4,targetPage->va) == true)
		{
			file_write_at(aux->file,addr,aux->readByte,aux->fileOfs);
			pml4_set_dirty(curr->pml4,targetPage->va,false);
		}

		pml4_clear_page(curr->pml4,targetPage->va);
		addr += PGSIZE;
	}
}
