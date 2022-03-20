#ifndef VM_SWAP_H
#define VM_SWAP_H

enum read_or_write_flag{
    READ,
    WRITE,
};

void read_write_from_block(struct single_frame_entry* frame, int index, enum read_or_write_flag rw_flag);


#endif /* vm/swap.h */
