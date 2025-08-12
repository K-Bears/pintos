/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

#define SECPERPAGE (PGSIZE / DISK_SECTOR_SIZE)
static struct bitmap *swap_table;
static struct lock swap_lock;

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);
static void free_swap(size_t swap_slot);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

static bool read_swap_page(size_t swap_slot, char *buffer, size_t size);
static bool write_swap_page(size_t swap_slot, char *buffer, size_t size);
static size_t alloc_swap_slot(void);

/* Initialize the data for anonymous pages */
void vm_anon_init(void) {
    /* Set up the swap_disk. */
    swap_disk = disk_get(1, 1);
    size_t swap_slot_size = disk_size(swap_disk) / SECPERPAGE;
    swap_table = bitmap_create(swap_slot_size);
    lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    page->operations = &anon_ops;

    struct anon_page *anon_page = &page->anon;
    anon_page->alloc_swap = false;
    anon_page->swap_slot = 0;
    return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
    struct anon_page *anon_page = &page->anon;
    if (!anon_page->alloc_swap) {
        return false;
    }
    anon_page->alloc_swap = false;
    read_swap_page(anon_page->swap_slot, page->frame->kva, PGSIZE);
    free_swap(anon_page->swap_slot);
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
    size_t swap_slot = alloc_swap_slot();
    if (swap_slot == BITMAP_ERROR) {
        return false;
    }
    struct anon_page *anon_page = &page->anon;
    anon_page->alloc_swap = true;
    anon_page->swap_slot = swap_slot;
    write_swap_page(anon_page->swap_slot, page->frame->kva, PGSIZE);
    page->frame = NULL;
    *(page->pte) = NULL;
    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
    struct anon_page *anon_page = &page->anon;
    if (anon_page->alloc_swap) {
        free_swap(anon_page->swap_slot);
    }
}

static bool read_swap_page(size_t swap_slot, char *buffer, size_t size) {
    ASSERT(size <= PGSIZE);
    size_t n = (size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;
    disk_sector_t sector = swap_slot * SECPERPAGE;
    for (int i = 0; i < n; i++) {
        disk_read(swap_disk, sector, ((char *)(buffer) + (i * DISK_SECTOR_SIZE)));
    }
    return true;
}

static bool write_swap_page(size_t swap_slot, char *buffer, size_t size) {
    ASSERT(size <= PGSIZE);
    size_t n = (size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;
    disk_sector_t sector = swap_slot * SECPERPAGE;
    for (int i = 0; i < n; i++) {
        disk_write(swap_disk, sector, ((char *)(buffer) + (i * DISK_SECTOR_SIZE)));
    }
    return true;
}

static size_t alloc_swap_slot(void) {
    lock_acquire(&swap_lock);
    size_t swap_slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
    lock_release(&swap_lock);
    return swap_slot;
}

bool copy_anon_page(struct hash *hash, struct page *dst_page, struct page *src_page) {
    uninit_new(dst_page, src_page->va, NULL, VM_ANON, NULL, anon_initializer);
    dst_page->writable = src_page->writable;
    hash_insert(hash, &dst_page->hash_elem);
    if (!vm_claim_page(dst_page->va)) {
        return false;
    }
    if (src_page->anon.alloc_swap) {
        read_swap_page(src_page->anon.swap_slot, dst_page->frame->kva, PGSIZE);
    } else {
        memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }
    return true;
}

static void free_swap(size_t swap_slot) {
    bitmap_set_multiple(swap_table, swap_slot, 1, false);
}