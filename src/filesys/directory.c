#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"

#define MAX_SUB_DIRS 30

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

bool dir_is_inode_removed(struct dir* dir); /*Function to check if inode for dir is removed*/


/* 
  Split path by "/" and store in array
  This algorithm is partially based off of:
  https://www.tutorialspoint.com/c_standard_library/c_function_strtok.htm
 */
char **
parse_path (const char *path)
{
  size_t path_len = strlen(path);
  if (path_len == 0) return NULL;
  if (path_len > (NAME_MAX+1)*MAX_SUB_DIRS) return NULL;
  
  char **path_array = (char**)malloc (MAX_SUB_DIRS*sizeof(char *));


  if (path_array == NULL)
  {
    return NULL;
  }
  
  for (size_t i = 0; i < MAX_SUB_DIRS; i++)
  {
    path_array[i] = (char*)malloc(NAME_MAX*sizeof(char*));
    memset(path_array[i], 0, NAME_MAX);
  }
  
  int i = 0;
  if(path[0] == '/')
  {
    path_array[0][0] = '/';
    i++;
  }
  
  char *token;  
  char *path_copy = malloc (NAME_MAX + 1);
  if (path_copy == NULL)
  {
    return NULL;
  }
  strlcpy (path_copy, path, NAME_MAX + 1);
  token = strtok_r (path_copy, "/", &path_copy);
  while (token != NULL)
  {
    path_array[i] = token;
    i++;
    token = strtok_r (NULL, "/", &path_copy);
  }
  return path_array;
}

/*
  Opens and returns the next directory with name next_dir_name if 
  it exists in the start_dir.
*/
struct dir*
dir_next_in_path(struct dir* start_dir, char* next_dir_name, struct inode* dir_inode)
{
  bool success = dir_lookup(start_dir, next_dir_name, &dir_inode);
  if(!success)
  {
    dir_close(start_dir);
    return NULL;
  }
  else
  {
    struct dir* new_dir = dir_open(dir_inode);
    dir_close(start_dir);
    return new_dir;
  }
}

/*
  Goes through each name in the path_array and opens that directory.
  For .. we open the parent dir
  For . we dont open any new dir and contiure the traversal with the next 
  name in the path_array.
*/
struct dir*
dir_traverse(struct dir* start_dir, char** path_array, struct inode* dir_inode)
{
  int i = 0;

  if (path_array[0][0] == '/') i++;
  
  while((int)path_array[i][0] != 0)
    {
      if (strcmp(path_array[i], "..") == 0)
      {
        start_dir = get_parent_dir(start_dir);
      }
      else if (strcmp(path_array[i], ".") == 0)
      {
        i++;
        continue;
      }
      else start_dir = dir_next_in_path(start_dir, path_array[i], dir_inode);

      if(start_dir == NULL)
      {
        return NULL;
      }
      i++;
    }
    return start_dir;
}

/*
  Opens the directory at the given file.
  Checks if the path is absolute or relative and 
  starts the traversal from the root for absolute paths 
  and current working dir for relative paths.
*/
struct dir*
dir_path_open(char **path_array)
{
  if(path_array == NULL)
    return NULL;

  // char **path_array = parse_path(path);
  struct dir* start_dir = NULL;
  struct inode* dir_inode = NULL;
  // Open absolute path
  if(path_array[0][0] == '/')
  {
    start_dir = dir_open_root();
    start_dir = dir_traverse(start_dir, path_array, dir_inode);
  }
  // Open relative path
  else
  {
    start_dir = thread_current()->current_dir;
    start_dir = dir_traverse(dir_reopen(start_dir), path_array, dir_inode);
  }

  return start_dir;
}

/*
  Checks if the dir is removed or not.
*/
bool
dir_is_inode_removed(struct dir* dir)
{
  struct inode* inode = dir_get_inode(dir);
  if(inode == NULL)
  {
    return true;
  }
  else
  {
    return inode_is_removed(inode);
  }
}



/*
  Unimplemented: Tried to free the path array
*/
void
free_path_array(char **path_array)
{
  path_array = NULL; // Disabled function
  if(false)
  {
    free(path_array);
  }
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), false);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  // printf("DIR ADD: %s\n", e.name);
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  inode_increment_containing_dirs(dir->inode);

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
