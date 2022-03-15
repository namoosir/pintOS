#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "lib/kernel/hash.h"

enum flag{
    FROM_FRAME_TABLE,
    FROM_SWAPED,
    FROM_FILE_SYSTEM,
    ALL_ZERO,
};
struct page_data
{
    struct file *file;
    int32_t ofs;
    uint32_t read_bytes;
};
//create page table data structure
struct supplemental_page_entry 
{
    uint8_t* user_virtual_address;
    uint64_t time_accessed;
    bool dirty;
    bool accessed;
    bool writable;
    struct page_data pg_data;
    // Flag to indicate swaped out or filemapped page
    enum flag page_flag;

    struct hash_elem supplemental_page_elem; 
};



struct page_data save_page_data(struct file *f, int32_t ofs, uint32_t read_bytes);
struct supplemental_page_entry* new_supplemental_page_entry(int page_flag, uint8_t* user_virtual_address, bool writable, struct page_data pg_data);
bool page_table_hash_comparator(const struct hash_elem *a, const struct hash_elem *b, void* aux);
unsigned page_hash (const struct hash_elem *p_, void *aux);
struct supplemental_page_entry *page_lookup (void *address);
#endif /* vm/page.h */
