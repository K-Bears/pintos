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

    read_swap_page(anon_page->swap_slot, page->frame->kva, PGSIZE);
    bitmap_set_multiple(swap_table, swap_table, 1, false);
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
    size_t swap_slot = alloc_swap_slot();
    if (swap_slot == BITMAP_ERROR) {
        return false;
    }
    struct anon_page *anon_page = &page->anon;
    anon_page->swap_slot = swap_slot;
    write_swap_page(anon_page->swap_slot, page->frame->kva, PGSIZE);
    page->frame = NULL;
    *(page->pte) = NULL;
    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
    struct anon_page *anon_page = &page->anon;
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

bool duplicate_swap_slot(struct page *dst_page, struct page *src_page) {
    char *buffer = palloc_get_page(0);
    if (buffer == NULL) {
        return false;
    }
    read_swap_page(src_page->anon.swap_slot, buffer, PGSIZE);
    size_t swap_slot = alloc_swap_slot();
    if (swap_slot == BITMAP_ERROR) {
        palloc_free_page(buffer);
        return false;
    }
    write_swap_page(swap_slot, buffer, PGSIZE);
    dst_page->anon.alloc_swap = true;
    dst_page->anon.swap_slot = swap_slot;
    palloc_free_page(buffer);
    return true;
}