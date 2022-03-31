#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/directory.h"

struct cache_entry
{
    uint8_t bounce_buffer[BLOCK_SECTOR_SIZE];
    block_sector_t sector;
    int accessed;
    int in_use;
    int dirty;
};

enum add_flag
{
    CACHE_READ,
    CACHE_WRITE
};

void cache_init(void);
void cache_retrieve(block_sector_t sector, uint8_t *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size);
bool cache_lookup(block_sector_t sector);
void cache_add(block_sector_t sector, uint8_t *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag);

#endif