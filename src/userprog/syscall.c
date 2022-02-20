#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static void
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
    *dst = get_user (usrc);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_number;
  int args[3];

  // Handle invalid stack pointer 
  // TODO: check validity of the other pointers
  // TODO: dereference the stack pointers
  if (f->esp == NULL || is_kernel_vaddr(f->esp) || f->esp <= (void*)0x08048000
      // || is_kernel_vaddr(f->esp + 1) || f->esp + 1 <= (void*)0x08048000
      // || is_kernel_vaddr(f->esp + 2) || f->esp + 2 <= (void*)0x08048000
      // || is_kernel_vaddr(f->esp + 3) || f->esp + 3 <= (void*)0x08048000
      || is_kernel_vaddr((void *)((int *)f->esp + 1)) || (void *)((int *)f->esp + 1) <= (void *)0x08048000
      || is_kernel_vaddr((void *)((int *)f->esp + 2)) || (void *)((int *)f->esp + 2) <= (void *)0x08048000
      || is_kernel_vaddr((void *)((int *)f->esp + 3)) || (void *)((int *)f->esp + 3) <= (void *)0x08048000)
  {
    // printf("as;dlkfja;lkdfj;alkdjf;kad;lf\n");
    //print exit statement
    printf("%s: exit(%d)\n", thread_current()->name, -1);

    //exit from the thread
    thread_exit ();
  }

  //extract the syscall number
  copy_in (&syscall_number, f->esp, sizeof syscall_number);

  if (syscall_number < 0 || syscall_number > SYS_INUMBER) 
  {
    printf("%s: exit(%d)\n", thread_current()->name, -1);

    //exit from the thread
    thread_exit ();
  }

  //extract the 3 arguments
  copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 3);

  if(syscall_number == SYS_HALT)
  {
    //shut down the system
    shutdown_power_off();
  }
  else if (syscall_number == SYS_EXIT)
  {
    //print exit statement
    printf("%s: exit(%d)\n", thread_current()->name, args[0]);

    //set the returned value
    f->eax = args[0];

    //exit from the thread
    thread_exit ();    
  }
  else if (syscall_number == SYS_WRITE)
  {
    //execute the write on STDOUT_FILENO
    putbuf (args[1], args[2]);

    //set the returned value
    f->eax = args[2];
  }
  
}
