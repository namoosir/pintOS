#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/directory.h"
#include "threads/synch.h"

struct cache_entry
{
    uint8_t bounce_buffer[BLOCK_SECTOR_SIZE];
    block_sector_t sector;
    int accessed;
    int in_use;
    int dirty;
    struct semaphore cache_entry_sema;
};

enum add_flag
{
    CACHE_READ,
    CACHE_WRITE
};

struct semaphore buffer_cache_sema;
struct semaphore read_ahead_sema;

void cache_init(void);
void cache_retrieve(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size);
bool cache_lookup(block_sector_t sector);
void cache_add(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag);
void cache_write_back_all(void);
void cache_evict(void);
void cache_perform_read_ahead(block_sector_t sector);
void cache_read_ahead(void *next_block);
#endif