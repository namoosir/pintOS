#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdint.h>
#include <lib/kernel/list.h>
#include <lib/kernel/hash.h>
#include "filesys/file.h"

void syscall_init (void);
void exit (int status);
void perform_munmap(int map_id);
void free_pages_in_hash (struct hash_elem *e, void *aux);
int add_file_descriptors(struct file* open_file);

struct semaphore file_modification_sema;

struct mapping_information
{
    uint8_t *start_addr;
    uint8_t *end_addr;
    int mapping_id;
    struct file *file;
    int file_size;
    struct list_elem map_elem;
};
#endif /* userprog/syscall.h */
