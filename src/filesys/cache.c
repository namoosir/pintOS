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

static struct cache_entry cache[MAX_CACHE_SIZE]; /* Cache array */
static int cache_clock_pointer = 0; /* Clock pointer for clock algorithm */
static int used_cache_size; /* The number of items used in the cache. */

/* 
    Initializes all the entries in the cache.
*/
void 
cache_init(void)
{
    used_cache_size = 0;
    sema_init(&buffer_cache_sema, 1);
    sema_init(&read_ahead_sema, 1);
    for(int i = 0; i < MAX_CACHE_SIZE; i++){
        cache[i].sector = -1;
        memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
        sema_init(&cache[i].cache_entry_read_sema, 1);
        sema_init(&cache[i].cache_entry_write_sema, 1);
        sema_init(&cache[i].cache_entry_modification_sema, 1);
    }
    
}

/* 
    Return true if sector has been added to cache
    and false otherwise.
*/
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

/*
    Retrives the contents in the cache entry based on the sector.
*/
void
cache_retrieve(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size)
{
    sema_down(&buffer_cache_sema);
    
    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        if (cache[i].sector == sector) 
        {
            memcpy(buffer + bytes_read_or_write, cache[i].bounce_buffer + sector_ofs, chunk_size);
            break;
        }
    }
    sema_up(&buffer_cache_sema);
}

/*
    Used to write back all dirty blocks from the cache to the block 
    at the end when closing the file system.
*/
void
cache_write_back_all(void)
{
    sema_down(&buffer_cache_sema);

    for(int i = 0; i < MAX_CACHE_SIZE; i++)
    {

        if (cache[i].dirty)
        {
            block_write(fs_device, cache[i].sector, cache[i].bounce_buffer);
        }
        memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
        cache[i].in_use = 0;
        cache[i].accessed = 0;
        cache[i].dirty = 0;
        cache[i].sector = -1;

    }
    sema_up(&buffer_cache_sema);
}

void
cache_evict(void)
{
    //Evict the cache entry using the clock algorithm
    sema_down(&buffer_cache_sema);

    /*
        Uses the clock algorithm to find a spot to evict.
    */
    while(cache[cache_clock_pointer].accessed == 1)
    {
        cache[cache_clock_pointer].accessed = 0;
        cache_clock_pointer = (cache_clock_pointer + 1) % MAX_CACHE_SIZE;
    }

    /*
        Check if any of the semaphores for the block is being used.
        If not, we should down the semaphore so nothing else can access the block 
        when evicting.
    */
    if(cache[cache_clock_pointer].cache_entry_modification_sema.value != 0)
    {
        sema_down(&cache[cache_clock_pointer].cache_entry_modification_sema);
    }
    if(cache[cache_clock_pointer].cache_entry_write_sema.value != 0)
    {
        sema_down(&cache[cache_clock_pointer].cache_entry_write_sema);
    }
        if(cache[cache_clock_pointer].cache_entry_read_sema.value != 0)
    {
        sema_down(&cache[cache_clock_pointer].cache_entry_read_sema);
    }
    /*
        Writes back the evicted item to the block sector.
    */
    block_write(fs_device, cache[cache_clock_pointer].sector, cache[cache_clock_pointer].bounce_buffer);
    cache[cache_clock_pointer].dirty = 0;
    memset(cache[cache_clock_pointer].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
    cache[cache_clock_pointer].in_use = 0;
    cache[cache_clock_pointer].sector = -1;

    if(cache[cache_clock_pointer].cache_entry_modification_sema.value == 0)
    {
        sema_up(&cache[cache_clock_pointer].cache_entry_modification_sema);
    }
    if(cache[cache_clock_pointer].cache_entry_write_sema.value == 0)
    {
        sema_up(&cache[cache_clock_pointer].cache_entry_write_sema);
    }
        if(cache[cache_clock_pointer].cache_entry_read_sema.value == 0)
    {
        sema_up(&cache[cache_clock_pointer].cache_entry_read_sema);
    }

    sema_up(&buffer_cache_sema);
}

/*
    Adds block sectors to the cache.
    This function taks in a flag to know if it needs to read or write.
*/

void
cache_add(block_sector_t sector, void *buffer, int32_t bytes_read_or_write, int sector_ofs, int chunk_size, enum add_flag flag)
{
    if (sector > 4096) exit(-1);
    for (int i = 0; i < MAX_CACHE_SIZE; i++) 
    {
        /*
            If the sector is already being used in the cache, then we just update its bounce buffer.
        */
        if (cache[i].sector == sector && flag == CACHE_WRITE && cache[i].in_use == 1)
        {
            sema_down(&buffer_cache_sema);
            sema_down(&cache[i].cache_entry_write_sema);

            // printf("down 111\n");

            cache[i].accessed = 1;
            cache[i].dirty = 1;
            
            if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
            {
                memcpy(cache[i].bounce_buffer, buffer + bytes_read_or_write, chunk_size);   
            }
            else
            {
                int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

                if (!(sector_ofs > 0 || chunk_size < sector_left))
                    memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
                memcpy(cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);
            }
            
            sema_up(&cache[i].cache_entry_write_sema);
            sema_up(&buffer_cache_sema);
            return;
        }
    }

    /* 
        Perform eviction is the cache is full.
    */
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
            sema_down(&cache[i].cache_entry_modification_sema);
            cache[i].sector = sector;
            cache[i].in_use = 1;
            cache[i].accessed = 1;
            sema_up(&cache[i].cache_entry_modification_sema);

            /*
                Reads from the block sector into the cache.
            */
            if(flag == CACHE_READ)
            {
                
                if(cache[i].cache_entry_write_sema.value == 0)
                {
                    sema_down(&cache[i].cache_entry_read_sema);
                }
                block_read(fs_device, sector, cache[i].bounce_buffer);
                memcpy(buffer + bytes_read_or_write, cache[i].bounce_buffer + sector_ofs, chunk_size);
                
                if(cache[i].cache_entry_read_sema.value == 0)
                {
                    sema_up(&cache[i].cache_entry_read_sema);
                }
            }
            /* Read the buffer into the bounce buffer inside the cache 
               This is write back beacuse we dont immedialty write our cache 
               contents to the disk. We wait till eviction.
            */
            else if (flag == CACHE_WRITE)
            {
                cache[i].dirty = 1;

                sema_down(&cache[i].cache_entry_write_sema);
                if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
                {
                    memcpy(cache[i].bounce_buffer, buffer + bytes_read_or_write, chunk_size);   
                }
                else
                {
                    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
                    if (sector_ofs > 0 || chunk_size < sector_left)
                        block_read(fs_device, sector, cache[i].bounce_buffer);
                    else
                        memset(cache[i].bounce_buffer, 0, BLOCK_SECTOR_SIZE);
                    memcpy(cache[i].bounce_buffer + sector_ofs, buffer + bytes_read_or_write, chunk_size);
                }
                sema_up(&cache[i].cache_entry_write_sema);

            }
            used_cache_size++;
            sema_up(&buffer_cache_sema);
            break;
        }
    }
}

/*
    Attempt at the read ahead.
    Uses cache add to read in the next block.
*/
void
cache_read_ahead(void *next_block)
{
    
    block_sector_t next_block_read_ahead = *(block_sector_t*)next_block;
    char buffer[BLOCK_SECTOR_SIZE];
    sema_down(&read_ahead_sema);
    if (!cache_lookup(next_block_read_ahead)){
        cache_add(next_block_read_ahead, buffer, 0, 0, BLOCK_SECTOR_SIZE, CACHE_READ);
    }else{
        sema_up(&buffer_cache_sema);
        free(next_block);
        sema_up(&read_ahead_sema);
        thread_exit();
    }
    free(next_block);
}

/*
    Spwans new thread to do read ahead.
*/
void
cache_perform_read_ahead(block_sector_t sector)
{
    block_sector_t *next_block = (block_sector_t*)malloc(sizeof(block_sector_t));
    *next_block = sector+1;
    thread_create("read_ahead", PRI_DEFAULT, cache_read_ahead, (void*)next_block);
}