#include "frame.h"
#include "page.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

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
    if (frame == NULL) return NULL;
    
    uint8_t *page_addr = palloc_get_page (flags);

    if (page_addr == NULL)
    {
        frame_evict();
        uint8_t *page_addr = palloc_get_page (flags);
    }
    
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

void
frame_remove(struct single_frame_entry *frame)
{
    if(frame != NULL){
        list_remove(&frame->frame_elem);
        pagedir_clear_page (frame->holder->pagedir, frame->page->user_virtual_address);
        palloc_free_page(frame->frame_address);
        free(frame);
    }
}

void 
frame_evict()
{
    struct single_frame_entry *to_evict = approximate_LRU();
    pagedir_clear_page(to_evict->holder->pagedir, to_evict->page->user_virtual_address);
    frame_remove(to_evict);
    //get the next free block index
    //write the data of the frame to said block

}

struct single_frame_entry* approximate_LRU()
{
    int random_frame_number = rand() % list_size(&frame_table);
    int i = 0;
    
    for (struct list_elem *e = list_begin (&frame_table); e != list_end (&frame_table);
          e = list_next (e))
    {
        if (i == random_frame_number){
            struct single_frame_entry* f = list_entry (e, struct single_frame_entry, frame_elem);
            return f;
        }
        i++;
    }
    return NULL;
}
