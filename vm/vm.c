/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "threads/vaddr.h" /* USER_STACK, PHYS_BASE 정의 */
#include "vm/inspect.h"

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
/* 초기화 함수를 포함한 보류 중인 페이지 객체를 생성한다.
 * 만약 페이지를 생성하고 싶다면,
 * 직접 생성하지 말고 이 함수나 `vm_alloc_page`를 통해 생성하라. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        /* TODO: 페이지를 생성하고, VM 타입에 따라 초기화 함수를 가져온 뒤,
         * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성하라.
         * TODO: uninit_new를 호출한 이후에 해당 필드들을 수정해야 한다. */

        /* TODO: 생성한 페이지를 spt(보조 페이지 테이블)에 삽입하라. */
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function. */

    return page;
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
/* 페이지 하나를 제거(evict)하고, 해당하는 프레임을 반환한다.
 * 에러가 발생하면 NULL을 반환한다. */

static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* palloc()을 이용해 프레임을 할당한다. 사용 가능한 페이지가 없다면,
 * 페이지를 제거(evict)한 뒤 그 프레임을 반환한다.
 * 이 함수는 항상 유효한 주소를 반환한다. 즉, 사용자 풀 메모리가 가득 차 있는 경우에도,
 * 사용 가능한 메모리 공간을 확보하기 위해 프레임을 제거하여 반환한다. */

static struct frame *vm_get_frame(void) {
    struct frame *frame = NULL;
    /* TODO: Fill this function. */

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    /* TODO: 페이지 폴트가 유효한 상황인지 확인하세요. */
    /* TODO: 여기에 당신의 구현 코드를 작성하면 됩니다. */

    /* 1. 기본 검증: 페이지가 없고, 유저 모드에서 쓰기 시도인가? */

    if (not_present && user && write) {
 
        /* 2. 유저 가상 주소 영역인지 확인 */
        if (!is_user_vadder(addr)) {
            return false;
        }
        /* intr_frame 포인터로 rsp가 그 시점의 스택포인터-> 이를 기준으로 유효한지 판단*/
        void *rsp = f->rsp;

        /*rsp보다 안쪽에 찍혀있는지*/
        bool in_rsp = ((uint8_t *)addr >= (uint8_t *)rsp - 8);
        /*유저영역을 벗어나지 않았는지*/
        bool top_stack = ((uint8_t *)addr <= (uint8_t *)USER_STACK);
        /*스택의 끝을 넘지 않았는지*/
        bool stack_limit = ((uint8_t *)USER_STACK - (uint8_t *)addr <= (1 << 20));
        if (in_rsp && top_stack && stack_limit) {
            vm_stack_growth(addr);
            page = spt_find_page(spt, addr);
        } else {
            return false;
        }
        if (page == NULL) {
            return false;
        }

        return vm_do_claim_page(page);
    }

    /* Free the page.
     * DO NOT MODIFY THIS FUNCTION. */
    void vm_dealloc_page(struct page * page) {
        destroy(page);
        free(page);
    }

    /* Claim the page that allocate on VA. */
    bool vm_claim_page(void *va UNUSED) {
        struct page *page = NULL;
        /* TODO: Fill this function */

        return vm_do_claim_page(page);
    }

    /* Claim the PAGE and set up the mmu. */
    static bool vm_do_claim_page(struct page * page) {
        struct frame *frame = vm_get_frame();

        /* Set links */
        frame->page = page;
        page->frame = frame;

        /* TODO: Insert page table entry to map page's VA to frame's PA. */

        return swap_in(page, frame->kva);
    }

    /* Initialize new supplemental page table */
    void supplemental_page_table_init(struct supplemental_page_table * spt UNUSED) {}

    /* Copy supplemental page table from src to dst */
    bool supplemental_page_table_copy(struct supplemental_page_table * dst UNUSED,
                                      struct supplemental_page_table * src UNUSED) {}

    /* Free the resource hold by the supplemental page table */
    void supplemental_page_table_kill(struct supplemental_page_table * spt UNUSED) {
        /* TODO: Destroy all the supplemental_page_table hold by thread and
         * TODO: writeback all the modified contents to the storage. */
    }
