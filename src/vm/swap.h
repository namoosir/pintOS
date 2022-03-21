#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/frame.h"

enum read_or_write_flag{
    READ,
    WRITE,
};

void block_read_write(struct single_frame_entry* frame, int index, enum read_or_write_flag rw_flag);
void swap_init(void);


#endif /* vm/swap.h */
