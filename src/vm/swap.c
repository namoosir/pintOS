#include "devices/block.h"
#include "vm/swap.h"
#include "vm/frame.h"
#include "lib/kernel/bitmap.h"
#include "vm/page.h"

static struct block* swap_block;
static struct bitmap* occupied_swap_bitmap;

void 
swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    occupied_swap_bitmap = bitmap_create( block_size(swap_block) );
    bitmap_set_all(occupied_swap_bitmap, false);
}

void
read_write_from_block(struct single_frame_entry* frame, int index, enum read_or_write_flag rw_flag)
{
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
}