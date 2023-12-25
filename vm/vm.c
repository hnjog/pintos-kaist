/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "include/threads/mmu.h"
#include "userprog/process.h"

struct list frame_list;

void spt_destructor(struct hash_elem *e, void* aux);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/*
			페이지를 만들고 VM 유형에 따라 이니셜을 가져온 다음
			uninit_new를 호출하여 "uninit" 페이지 구조를 만듭니다.
			uninit_new를 호출한 후 필드를 수정해야 합니다.
		*/

		struct page* newPage = (struct page *)malloc(sizeof(struct page));
		// 이랬을 때, 할당 못받을 경우에 대하여???
		if(newPage == NULL)
		{
			goto err;
		}

		typedef bool(*initializerFunc)(struct page *, enum vm_type, void *);
        initializerFunc initializer = NULL;

        // vm_type에 따라 다른 initializer를 부른다.
        switch(VM_TYPE(type)){
            case VM_ANON:
                initializer = anon_initializer;
                break;
            case VM_FILE:
                initializer = file_backed_initializer;
                break;
        }

		// 이대로 그냥 넣으면 null이라서 내부에서 assert
		uninit_new(newPage,upage,init,type,aux,initializer);

		newPage->isWritable = writable;

		return spt_insert_page(spt,newPage);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// spt(supplemental_page_table)에서 가상 주소를 찾고, 페이지를 반환하기
// 못찾으면 NULL 반환
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	if(spt == NULL)
		return NULL;

	struct page *page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;
	
	page->va = pg_round_down(va); //해당 va가 속해있는 페이지 시작 주소를 갖는 page를 만든다.

	/* e와 같은 해시값을 갖는 page를 spt에서 찾은 다음 해당 hash_elem을 리턴 */
	e = hash_find(&spt->findTable, &page->spt_hash_elem);
	free(page);

	return e != NULL ? hash_entry(e, struct page, spt_hash_elem): NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	if(spt == NULL ||
		page == NULL)
		return false;

	bool success = false;
	
	// 이미 내부에 존재하는 경우, hash_insert는 null이 아닌 값을 내뱉음
	// 정확히는 이전에 insert한 동일한 값을 내뱉음
	if(hash_insert(&spt->findTable,&page->spt_hash_elem) == NULL)
	{
		success = true;
	}

	return success;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	if(list_empty(&frame_list) == true)
	{
		return NULL;
	}

	struct frame *victim = NULL;
	 
	// frame 과 연결된 page의 access bit가 0인 녀석을 고른다
	// clock 알고리즘
	struct list_elem* tempElem = list_begin(&frame_list);
	struct list_elem* endElem = list_tail(&frame_list);

	struct thread* curr = thread_current();

	while (tempElem != endElem)
	{
		victim = list_entry(tempElem,struct frame,frame_elem);
		if(pml4_is_accessed(curr->pml4,victim->page->va) == true)
		{
			pml4_set_accessed(curr->pml4,victim->page->va,false);
		}
		else
		{
			return victim;
		}
	}
	
	// 여기까지 온 경우, 모든 list의 access가 true여서 전부 false가 된 상황이다
	tempElem = list_begin(&frame_list);
	while (tempElem != endElem)
	{
		victim = list_entry(tempElem,struct frame,frame_elem);
		if(pml4_is_accessed(curr->pml4,victim->page->va) == true)
		{
			pml4_set_accessed(curr->pml4,victim->page->va,false);
		}
		else
		{
			return victim;
		}
	}

	return NULL;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	swap_out(victim->page);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/*
palloc()과 frame을 가져옵니다. 사용 가능한 페이지가 없으면 페이지를 삭제하고 반환합니다. 
이렇게 하면 항상 유효한 주소가 반환됩니다. 즉, 사용자 풀(pool)이 있는 경우입니다
메모리가 가득 찼고, 이 기능은 사용 가능한 메모리 공간을 얻기 위해 프레임을 제거합니다.
*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;

	frame = (struct frame*)malloc(sizeof(struct frame));
	
	frame->page = NULL;
	// kva 가 '물리 메모리'의 user 영역을 가리키도록 한다
	// 따라서 page 와 frame은 'kernel 영역'에 할당된다
	frame->kva = palloc_get_page(PAL_USER);

	if(frame->kva == NULL)
	{
		frame = vm_evict_frame();
		return frame;
	}

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	list_push_back(&frame_list,&frame->frame_elem);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) 
{
	if(vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true))
	{
        vm_claim_page(addr);
        thread_current()->stack_bottom -= PGSIZE;
    }
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;

	// addr은 '가상 메모리'의 위치
	// 따라서 'user' 영역이 아니면 안됨
	if (is_kernel_vaddr(addr) || addr == NULL || not_present == false)
	{
		exit(-1);
		return false;
	}

	if(vm_claim_page(addr) == true)
	{
		return true;
	}
	
    const uintptr_t one_megaByte = (1 << 12);
	uintptr_t stack_limit = USER_STACK - one_megaByte; // pintOS에서 stack의 크기를 1MB로 제한하기에 
	uintptr_t rsp = user ? f->rsp : thread_current()->user_rsp;

	if (addr >= rsp - 8 && 
	addr <= USER_STACK  &&
	 addr >= stack_limit)
	{
		vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
		return true;
    }

	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	if(va == NULL)
	{
		return false;
	}

	struct page *page = NULL;
	
	struct thread* curr = thread_current();
	struct supplemental_page_table* lpSpt = &curr->spt;
	page = spt_find_page(lpSpt,va);

	if(page == NULL)
	{
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/*
클레임, 즉 물리적 프레임, 페이지를 할당합니다. 
먼저 vm_get_frame을 호출하여 프레임을 가져옵니다(템플릿에서 이미 완료됨). 
그런 다음 MMU를 설정해야 합니다. 
즉, 가상 주소에서 페이지 테이블의 실제 주소로 매핑을 추가합니다. 
반환 값은 작업이 성공했는지 여부를 나타내야 합니다.

-> 프레임과 페이지를 링크
vm_get_Frame으로 유효한 frame을 가져오고

*/
static bool
vm_do_claim_page (struct page *page) {
	if(page == NULL)
	{
		return false;
	}

	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread* curr = thread_current();
	struct supplemental_page_table* lpSpt = &curr->spt;

	// 현재 메모리에 매핑되어 있지 않음
	if(pml4_get_page(curr->pml4,page->va) == NULL)
	{
		if(pml4_set_page(curr->pml4,page->va,frame->kva,page->isWritable) == false)
		{
			return false;
		}
	}

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) 
{
	if(spt == NULL)
		return;
	
	hash_init(&spt->findTable,page_hash,page_less,NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) 
{
	struct hash_iterator i;

	// 원본 hash 반복문
	hash_first(&i, &src->findTable);
	while (hash_next(&i))
	{
		struct page *src_page =
            hash_entry(hash_cur(&i), struct page, spt_hash_elem);
        /*vm_alloc_page_with_initializer에 필요한 인자들*/
        enum vm_type dst_type = page_get_type(src_page);
        enum vm_type now_type = src_page->operations->type;
        void *dst_va = src_page->va;
        bool dst_writable = src_page->isWritable;

        /*UNINIT 처리*/
        if (now_type == VM_UNINIT) {
            vm_initializer *dst_init = src_page->uninit.init;
            void *dst_aux = src_page->uninit.aux;
            if (!vm_alloc_page_with_initializer(dst_type, dst_va, dst_writable,
                                                dst_init, dst_aux)) {
                return false;
            }
        }
        /*ANON, FILE 처리*/
        else {
            if (!vm_alloc_page(now_type, dst_va, dst_writable)) {
                return false;
            }
            if (!vm_claim_page(dst_va)) {
                return false;
            }
            struct page * dst_page = spt_find_page(dst, dst_va);

            memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
        }
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// struct hash_iterator i;

    // hash_first(&i, &spt->findTable);
    // while(hash_next(&i))
	// {
    //     struct page *page = hash_entry(hash_cur(&i), struct page, spt_hash_elem);

    //     if(page->operations->type == VM_FILE)
	// 	{
    //         do_munmap(page->va);
    //     }
    // }

	hash_clear(&spt->findTable,spt_destructor);
	//hash_destroy(&spt->findTable,spt_destructor);
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, spt_hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, spt_hash_elem);
  const struct page *b = hash_entry (b_, struct page, spt_hash_elem);

  return a->va < b->va;
}

void spt_destructor(struct hash_elem *e, void* aux){
    const struct page *p = hash_entry(e, struct page, spt_hash_elem);
    free(p);
}