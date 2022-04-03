#include "cache.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "lib/string.h"
#include <stdio.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/syscall.h"

#define MAX_CACHE_SIZE 64
#define LOWEST_ADDR ((void *) 0x08048000)

static struct cache_entry cache[MAX_CACHE_SIZE];
static int cache_clock_pointer = 0;
static int used_cache_size;


struct inode_disk
  {
    block_sector_t **blocks;
    // block_sector_t *indirect_blocks;
    // block_sector_t **double_indirect_blocks;
    // block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };


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
cache_lookup(block_sector_t sector)
{
    sema_down(&buffer_cache_sema);
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        if (cache[i].sector == sector)
        {
            sema_up(&buffer_cache_sema);
            // printf("up 44\n");
            return true;
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
cache_evict(void)
{
    //Evict the cache entry using the clock algorithm
    sema_down(&buffer_cache_sema);
    // printf("down 105\n");

    while(cache[cache_clock_pointer].accessed == 1)
    {
        cache[cache_clock_pointer].accessed = 0;
        cache_clock_pointer = (cache_clock_pointer + 1) % MAX_CACHE_SIZE;
    }
    if(cache[cache_clock_pointer].dirty)
    {
        block_write(fs_device, cache[cache_clock_pointer].sector, cache[cache_clock_pointer].bounce_buffer);
        cache[cache_clock_pointer].dirty = 0;
    }
    memset(cache[cache_clock_pointer].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
    cache[cache_clock_pointer].in_use = 0;
    cache[cache_clock_pointer].sector = -1;

    sema_up(&buffer_cache_sema);
}

void
cache_add(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag)
{
    printf("\nCACHE ADD SECTOR::%u, write %d\n", sector, flag);
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
        cache_evict();
        used_cache_size--;
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
                
                // struct thread *cur = thread_current();
                // if (strcmp(thread_current()->name, "read_ahead_thread") != 0)
                // {
                //     used_cache_size++;
                //     sema_up(&buffer_cache_sema);
                //     cache_perform_read_ahead(sector);
                //     return;
                //     // sema_down(&buffer_cache_sema);
                // }
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

void
cache_read_ahead(void *next_block)
{
    block_sector_t next_block_read_ahead = *(block_sector_t*)next_block;
    char buffer[BLOCK_SECTOR_SIZE];
    if (!cache_lookup(next_block_read_ahead)) 
        cache_add(next_block_read_ahead, buffer, 0, 0, BLOCK_SECTOR_SIZE, CACHE_READ);
    free(next_block);
    thread_exit();
}

void
cache_perform_read_ahead(block_sector_t sector)
{
    block_sector_t *next_block = (block_sector_t*)malloc(sizeof(block_sector_t));
    *next_block = sector+1;
    thread_create("read_ahead_thread", PRI_DEFAULT, cache_read_ahead, (void*)next_block);
}