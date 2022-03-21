#include "vm/frame.h"
#include "vm/page.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "devices/timer.h"
#include "threads/vaddr.h"
#include "stdio.h"

static int clock_pointer = 0;
static struct semaphore frame_sema;

void
frame_table_init(void)
{
    //Initialize frame table list
    list_init(&frame_table);
    sema_init(&frame_sema, 1);
}

struct single_frame_entry*
frame_add(enum palloc_flags flags, uint8_t *user_virtual_address, bool writable, enum create_sup_page_entry should_create_sup_page_entry) 
{
    sema_down(&frame_sema);
    struct single_frame_entry *frame = (struct single_frame_entry*) malloc(sizeof(struct single_frame_entry));
    if (frame == NULL) {
        sema_up(&frame_sema);
        return NULL;
    }

    uint8_t *page_addr = palloc_get_page (flags);

    if (page_addr == NULL)
    {
        // printf("NEED TO SWAP\n");
        struct single_frame_entry *replacer_frame = frame_evict();
        
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
        free(frame);
        sema_up(&frame_sema);
        return replacer_frame;
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
    sema_up(&frame_sema);
    return frame;
}

void
frame_remove(struct single_frame_entry *frame)
{
    if(frame != NULL){
        list_remove(&frame->frame_elem);
        if (is_user_vaddr(frame->page->user_virtual_address))
            pagedir_clear_page (frame->holder->pagedir, frame->page->user_virtual_address);
        
        palloc_free_page(frame->frame_address);
        free(frame);
    }
}

struct single_frame_entry*
frame_evict(void)
{
    struct single_frame_entry *to_evict = approximate_LRU();
    to_evict->page->page_flag = FROM_SWAPPED;

    pagedir_clear_page(to_evict->holder->pagedir, to_evict->page->user_virtual_address);
    block_read_write(to_evict, 0, WRITE);
    
    return to_evict;
}

//Given a list get the item at that index
struct single_frame_entry*
get_frame_at_index(int index)
{
    if((size_t)index >= list_size(&frame_table)) return NULL;
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
increment_clock_pointer(void)
{
    clock_pointer++;
    if((size_t)clock_pointer >= list_size(&frame_table)){
        clock_pointer = 0;
    }
}

struct single_frame_entry* 
approximate_LRU(void)
{
    // int random_frame_number = timer_ticks() % list_size(&frame_table);
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
                pagedir_set_accessed (f->holder->pagedir, f->page->user_virtual_address, false);
                //Increment the clock pointer
                increment_clock_pointer();
                continue;
            }
            if(!pagedir_is_accessed(f->holder->pagedir, f->page->user_virtual_address)){
                //Check if the frame is dirty
                if(pagedir_is_dirty(f->holder->pagedir, f->page->user_virtual_address)){
                    //Write the changes

                    sema_down(&file_modification_sema);
                    file_write_at(f->page->pg_data.file, f->frame_address, f->page->pg_data.read_bytes, f->page->pg_data.ofs);
                    sema_up(&file_modification_sema);

                    //Set this frame to not dirty
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
