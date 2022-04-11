#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
void file_grow(struct inode *inode, int grow_to_length);
bool inode_is_removed(struct inode *inode);
bool inode_is_file(struct inode *inode);
struct dir* get_parent_dir(struct dir* curr);
void save_parent_dir(struct dir* parent_dir, struct inode* current);
void inode_set_length(struct inode* inode, int length);
void inode_decrement_containing_dirs(struct inode* inode);
void inode_increment_containing_dirs(struct inode* inode);
int inode_get_containing_dirs(struct inode* inode);
void inode_initialize_containing_dirs(struct inode* inode);
void inode_sema_down (struct inode *inode);
void inode_sema_up (struct inode *inode);
int inode_sema_value (struct inode *inode);
#endif /* filesys/inode.h */
