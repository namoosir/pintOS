#include "frame.h"
#include "page.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

static int clock_pointer = 0;

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
        // frame_evict();
        struct single_frame_entry *replacer_frame = clear_page_and_swap_insert();
        page_addr = palloc_get_page (flags);
        if (page_addr == NULL)
        {
            return NULL;
        }
        else
        {
            replacer_frame->frame_address = page_addr;
            replacer_frame->holder = thread_current();
            if(should_create_sup_page_entry == CREATE_SUP_PAGE_ENTRY)
            {
                struct page_data pg_data;
                pg_data.file = NULL;
                pg_data.ofs = -1;
                pg_data.read_bytes = -1;
                struct supplemental_page_entry *s = new_supplemental_page_entry(FROM_FRAME_TABLE, user_virtual_address, writable, pg_data);
                replacer_frame->page = s;
            } else
            {
                replacer_frame->page = NULL;
            }
            return replacer_frame;
        }
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

struct single_frame_entry*
clear_page_and_swap_insert()
{
    struct single_frame_entry *to_evict = approximate_LRU();

    pagedir_clear_page(to_evict->holder->pagedir, to_evict->page->user_virtual_address);
    read_or_write_from_block(to_evict->page, index, WRITE);
    //get the next free block index
    //write the data of the frame to said block
}

//Given a list get the item at that index
struct single_frame_entry*
get_frame_at_index(int index)
{
    if(index >= list_size(&frame_table)) return NULL;
    if(index < 0) return NULL;

    struct list_elem *e;
    int i = 0;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct single_frame_entry *frame = list_entry(e, struct single_frame_entry, frame_elem);
        if(i == index){
            return frame;
        }
        i++;
    }
    return NULL;
}

void
increment_clock_pointer(){
    clock_pointer++;
    if(clock_pointer >= list_size(&frame_table)){
        clock_pointer = 0;
    }
}

struct single_frame_entry* 
approximate_LRU()
{
    // int random_frame_number = rand() % list_size(&frame_table);
    // int i = 0;
    
    // for (struct list_elem *e = list_begin (&frame_table); e != list_end (&frame_table);
    //       e = list_next (e))
    // {
    //     if (i == random_frame_number){
    //         struct single_frame_entry* f = list_entry (e, struct single_frame_entry, frame_elem);
    //         return f;
    //     }
    //     i++;
    // }
    // return NULL;
    struct single_frame_entry *eviction_frame = NULL;
    while (eviction_frame == NULL)
    {
        struct single_frame_entry *f = get_frame_at_index(clock_pointer);
        if(f != NULL){
            //Check if the frame is accessed
            
            if(pagedir_is_accessed(f->holder->pagedir, f->page->user_virtual_address)){
                //Set this frame to not accessed and move to the next frame
                f->page->accessed = false;
                pagedir_set_accessed (f->holder->pagedir, f->page->user_virtual_address, false);
                //Increment the clock pointer
                increment_clock_pointer();
                continue;
            }
            if(!pagedir_is_accessed(f->holder->pagedir, f->page->user_virtual_address)){
                //Check if the frame is dirty
                if(pagedir_is_dirty(f->holder->pagedir, f->page->user_virtual_address)){
                    //Write the changes
                    
                    //Set this frame to not dirty
                    f->page->dirty = false;
                    pagedir_set_dirty (f->holder->pagedir, f->page->user_virtual_address, false);
                    eviction_frame = f;
                    break;
                }
                if(!pagedir_is_dirty(f->holder->pagedir, f->page->user_virtual_address)){
                    //This frame is not dirty and not accessed
                    //Evict this frame
                    eviction_frame = f;
                    break;
                }
            }
        }
    }
    return eviction_frame;
}
