#include "devices/block.h"

static struct block* swap_block;

void swap_init()
{
    swap_block = block_get_role(BLOCK_SWAP);
}

void
read_write_from_block(uint8_t* frame, int index)
{
    for (int i = 0; i < 8; i++)
    {
        
    }
}