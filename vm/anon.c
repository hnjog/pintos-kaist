/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"

struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = -1;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	 // swap out된 page가 disk swap영역 어느 위치에 저장되었는지는 
    // anon_page 구조체 안에 저장되어 있다.
    int page_no = anon_page->swap_index;

    if(bitmap_test(swap_table, page_no) == false)
	{
        return false;
    }

    // 해당 swap 영역의 data를 가상 주소공간 kva에 써준다.
    for(int i=0; i< SECTORS_PER_PAGE; ++i)
	{
        disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
    }
    // 해당 swap slot false로 만들어줌(다음번에 쓸 수 있게)
    bitmap_set(swap_table, page_no, false);

    return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	 // swap table에서 page를 할당받을 수 있는 swap slot 찾기
    int page_no = bitmap_scan(swap_table, 0, 1, false);
    if(page_no == BITMAP_ERROR)
	{
        return false;
    }
    // 한 page를 disk에 쓰기 위해 SECTORS_PER_PAGE개의 섹터에 저장한다.
    // 이 때 disk의 각 섹터의 크기(DISK_SECTOR_SIZE)만큼 써 준다.
    for(int i=0; i<SECTORS_PER_PAGE; ++i)
	{
        disk_write(swap_disk, page_no *SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
    }

    // swap table의 해당 page에 대한 swap slot의 bit를 ture로 바꿔준다.
    // 해당 page의 pte에서 present bit을 0으로 바꿔준다.
    // 이제 프로세스가 이 page에 접근하면 page fault가 뜬다.
    bitmap_set(swap_table, page_no, true);
    pml4_clear_page(thread_current()->pml4, page->va);
    // page의 swap_index 값을 이 page가 저장된 swap slot의 번호로 써준다.
    anon_page->swap_index = page_no;

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	if(page == NULL)
	{
		return;
	}

	if (page->operations == &anon_ops)
	{
		// struct anon_page *anon_page = &page->anon;
		// if (anon_page != NULL)
		// {
		// 	file_close(anon_page);
		// 	anon_page = NULL;
		// }
		struct uninit_page *uninit = &page->uninit;
		if (uninit->aux != NULL)
		{
			free(uninit->aux);
			uninit->aux = NULL;
		}
	}
}
