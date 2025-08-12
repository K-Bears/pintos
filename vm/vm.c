/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "kernel/hash.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "vm/inspect.h"

static struct frame *frame_table;  // 프레임 테이블

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
    // 필요한 페이지 수 계산
    size_t n = (user_pages * sizeof(struct frame) + PGSIZE - 1) >> PGBITS;  // user_pool 페이지 개수
    frame_table = palloc_get_multiple(PAL_ZERO, n);                         // 프레임 테이블 초기화

    ASSERT(frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
    int ty = VM_TYPE(page->operations->type);
    switch (ty) {
        case VM_UNINIT:
            return VM_TYPE(page->uninit.type);
        default:
            return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        /* TODO: Create the page, fetch the initializer according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */
        struct page *Page = palloc_get_page(PAL_USER);  // malloc 충분. malloc으로 수정
        if (Page == NULL) {
            goto err;
        } else {
            void *initializer;
            switch (type) {
                case VM_ANON:
                    initializer = anon_initializer;
                    break;
                case VM_FILE:
                    initializer = file_backed_initializer;
                default:
                    break;
            }

            uninit_new(Page, upage, init, type, aux, initializer);
            Page->writable = writable;
        }
        /* TODO: Insert the page into the spt. */
        hash_insert(&thread_current()->spt.table, &Page->hash_elem);

        return true;
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function. */
    if (spt == NULL || va == NULL)
        return NULL;

    void *uva = pg_round_down(va);  // 주소를 round_down하는 이유 ?

    struct page key;
    key.va = uva;
    struct hash_elem *e = hash_find(&spt->table, &key.hash_elem);
    if (e == NULL)
        return NULL;

    return hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
    int succ = false;
    /* TODO: Fill this function. */

    return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    vm_dealloc_page(page);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
    struct frame *frame = NULL;
    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if (kpage == NULL) {
        // TODO : swapping
        ASSERT(kpage);
    }
    frame = &frame_table[pg_no(kpage) - pg_no(user_pool_base)];
    frame->kva = kpage;
    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {
    // addr이 더 이상 페이지 폴트를 일으키지 않도록 하나 이상의 익명 페이지를 할당해야 함.
    // 이 때 addr을 페이지 크기(PGSIZE) 단위로 내림 처리해야 함.
    vm_alloc_page(VM_ANON, pg_round_down(addr), 1);
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
    /* TODO: Validate the fault */
    /* TODO: Your code goes here */

    // spt에 엔트리가 존재하는지 확인
    // 페이지 폴트가 스택 증가로 처리할 수 있는 유효한 경우인지 검사.
    // 스택 증가가 유효하면,  vm_stack_growth(addr)를 호출해서 추가 페이지를 할당.
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    if (addr == NULL)
        return false;

    if (is_kernel_vaddr(addr))
        return false;

    if (not_present)  // 접근한 메모리의 physical page가 존재하지 않은 경우
    {
        /* TODO: Validate the fault */
        // 페이지 폴트가 스택 확장에 대한 유효한 경우인지를 확인한다.
        void *rsp = f->rsp;  // user access인 경우 rsp는 유저 stack을 가리킨다.
        if (!user)           // kernel access인 경우 thread에서 rsp를 가져와야 한다.
            rsp = thread_current()->rsp;

        /* 1. addr이 NULL이 아닌 경우
        2. addr이 1MB 보다 작아야 함
        3. 인터럽트 프레임에 저장되는 f->rsp는 폴트 직전의 RSP로 보는 게 관례라서, 유요한 스택
        접근인지 보기위해서는 rsp - 8 <= addr인지 확인
        4. USER_STACK보다 아래 주소에 존재해야 함 */

        if (addr != NULL && USER_STACK - (1 << 20) <= addr && rsp - 8 <= addr &&
            addr < USER_STACK) {
            vm_stack_growth(addr);
        }
        page = spt_find_page(spt, addr);

        if (page == NULL)
            return false;
        if (write == 1 && page->writable == 0)  // write 불가능한 페이지에 write 요청한 경우
            return false;
        return vm_do_claim_page(page);
    }
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function */
    struct page p;
    p.va = va;
    struct hash_elem *e = hash_find(&thread_current()->spt, &p.hash_elem);
    page = hash_entry(e, struct page, hash_elem);

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
    struct frame *frame = vm_get_frame();  // swap 구현

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    switch (page_get_type(page)) {
        case VM_ANON:
            if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
                return false;
            }
            break;
        case VM_FILE:
            break;
        default:
            break;
    }
    return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    ASSERT(spt != NULL);
    hash_init(spt, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
    /*
    src SPT를 순회하면서, 각 SPT 엔트리를 dst SPT에 복사한다.
    이때 각 엔트리(페이지)의 타입에 따라 복사하는 방법을 다르게 적용한다.
    VM_UNINIT의 경우,
    VM_FILE의 경우,
    VM_ANON의 경우,
    */
    // TODO: 보조 페이지 테이블을 src에서 dst로 복사합니다.
    // TODO: src의 각 페이지를 순회하고 dst에 해당 entry의 사본을 만듭니다.
    // TODO: uninit page를 할당하고 그것을 즉시 claim해야 합니다.
    struct hash_iterator i;
    hash_first(&i, &src->table);
    while (hash_next(&i)) {
        // src_page 정보
        struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
        enum vm_type type = src_page->operations->type;
        void *upage = src_page->va;
        bool writable = src_page->writable;

        /* 1) type이 uninit이면 */
        if (type == VM_UNINIT) {  // uninit page 생성 & 초기화
            vm_initializer *init = src_page->uninit.init;
            void *aux = src_page->uninit.aux;
            vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
            continue;
        }

        /* 2) type이 file이면 */
        if (type == VM_FILE) {
            // struct lazy_load_arg *file_aux = malloc(sizeof(struct lazy_load_arg));
            // file_aux->file = src_page->file.file;
            // file_aux->ofs = src_page->file.offset;
            // file_aux->read_bytes = src_page->file.read_bytes;
            // file_aux->zero_bytes = src_page->file.zero_bytes;
            // if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux))
            //     return false;
            // struct page *file_page = spt_find_page(dst, upage);
            // file_backed_initializer(file_page, type, NULL);
            // file_page->frame = src_page->frame;
            // pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva,
            //               src_page->writable);
            if (!vm_alloc_page(type, upage, writable)) {
                return false;
            }

            if (!vm_claim_page(upage)) {
                return false;
            }

            struct page *dst_page = spt_find_page(dst, upage);
            if (dst_page == NULL || src_page->frame == NULL) {
                return false;
            }

            memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
            continue;
        }

        /* 3) type이 anon이면 */
        if (!vm_alloc_page(type, upage, writable))  // uninit page 생성 & 초기화
            return false;  // init이랑 aux는 Lazy Loading에 필요. 지금 만드는 페이지는 기다리지 않고
                           // 바로 내용을 넣어줄 것이므로 필요 없음

        // vm_claim_page으로 요청해서 매핑 & 페이지 타입에 맞게 초기화
        if (!vm_claim_page(upage))
            return false;

        // 매핑된 프레임에 내용 로딩
        struct page *dst_page = spt_find_page(dst, upage);
        memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }
    return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
}

unsigned page_hash(const struct hash_elem *p_, void *aux) {
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux) {
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);
    return a->va < b->va;
}
