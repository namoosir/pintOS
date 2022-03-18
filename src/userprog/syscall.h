#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void exit (int status);

struct semaphore file_read_sema; /* semaphore for file read */

#endif /* userprog/syscall.h */
