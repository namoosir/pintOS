#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "lib/kernel/hash.h"

//create page table data structure
struct supplemental_page_entry 
{
    uint8_t* user_virtual_address;
    uint64_t time_accessed;
    bool dirty;
    bool accessed;
    bool writable;
    struct hash_elem supplemental_page_elem; 
};

struct supplemental_page_entry* new_supplemental_page_entry(uint8_t* user_virtual_address, bool writable);
bool page_table_hash_comparator(const struct hash_elem *a, const struct hash_elem *b, void* aux);
unsigned page_hash (const struct hash_elem *p_, void *aux);
struct supplemental_page_entry *page_lookup (void *address);
#endif /* vm/page.h */
