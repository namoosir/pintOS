#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define MAX_INDIRECT_BLOCKS 125
#define NUMBER_BLOCKS 12

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t blocks[NUMBER_BLOCKS];
    // block_sector_t start;            /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[114];               /* Not used. */    
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
  {
    off_t index = pos/BLOCK_SECTOR_SIZE;
    if (index < 10) return inode->data.blocks[index];
    else if (index < MAX_INDIRECT_BLOCKS + 10)
    {
      size_t level_one_index = index - 10;
      return ((block_sector_t *)inode->data.blocks[10])[level_one_index];
    } 
    else
    {
      size_t level_one_index = (index - 10 - MAX_INDIRECT_BLOCKS) / MAX_INDIRECT_BLOCKS;
      size_t level_two_index = (index - 10 - MAX_INDIRECT_BLOCKS) % MAX_INDIRECT_BLOCKS;
      return ((block_sector_t *)((block_sector_t *)inode->data.blocks[11])[level_one_index])[level_two_index];
    }
  }
  else
    return -1;
  // if (pos < inode->data.length)
  //   return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  // else
  //   return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      for (int i = 0; i < NUMBER_BLOCKS; i++)
      {
        if (i < 10) disk_inode->blocks[i] = -1;
        else if (i == 10)
        {
          block_sector_t indirect_blocks[MAX_INDIRECT_BLOCKS];

          for (int j = 0; j < MAX_INDIRECT_BLOCKS; j++)
            indirect_blocks[j] = -1;

          disk_inode->blocks[i] = (block_sector_t)indirect_blocks;
        }
        else
        {
          block_sector_t *level_one[MAX_INDIRECT_BLOCKS];

          for (int j = 0; j < MAX_INDIRECT_BLOCKS; j++)
          {
            block_sector_t level_two[MAX_INDIRECT_BLOCKS];

            for (int k = 0; k < MAX_INDIRECT_BLOCKS; k++)
              level_two[k] = -1;

            level_one[j] = level_two;            
          }

          disk_inode->blocks[i] = (block_sector_t)level_one;
        }
      }

      size_t occupied = 0;
      static char zeros[BLOCK_SECTOR_SIZE];

      while (occupied < sectors)
      {
        if (occupied < 10)
        {
          if (free_map_allocate (1, &disk_inode->blocks[occupied]))
          {
            cache_add(disk_inode->blocks[occupied], zeros, 0, 0, BLOCK_SECTOR_SIZE, CACHE_WRITE);
          }
          else break;
        }
        else if (occupied < 10 + MAX_INDIRECT_BLOCKS)
        {
          size_t indirect_index = occupied - 10;

          if (free_map_allocate (1, &((block_sector_t *)disk_inode->blocks[10])[indirect_index]))
          {
            cache_add(((block_sector_t *)disk_inode->blocks[10])[indirect_index], zeros, 0, 0, BLOCK_SECTOR_SIZE, CACHE_WRITE);
          }
          else break;
        }
        else 
        {
          size_t level_one_index = (occupied - 10 - MAX_INDIRECT_BLOCKS) / MAX_INDIRECT_BLOCKS;
          size_t level_two_index = (occupied - 10 - MAX_INDIRECT_BLOCKS) % MAX_INDIRECT_BLOCKS;

          if (free_map_allocate (1, &((block_sector_t *)((block_sector_t *)disk_inode->blocks[11])[level_one_index])[level_two_index]))
          {
            cache_add(((block_sector_t *)((block_sector_t *)disk_inode->blocks[11])[level_one_index])[level_two_index], zeros, 0, 0, BLOCK_SECTOR_SIZE, CACHE_WRITE);
          }
          else break;

        }
        occupied++;
      }

      if (occupied < sectors) 
      {
        free(disk_inode);
        return false;
      }
      
      cache_add(sector, disk_inode, 0, 0, BLOCK_SECTOR_SIZE, CACHE_WRITE);
      // block_write(fs_device, sector, disk_inode);
      free(disk_inode);
      return true;

    //   if (free_map_allocate (sectors, &disk_inode->start)) 
    //     {
    //       cache_add(sector, disk_inode, 0, 0, BLOCK_SECTOR_SIZE, CACHE_WRITE);
    //       // block_write (fs_device, sector, disk_inode);
    //       if (sectors > 0) 
    //         {
    //           static char zeros[BLOCK_SECTOR_SIZE];
    //           size_t i;
              
    //           for (i = 0; i < sectors; i++) 
    //           {
    //             cache_add(disk_inode->start + i, zeros, 0, 0, BLOCK_SECTOR_SIZE, CACHE_WRITE);
    //             // block_write (fs_device, disk_inode->start + i, zeros);
    //           }
    //         }
    //       success = true; 
    //     } 
    //   free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  // block_read (fs_device, inode->sector, &inode->data);
  if (cache_lookup(inode->sector)) 
    cache_retrieve(inode->sector, &inode->data, 0, 0, BLOCK_SECTOR_SIZE);
  else
    cache_add(inode->sector, &inode->data, 0, 0, BLOCK_SECTOR_SIZE, CACHE_READ);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
      //     free_map_release (inode->data.start,
      //                       bytes_to_sectors (inode->data.length)); 
          for (size_t i = 0; i < bytes_to_sectors(inode->data.length); i++)
          {
            if (i < 10) free_map_release(inode->data.blocks[i], 1);
            else if (i < MAX_INDIRECT_BLOCKS + 10) free_map_release(((block_sector_t *)inode->data.blocks[10])[i - 10], 1);
            else 
            {
              size_t level_one_index = (i - 10 - MAX_INDIRECT_BLOCKS) / MAX_INDIRECT_BLOCKS;
              size_t level_two_index = (i - 10 - MAX_INDIRECT_BLOCKS) % MAX_INDIRECT_BLOCKS;
              free_map_release(((block_sector_t *)((block_sector_t *)inode->data.blocks[11])[level_one_index])[level_two_index], 1);
            }
              
          }
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  // uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
          /* Read full sector directly into caller's buffer. */
          if (cache_lookup(sector_idx)) 
            cache_retrieve(sector_idx, buffer, bytes_read, sector_ofs, chunk_size);
          else
            cache_add(sector_idx, buffer, bytes_read, sector_ofs, chunk_size, CACHE_READ);
          // block_read (fs_device, sector_idx, buffer + bytes_read); // cache_add(sector_idx, buffer, bytes_read, READ);
        // }
      // else 
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     block_read (fs_device, sector_idx, bounce); // cache_add(sector_idx, bounce, 0, READ);
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //   }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  // free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  // uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
          /* Write full sector directly to disk. */
          // if (cache_lookup(sector_idx)) 
            // cache_retrieve(sector_idx, (uint8_t *)buffer, bytes_written, sector_ofs, chunk_size);
          // else
            cache_add(sector_idx, (uint8_t *)buffer, bytes_written, sector_ofs, chunk_size, CACHE_WRITE);
        //   block_write (fs_device, sector_idx, buffer + bytes_written); // cache_add(sector_idx, buffer, bytes_read, WRITE);
        // }
      // else 
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL) 
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left) 
      //       block_read (fs_device, sector_idx, bounce); // cache_add(sector_idx, bounc, 0, READ);
      //     else
      //       memset (bounce, 0, BLOCK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     block_write (fs_device, sector_idx, bounce);//cache_write(sector_idx, bounce, 0, WRITE);
      //   }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  // free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
