#include "cache.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "lib/string.h"
#include <stdio.h>
#include "threads/vaddr.h"
#include "userprog/syscall.h"

#define MAX_CACHE_SIZE 64
#define LOWEST_ADDR ((void *) 0x08048000)

static struct cache_entry cache[MAX_CACHE_SIZE];

static int used_cache_size;

void 
cache_init(void)
{
    used_cache_size = 0;
    sema_init(&buffer_cache_sema, 1);
    for(int i = 0; i < MAX_CACHE_SIZE; i++){
        cache[i].sector = -1;
        memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
        sema_init(&cache[i].cache_entry_sema, 1);
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
cache_lookup(block_sector_t sector/*, uint8_t *buffer, enum add_flag flag*/)
{
    sema_down(&buffer_cache_sema);
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        if (cache[i].sector == sector)
        {
            // if (flag == CACHE_READ)
            sema_up(&buffer_cache_sema);
            // printf("up 44\n");
            return true;
            // if (memcmp(buffer, &cache[i].bounce_buffer, ))
        }
    }
    sema_up(&buffer_cache_sema);
    return false;
}

void
cache_retrieve(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size)
{
    sema_down(&buffer_cache_sema);
    // printf("down 57\n");
    
    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].sector == sector) 
        {
            // sema_down(&cache[i].cache_entry_sema);
            memcpy(buffer + bytes_read_or_write, cache[i].bounce_buffer + sector_ofs, chunk_size);
            // sema_up(&cache[i].cache_entry_sema);
            break;
        }
    }
    sema_up(&buffer_cache_sema);
    // printf("up 71\n");
}

// struct cache_entry*
// cache_evict()
// {

// }

void
cache_write_back_all(void)
{
    sema_down(&buffer_cache_sema);
    // printf("down 84\n");

    for(int i = 0; i < MAX_CACHE_SIZE; i++)
    {

        if (cache[i].dirty)
        {
            // sema_down(&file_modification_sema);
            block_write(fs_device, cache[i].sector, cache[i].bounce_buffer);
            // sema_up(&file_modification_sema);
        }
        memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
        cache[i].in_use = 0;
        cache[i].accessed = 0;
        cache[i].dirty = 0;
        cache[i].sector = -1;

    }
    sema_up(&buffer_cache_sema);
    // printf("up 100\n");
}

void
cache_add(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag)
{
    static int x;

    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].sector == sector && flag == CACHE_WRITE && cache[i].in_use == 1)
        {
            sema_down(&buffer_cache_sema);
            // printf("down 111\n");

            cache[i].accessed = 1;
            cache[i].dirty = 1;
            
            if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
            {
                memcpy(cache[i].bounce_buffer, buffer + bytes_read_or_write, chunk_size);   
                // sema_up(&buffer_cache_sema);
                // printf("up 123\n");
            }
            else
            {
                int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

                // sema_down(&buffer_cache_sema);

                if (!(sector_ofs > 0 || chunk_size < sector_left))
                    memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
                memcpy(cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);

                // sema_up(&buffer_cache_sema);
                // printf("up 135\n");
            }
            sema_up(&buffer_cache_sema);

            return;
        }
    }

    if (used_cache_size == MAX_CACHE_SIZE) 
    {
        //eviction stuff
        sema_down(&buffer_cache_sema);
        if (x == 63) x = 0;

        // printf("down 144\n");
        // sema_down(&cache[x].cache_entry_sema);

        cache[x].in_use = 0;
        
        // sema_down(&file_modification_sema);
        if (cache[x].dirty) block_write (fs_device, cache[x].sector, cache[x].bounce_buffer);
        // sema_up(&file_modification_sema);

        memset(cache[x].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
        cache[x].in_use = 0;
        cache[x].accessed = 0;
        cache[x].dirty = 0;
        cache[x].sector = -1;
        
        used_cache_size--;
        x++;
        sema_up(&buffer_cache_sema);
        // printf("Up 160\n");
        // sema_up(&cache[x].cache_entry_sema);
    }

    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].in_use == 0)
        {
            sema_down(&buffer_cache_sema);
            // printf("down 167\n");
            // sema_down(&cache[i].cache_entry_sema);

            cache[i].sector = sector;
            cache[i].in_use = 1;
            cache[i].accessed = 1;

            if(flag == CACHE_READ)
            {
                cache[i].dirty = 0;
                block_read(fs_device, sector, cache[i].bounce_buffer);
                memcpy(buffer + bytes_read_or_write, cache[i].bounce_buffer + sector_ofs, chunk_size);
            }
            else if (flag == CACHE_WRITE)
            {
                cache[i].dirty = 1;

                if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
                {
                    // sema_down(&buffer_cache_sema);
                    memcpy(cache[i].bounce_buffer, buffer + bytes_read_or_write, chunk_size);   
                    // sema_up(&buffer_cache_sema);
                }
                else
                {
                    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

                    // sema_down(&buffer_cache_sema);

                    if (sector_ofs > 0 || chunk_size < sector_left)
                        block_read(fs_device, sector, cache[i].bounce_buffer);
                    else
                        memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
                    memcpy(cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);

                    // sema_up(&buffer_cache_sema);
                }
                // block_write (fs_device, sector, &cache[i].bounce_buffer);
            }
            used_cache_size++;
            sema_up(&buffer_cache_sema);
            // printf("Up 208\n");
            // sema_up(&cache[i].cache_entry_sema);
            break;
        }
    }
}

// void 
// cache_add(block_sector_t sector, uint8_t *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag)
// {
//     static int x;

//     for (int i = 0; i < MAX_CACHE_SIZE; i++) 
//     {
//         if (cache[i].sector == sector && flag == CACHE_WRITE && cache[i].in_use == 1)
//         {
//             sema_down(&buffer_cache_sema);
//             // sema_down(&cache[i].cache_entry_sema);

//             cache[i].accessed = 1;
//             cache[i].dirty = 1;

//             block_read(fs_device, sector, &cache[i].bounce_buffer);
//             memcpy(&cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);
//             block_write (fs_device, sector, &cache[i].bounce_buffer + sector_ofs);
//             // block_write (fs_device, sector, &cache[i].bounce_buffer);
            
//             sema_up(&buffer_cache_sema);
//             // sema_up(&cache[i].cache_entry_sema);
//             return;
//         }
//     }

//     if (x == 63) x = 0;

//     if (used_cache_size == MAX_CACHE_SIZE) 
//     {
//         //eviction stuff
//         sema_down(&buffer_cache_sema);
//         // sema_down(&cache[x].cache_entry_sema);

//         cache[x].in_use = 0;
//         block_write (fs_device, cache[x].sector, &cache[x].bounce_buffer);

//         memset(&cache[x].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
//         used_cache_size--;
//         x++;
//         sema_up(&buffer_cache_sema);
//         // sema_up(&cache[x].cache_entry_sema);
//     }

//     for (int i = 0; i < MAX_CACHE_SIZE; i++) 
//     {
//         if (cache[i].in_use == 0)
//         {
//             sema_down(&buffer_cache_sema);
//             // sema_down(&cache[i].cache_entry_sema);

//             cache[i].sector = sector;
//             cache[i].in_use = 1;
//             cache[i].accessed = 1;

//             if(flag == CACHE_READ)
//             {
//                 cache[i].dirty = 0;
//                 block_read(fs_device, sector, &cache[i].bounce_buffer);
//                 memcpy(buffer + bytes_read_or_write, &cache[i].bounce_buffer + sector_ofs, chunk_size);
//             }
//             else if (flag == CACHE_WRITE)
//             {
//                 cache[i].dirty = 1;
//                 block_read(fs_device, sector, &cache[i].bounce_buffer);
//                 memcpy(&cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);
//                 block_write (fs_device, sector, &cache[i].bounce_buffer + sector_ofs);
//                 // block_write (fs_device, sector, &cache[i].bounce_buffer);

//             }
//             used_cache_size++;
//             sema_up(&buffer_cache_sema);
//             // sema_up(&cache[i].cache_entry_sema);

//             break;
//         }
//     }
//     // sema_up(&buffer_cache_sema);
// }


