#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/cache.h"
#include "threads/thread.h"
#include <string.h>

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();
  
  struct dir *root = dir_open_root();
  save_parent_dir(root, root->inode);
  inode_initialize_containing_dirs(root->inode);
  dir_close (root);

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_write_back_all();
  free_map_close ();
  
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, struct dir* dir, bool is_file) 
{
  // print all input parameters
  // printf("filesys_create: name: %s, initial_size: %d, dir: %p, is_file: %d\n", name, initial_size, dir, is_file);
  block_sector_t inode_sector = 0;
  // struct dir *dir = dir_open_root ();
  if(inode_sema_value(dir->inode) ==0)
    inode_sema_down(dir->inode);
  
  bool success = (dir != NULL
                  && !inode_is_removed(dir->inode)
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_file)
                  && dir_add (dir, name, inode_sector));
  if(inode_sema_value(dir->inode) !=0 )
    inode_sema_up(dir->inode);

  /*
    Once the file is created we initialize it and set the parent dir
  */
  if (success)
  {
    struct inode *inode = NULL;
    dir_lookup(dir, name, &inode);

    save_parent_dir(dir, inode);
    inode_initialize_containing_dirs(inode);
  }
  
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  // printf("before dir close\n");
  dir_close (dir);
  // printf("after dir close\n");

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name, struct dir* dir)
{
  if (strcmp(name, ".") == 0)
  {
    if (!inode_is_removed(dir->inode))
      return file_open (dir->inode);
    else
      return NULL;
  }
  if (strcmp(name, "..") == 0)
  {
    if (!inode_is_removed(dir->inode))
      return file_open (dir->inode);
    else
      return NULL;
  }
  struct inode *inode = NULL;
  // printf("filesys_open: %s\n", name);
  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  // Only open the file if its not removed and has an inode
  if (inode != NULL && !inode_is_removed(inode))
  {
    // printf("OPENED\n");
    return file_open (inode);
  }
    
  else
    return NULL;
}

struct file *
filesys_open_fsutil (const char *name)
{
  // printf("filesys_open_fsutil: %s\n", name);
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
}

bool
filesys_create_fsutil (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  // printf("filesys_create_fsutil: %s\n", name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, true)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

bool
filesys_remove_fsutil (const char *name) 
{

  struct dir *dir = dir_open_root ();
  // printf("filesys_remove_fsutil: %s\n", name);
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name, struct dir* dir) 
{
  //TODO: FIX these cases are not correct
  if (strcmp(name, ".") == 0 && !inode_is_removed(dir->inode))
  {
    if (dir != NULL && !inode_is_file(dir->inode) && inode_get_containing_dirs(dir->inode) == 0)
    {
      // int size = inode_length(dir->inode);
      struct dir *p_dir = get_parent_dir(dir);
      bool success = p_dir != NULL && dir_remove (p_dir, name);

      // inode_set_length(p_dir->inode, inode_length(p_dir->inode) - size);
      if (success) inode_decrement_containing_dirs(p_dir->inode);
      dir_close(dir);

      // dir_reopen(p_dir);
      // thread_current()->current_dir = NULL;
      if (thread_current()->current_dir->inode == dir->inode)
      {
        inode_remove(thread_current()->current_dir->inode);
      }
      return success;
    }
  }
  if (strcmp(name, "..") == 0)
  {
    // if (dir != NULL && get_parent_dir(dir) != NULL && !inode_is_file(get_parent_dir(dir)->inode) && inode_length(get_parent_dir(dir)->inode) == 0)
    // {
    //   struct dir *dir_to_remove = get_parent_dir(dir);
    //   struct dir *p_dir = get_parent_dir(dir_to_remove);
    //   int size = inode_length(dir_to_remove->inode);
    //   bool success = p_dir != NULL && dir_remove (p_dir, name);

    //   inode_set_length(p_dir->inode, inode_length(p_dir->inode) - size);
    //   dir_close(dir_to_remove);
    //   thread_current()->current_dir = p_dir;
    //   return success;
    // }
    return false;
  }
  // struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
  {
    dir_lookup(dir, name, &inode);
    
    if (inode != NULL) 
    {
      if ((inode_is_file(inode) || (!inode_is_file(inode) && inode_get_containing_dirs(inode) == 0)) && !inode_is_removed(inode))
      {
        bool success = dir_remove (dir, name);
        if (success) inode_decrement_containing_dirs(dir->inode);
        if (thread_current()->current_dir->inode == inode) 
        {
          inode_remove(thread_current()->current_dir->inode);
          // thread_current()->current_dir = dir_reopen(dir);
          // thread_current()->current_dir = NULL;
        }
        // else 
        dir_close (dir); 

        return success;
      }
    }
  }

  return false;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");

  // struct dir *root = dir_open_root();
  // save_parent_dir(root, root->inode);
  // inode_initialize_containing_dirs(root->inode);
  // dir_close (root);

  free_map_close ();
  printf ("done.\n");
}
