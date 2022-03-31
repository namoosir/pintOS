#include "cache.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/string.h"

#define MAX_CACHE_SIZE 64

static struct cache_entry cache[MAX_CACHE_SIZE];
static struct semaphore buffer_cache_sema;
static int used_cache_size;

void 
cache_init(void)
{
    used_cache_size = 0;
    sema_init(&buffer_cache_sema, 1);
    for(int i = 0; i < MAX_CACHE_SIZE; i++){
        cache[i].sector = -1;
        memset(&cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
    }
    
}

/* 
    Return true if sector has been added to cache
    and false otherwise.
*/
//TODO: might need to modify this to take read/write flag
//      and if it is a write flag then compare the actual 
//      buffer contents with sent in buffer and return true 
//      if the contents are the same 
bool 
cache_lookup(block_sector_t sector)
{
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        if (cache[i].sector == sector)
        {
            return true;
        }
    }
    return false;
}

void
cache_retrieve(block_sector_t sector, uint8_t *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size)
{
    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].sector == sector) 
        {
            sema_down(&buffer_cache_sema);
            memcpy(buffer + bytes_read_or_write, cache[i].bounce_buffer + sector_ofs, chunk_size);
            sema_up(&buffer_cache_sema);
            break;
        }
    }
}

// struct cache_entry*
// cache_evict()
// {

// }

void 
cache_add(block_sector_t sector, uint8_t *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag)
{
    static int x;
    if (x == 63) x = 0;

    if (used_cache_size == MAX_CACHE_SIZE) 
    {
        //eviction stuff
        sema_down(&buffer_cache_sema);

        cache[x].in_use = 0;
        block_write (fs_device, cache[x].sector, &cache[x].bounce_buffer);

        memset(&cache[x].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
        used_cache_size--;
        x++;
        sema_up(&buffer_cache_sema);
    }

    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].sector == sector && flag == CACHE_WRITE)
        {
            sema_down(&buffer_cache_sema);

            cache[i].accessed = 1;
            cache[i].dirty = 1;

            block_read(fs_device, sector, &cache[i].bounce_buffer);
            memcpy(&cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);
            block_write (fs_device, sector, &cache[i].bounce_buffer + sector_ofs);
            sema_up(&buffer_cache_sema);
            return;
        }
    }

    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].in_use == 0)
        {
            sema_down(&buffer_cache_sema);

            cache[i].sector = sector;
            cache[i].in_use = 1;
            cache[i].accessed = 1;

            if(flag == CACHE_READ)
            {
                cache[i].dirty = 0;
                block_read(fs_device, sector, &cache[i].bounce_buffer);
                memcpy(buffer + bytes_read_or_write, &cache[i].bounce_buffer + sector_ofs, chunk_size);
            }
            else if (flag == CACHE_WRITE)
            {
                cache[i].dirty = 1;
                block_read(fs_device, sector, &cache[i].bounce_buffer);
                memcpy(&cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);
                block_write (fs_device, sector, &cache[i].bounce_buffer + sector_ofs);
            }
            used_cache_size++;
            sema_up(&buffer_cache_sema);

            break;
        }
    }
    // sema_up(&buffer_cache_sema);
}


