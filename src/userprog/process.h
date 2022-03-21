#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct semaphore executable_list_sema;

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void populate_stack (void **esp, const char *file_name);
char *trimString(char *str);
bool install_page (void *upage, void *kpage, bool writable);
#endif /* userprog/process.h */
