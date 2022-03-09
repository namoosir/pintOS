#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"

// List of all frames
struct list frame_table;

//Single frame table entry
struct single_frame_elem {
    void *frame;
    struct thread *holder;
    struct single_page_elem *page;
    struct list_elem frame_elem;
};

#endif /* vm/frame.h */
