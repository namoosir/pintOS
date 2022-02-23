#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"

#define LOWEST_ADDR ((void *) 0x08048000)


static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  // 0xc0000000;3221225472; UPPER
  // 0xbfffff28;3221225256; CURRENT LOWER
  // 0x20101234;537924148; INPUT
  // 0x08048000;134512640; LOWER
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

/* Exit from the thread with status code status*/
static void
exit (int status)
{
  printf("%s: exit(%d)\n", thread_current()->name, status);
  
  //exit from the thread
  thread_exit ();
}

static void
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
  {
    if(is_kernel_vaddr((void*)usrc) || usrc == NULL || (void*)usrc <= LOWEST_ADDR)
      exit(-1);
    *dst = get_user (usrc);
  }
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_number;
  int args[3];
  
  //extract the syscall number
  copy_in (&syscall_number, f->esp, sizeof syscall_number);

  //TODO: OFFICE HOURS
  //pg_round_down
  if ((int)syscall_number < 0 || syscall_number > SYS_INUMBER) 
  {
    exit(-1);
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
    //set the returned value
    f->eax = args[0];

    exit(args[0]);
  }
  else if (syscall_number == SYS_WRITE)
  {
    //execute the write on STDOUT_FILENO
    putbuf ((const char*)args[1], args[2]);

    //set the returned value
    f->eax = args[2];
  }
  else if (syscall_number == SYS_CREATE)
  {
    //Print args
    // for(int i = 0; i < 3; i++)
    // {
    //   printf("args[%d]: %d\n", i, args[i]);
    // }

    //TODO: OFFICE HOURS
    //pagedir_get_page
    if ((const char*)args[0] == NULL || (int)args[1] < 0 || 
        (void*)args[0] <= LOWEST_ADDR || is_kernel_vaddr((void *)args[0]))
    {
      exit(-1);
    }
    // 0x804b290

    // if((void *)args[0] < f->esp)
    // {
    //   printf("address of file %d address of stack pointer %u\n", (void *)args[0], f->esp);
    //   exit(-1);
    // }

    //create the file
    bool success = filesys_create((const char*)args[0], args[1]);

    //set the returned value
    f->eax = success;
  }
  else if(syscall_number == SYS_REMOVE)
  {

    if ((const char*)args[0] == NULL || 
        (void*)args[0] <= LOWEST_ADDR || is_kernel_vaddr((void *)args[0]))
    {
      exit(-1);
    }

    //remove the file
    bool success = filesys_remove((const char*)args[0]);
    
    //set the returned value
    f->eax = success;
  }
  
  //TODO: complete this
  //Hashmap saves space
  //Array[128] idx is fd, content is pointer to file
  else if (syscall_number == SYS_OPEN)
  {
    // struct file* f = filesys_open()
  }
}
