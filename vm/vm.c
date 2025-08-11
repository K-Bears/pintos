/* vm.c: Generic interface for virtual memory objects. */
#include "vm/vm.h"

#include <string.h>

#include "kernel/hash.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/check_perm.h"
#include "vm/inspect.h"

static struct frame *frame_table;
static size_t next;

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
    size_t n = (user_pages * sizeof(struct frame) + PGSIZE - 1) >> PGBITS;
    frame_table = palloc_get_multiple(PAL_ZERO, n);
    next = 0;

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
static void vm_free_frame(struct frame *frame);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        /* Create the page, fetch the initializer according to the VM type,
         * and then create "uninit" page struct by calling uninit_new. You
         * should modify the field after calling the uninit_new. */
        struct page *Page = malloc(sizeof(struct page));
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
        /* Insert the page into the spt. */
        spt_insert_page(spt, Page);

        return true;
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    struct page *page = NULL;
    struct page p;
    p.va = pg_round_down(va);
    struct hash_elem *e = hash_find(&spt->table, &p.hash_elem);
    if (e == NULL) {
        return NULL;
    }
    page = hash_entry(e, struct page, hash_elem);

    return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
    return hash_insert(&spt->table, &page->hash_elem);
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    // Frame 해제
    vm_free_frame(page->frame);
    vm_dealloc_page(page);
    // hash 테이블에서 제거
    hash_delete(&spt->table, &page->hash_elem);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
    struct frame victim;
    uintptr_t *pte;
    for (int i = 0; i <= user_pages; i++) {
        victim = frame_table[next++];
        if (next >= user_pages) {
            next = 0;
        }

        pte = (victim.page->pte);
        if (*pte & PTE_A) {
            *pte &= ~(uintptr_t)PTE_A;
            continue;
        }

        return &victim;
    }
    return NULL;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */
    if (!swap_out(victim->page)) {
        // PANIC
        NOT_REACHED();
    }
    victim->page->pte = NULL;
    victim->page = NULL;

    return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
    struct frame *frame = NULL;
    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if (kpage == NULL) {
        // swapping
        return vm_evict_frame();
    }
    frame = &frame_table[pg_no(kpage) - pg_no(user_pool_base)];
    frame->kva = kpage;
    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
}

static void vm_free_frame(struct frame *frame) {
    palloc_free_page(frame->kva);
    frame->page = NULL;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {
    vm_alloc_page(VM_ANON, pg_round_down(addr), true);
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

static bool vm_check_stack_growth(void *addr) {
    // pushq 명령어의 경우 최대 8 까지만 내려감(레지스터 크기가 8)
    return (addr >= thread_current()->saved_user_rsp - 8 && addr < USER_STACK &&
            addr >= USER_STACK - (1 << 20));
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
    // struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    // struct page *page = NULL;
    /* Validate the fault */
    if (!check_user_addr_valid(addr)) {
        return false;
    }

    // FIXME : COW 때 고쳐야 함
    if (!not_present) {
        return false;
    }

    // spt 보고 stack groth 판단
    struct page *page = NULL;
    page = spt_find_page(&thread_current()->spt, pg_round_down(addr));
    if (page == NULL && vm_check_stack_growth(addr)) {
        vm_stack_growth(pg_round_down(addr));
        page = spt_find_page(&thread_current()->spt, pg_round_down(addr));
    }
    if (page == NULL) {
        return false;
    }

    return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
    // TODO: 여기에는 주소 검증이 없어도 될까?
    struct page *page = NULL;
    page = spt_find_page(&thread_current()->spt, va);
    if (page == NULL) {
        return false;
    }

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
    if (page == NULL) {
        return false;
    }

    struct frame *frame = vm_get_frame();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* Insert page table entry to map page's VA to frame's PA. */
    if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
        return false;
    }
    page->pte = pml4e_walk(thread_current()->pml4, page->va, false);
    return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    if (!hash_init(&spt->table, page_hash, page_less, spt)) {
        PANIC("!!왜 말록함? 진짜 킹받네!!\n");
    }
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
    struct hash_iterator it;
    struct page *src_page, *dst_page;
    hash_first(&it, &src->table);
    while (hash_next(&it)) {
        dst_page = NULL;
        src_page = hash_entry(hash_cur(&it), struct page, hash_elem);
        if ((dst_page = malloc(sizeof(struct page))) == NULL) {
            return false;
        }
        switch (src_page->operations->type) {
            case VM_UNINIT:
                void *aux = NULL;
                if (src_page->uninit.aux) {
                    size_t *size = src_page->uninit.aux;
                    if ((aux = malloc(*size)) == NULL) {
                        goto copy_err;
                    }
                    memcpy(aux, src_page->uninit.aux, *size);
                }
                uninit_new(dst_page, src_page->va, src_page->uninit.init, src_page->uninit.type,
                           aux, src_page->uninit.page_initializer);
                break;
            case VM_ANON:
                uninit_new(dst_page, src_page->va, NULL, VM_ANON, NULL, anon_initializer);
                if (src_page->frame == NULL) {
                    if (!vm_do_claim_page(src_page)) {
                        goto copy_err;
                    }
                }
                if (!vm_do_claim_page(dst_page)) {
                    goto copy_err;
                }
                memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
                break;
            default:
                break;
        }
        dst_page->writable = src_page->writable;
        hash_insert(&dst->table, &dst_page->hash_elem);
    }
    return true;
copy_err:
    free(dst_page);
    return false;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
    hash_destroy(&spt->table, page_delete);
}

void page_delete(struct hash_elem *e, void *aux) {
    struct page *p = hash_entry(e, struct page, hash_elem);
    vm_dealloc_page(p);
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
