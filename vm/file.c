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
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) 
{
	if(page == NULL)
	{
        return false;
    }

    struct loadArgs *aux = (struct loadArgs*)page->uninit.aux;

    struct file *file = aux->file;
    off_t offset = aux->fileOfs;
    size_t page_read_bytes = aux->readByte;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    file_seek(file, offset);
    if(file_read(file, kva, page_read_bytes) != (int)page_read_bytes)
	{
        return false;
    }

    memset(kva + page_read_bytes, 0, page_zero_bytes);

    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) 
{
    if(page == NULL)
	{
        return false;
    }

    struct loadArgs *aux = (struct loadArgs *)page->uninit.aux;

    // dirty check
    if(pml4_is_dirty(thread_current()->pml4, page->va))
	{
		// file에 수정된 부분을 write해준다
        file_write_at(aux->file, page->va, aux->readByte, aux->fileOfs);
        pml4_set_dirty(thread_current()->pml4, page->va, false);
    }
    pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) 
{
	if(page == NULL)
	{
        return;
    }

	if (page->operations == &file_ops)
	{
		struct uninit_page *uninit = &page->uninit;
		if (uninit->aux != NULL)
		{
			free(uninit->aux);
			uninit->aux = NULL;
		}
	}
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
	struct file* reopenFile = file_reopen(file);

	void* origin_addr = addr;
	size_t read_bytes = length > file_length(file) ? file_length(file) : length;
	size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(addr) == 0);      // upage가 페이지 정렬되어 있는지 확인
    ASSERT(offset % PGSIZE == 0); // ofs가 페이지 정렬되어 있는지 확인

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
		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
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
}

/* Do the munmap */
void
do_munmap (void *addr)
{
	struct thread *curr = thread_current();
	while (true)
	{
		struct page *targetPage = spt_find_page(&curr->spt, addr);
		if (targetPage == NULL)
			return;

		struct loadArgs *aux = (struct loadArgs *)targetPage->uninit.aux;

		// null 인 경우 아래에서 null 참조가 일어나게 된다
		// dirty check
		if (pml4_is_dirty(curr->pml4, targetPage->va) == true)
		{
			file_write_at(aux->file, addr, aux->readByte, aux->fileOfs);
			pml4_set_dirty(curr->pml4, targetPage->va, false);
		}

		pml4_clear_page(curr->pml4, targetPage->va);

		addr += PGSIZE;
	}
}
