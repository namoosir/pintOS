#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
#include "threads/palloc.h"

// List of all frames
struct list frame_table;

enum create_sup_page_entry{
    CREATE_SUP_PAGE_ENTRY,
    DONT_CREATE_SUP_PAGE_ENTRY,
};

//Single frame table entry
struct single_frame_entry 
{
    uint8_t *frame_address;
    struct thread *holder;
    struct supplemental_page_entry *page;
    struct list_elem frame_elem;
};

void frame_table_init(void);
struct single_frame_entry* frame_add(enum palloc_flags flags, uint8_t *user_virtual_address, bool writable, enum create_sup_page_entry);
#endif /* vm/frame.h */
