#include "page.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"

bool
page_table_hash_comparator(const struct hash_elem *a, const struct hash_elem *b, void* aux) {
    if (aux == NULL) {
        const struct supplemental_page_entry *page_entry_a;
        const struct supplemental_page_entry *page_entry_b;

        page_entry_a = hash_entry (a, struct supplemental_page_entry, supplemental_page_elem);
        page_entry_b = hash_entry (b, struct supplemental_page_entry, supplemental_page_elem);

        return page_entry_a->user_virtual_address < page_entry_b->user_virtual_address;
    }
    return NULL;
}

struct supplemental_page_entry* 
new_supplemental_page_entry(uint32_t* user_virtual_address) {
    struct supplemental_page_entry *s = (struct supplemental_page_entry*)malloc(sizeof(struct supplemental_page_entry));
    s->user_virtual_address = user_virtual_address;
    s->time_accessed = 0;
    s->dirty = false;
    s->accessed = false;

    // hash_insert(&(thread_current()->supplemental_page_hash_table), &s->supplemental_page_elem);
    return s;
}
