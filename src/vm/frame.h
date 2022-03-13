#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
#include "threads/palloc.h"

// List of all frames
struct list frame_table;

//Single frame table entry
struct single_frame_entry 
{
    uint32_t *frame_address;
    struct thread *holder;
    struct single_page_entry *page;
    struct list_elem frame_elem;
};

void frame_table_init(void);
void* frame_add(enum palloc_flags flags, uint8_t *user_virtual_address, bool writable);
#endif /* vm/frame.h */
