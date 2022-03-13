#include "frame.h"
#include "page.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"

void
frame_table_init(void)
{
    //Initialize frame table list
    list_init(&frame_table);
}

void*
frame_add(enum palloc_flags flags, uint8_t *user_virtual_address, bool writable) 
{
    struct single_frame_entry *frame = (struct single_frame_entry*) malloc(sizeof(struct single_frame_entry));
    uint32_t *page_addr = palloc_get_page (flags);

    ASSERT(page_addr != NULL);
    
    struct supplemental_page_entry *s = new_supplemental_page_entry(user_virtual_address, writable);

    frame->holder = thread_current();
    frame->page = s;
    frame->frame_address = page_addr;

    list_push_front(&frame_table, &frame->frame_elem);
    return page_addr;
}
