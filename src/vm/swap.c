#include "devices/block.h"
#include "vm/swap.h"
#include "vm/frame.h"

static struct block* swap_block;

void swap_init()
{
    swap_block = block_get_role(BLOCK_SWAP);
}

void
read_write_from_block(struct single_frame_entry* frame, int index, enum read_or_write_flag rw_flag)
{
    for (int i = 0; i < 8; i++)
    {
        if (rw_flag == READ)
        {
            block_read(swap_block, index + i, frame->frame_address + (i*BLOCK_SECTOR_SIZE));
        }
        else
        {
            block_write(swap_block, index + i, frame->frame_address + (i*BLOCK_SECTOR_SIZE));
        }
    }
}