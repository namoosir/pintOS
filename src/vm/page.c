#include "page.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "devices/timer.h"

bool
page_table_hash_comparator(const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED) 
{
    const struct supplemental_page_entry *page_entry_a;
    const struct supplemental_page_entry *page_entry_b;

    page_entry_a = hash_entry (a, struct supplemental_page_entry, supplemental_page_elem);
    page_entry_b = hash_entry (b, struct supplemental_page_entry, supplemental_page_elem);

    return page_entry_a->user_virtual_address < page_entry_b->user_virtual_address;
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) 
{
  const struct supplemental_page_entry *p = hash_entry (p_, struct supplemental_page_entry, supplemental_page_elem);
  return hash_bytes (&p->user_virtual_address, sizeof p->user_virtual_address);
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct supplemental_page_entry *
page_lookup (void *address)
{
  struct supplemental_page_entry p;
  struct hash_elem *e;

  p.user_virtual_address = address;
  e = hash_find (&thread_current()->supplemental_page_hash_table, &p.supplemental_page_elem);
  return e != NULL ? hash_entry (e, struct supplemental_page_entry, supplemental_page_elem) : NULL;
}

struct supplemental_page_entry* 
new_supplemental_page_entry(uint8_t* user_virtual_address, bool writable) 
{
    struct supplemental_page_entry *s = (struct supplemental_page_entry*)malloc(sizeof(struct supplemental_page_entry));
    s->user_virtual_address = user_virtual_address;
    s->time_accessed = timer_ticks();
    s->dirty = pagedir_is_dirty(thread_current()->pagedir, user_virtual_address);
    s->accessed = pagedir_is_accessed(thread_current()->pagedir, user_virtual_address);
    s->writable = writable;

    struct thread *curr = thread_current();
    if (NULL != hash_insert(&(curr->supplemental_page_hash_table), &s->supplemental_page_elem)) 
    {
        hash_replace(&(curr->supplemental_page_hash_table), &s->supplemental_page_elem);
    }
    return s;
}


