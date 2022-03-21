#include "devices/block.h"
#include "vm/swap.h"
#include "vm/frame.h"
#include "lib/kernel/bitmap.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "stdio.h"

static struct block* swap_block;
static struct bitmap* occupied_swap_bitmap;
static struct semaphore read_write_sema;

void 
swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    occupied_swap_bitmap = bitmap_create( block_size(swap_block) );
    sema_init(&read_write_sema, 1);
    bitmap_set_all(occupied_swap_bitmap, false);
}

void
read_write_from_block(struct single_frame_entry* frame, int index, enum read_or_write_flag rw_flag)
{
    sema_down(&read_write_sema);
    if (rw_flag == WRITE)
    {
        index = bitmap_scan(occupied_swap_bitmap, 0, 8, false);
        frame->page->block_index = index;
    }

    for (int i = 0; i < 8; i++)
    {
        if (rw_flag == READ)
        {
            block_read(swap_block, index + i, frame->frame_address + (i*BLOCK_SECTOR_SIZE));
            bitmap_set(occupied_swap_bitmap, index + i, false);
        }
        else
        {
            block_write(swap_block, index + i, frame->frame_address + (i*BLOCK_SECTOR_SIZE));
            bitmap_set(occupied_swap_bitmap, index + i, true);
        }
    }
    sema_up(&read_write_sema);
}