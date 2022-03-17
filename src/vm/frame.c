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

struct single_frame_entry*
frame_add(enum palloc_flags flags, uint8_t *user_virtual_address, bool writable, enum create_sup_page_entry should_create_sup_page_entry) 
{
    struct single_frame_entry *frame = (struct single_frame_entry*) malloc(sizeof(struct single_frame_entry));
    uint8_t *page_addr = palloc_get_page (flags);

    ASSERT(page_addr != NULL);
    
    if(should_create_sup_page_entry == CREATE_SUP_PAGE_ENTRY){
        struct page_data pg_data;
        pg_data.file = NULL;
        pg_data.ofs = -1;
        pg_data.read_bytes = -1;
        struct supplemental_page_entry *s = new_supplemental_page_entry(FROM_FRAME_TABLE, user_virtual_address, writable, pg_data);
        frame->page = s;
    } else{
        frame->page = NULL;
    }
    frame->holder = thread_current();
    frame->frame_address = page_addr;

    list_push_front(&frame_table, &frame->frame_elem);
    return frame;
}
