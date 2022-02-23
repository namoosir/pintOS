#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "pagedir.h"
#include "devices/input.h"

#define LOWEST_ADDR ((void *) 0x08048000)

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
    //ensure that usrc is on the stack
    if (is_kernel_vaddr((void*)usrc) || usrc == NULL || (void*)usrc <= LOWEST_ADDR)
      exit(-1);
    *dst = get_user (usrc);
  }
}


static bool
bad_ptr_arg(int arg)
{
  if ((const char*)arg == NULL || 
        (void*)arg <= LOWEST_ADDR || is_kernel_vaddr((void *)arg) ||
        pagedir_get_page(thread_current ()->pagedir, (void*)arg) == NULL)
  {
    return true;
  }
  return false;
}


static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_number;
  int args[3];
  
  //extract the syscall number
  copy_in (&syscall_number, f->esp, sizeof syscall_number);

  //check if the entire address of the syscall is in the stack
  if ((uint32_t) pg_round_up (f->esp) - (uint32_t) f->esp < sizeof(int)) 
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
    // ensure that argument is a valid pointer
    if (bad_ptr_arg(args[0]) || (int)args[1] < 0)
    {
      exit(-1);
    }

    //create the file
    bool success = filesys_create((const char*)args[0], args[1]);

    //set the returned value
    f->eax = success;
  }
  else if (syscall_number == SYS_REMOVE)
  {

    // ensure that argument is a valid pointer
    if (bad_ptr_arg(args[0]))
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
    if (bad_ptr_arg(args[0]))
    {
      exit(-1);
    }

    struct file* open_file = filesys_open((const char*)args[0]);

    if (open_file == NULL)
    {
      f->eax = -1;
    }
    else
    {
      int i;

      for (i = 2; i < 128; i++) 
      {
        //Find first empty slot for fd
        if (thread_current ()->fd_array[i] == NULL)
        {
          thread_current ()->fd_array[i] = open_file;
          break;
        }
      }

      f->eax = i;
    }
  }
  else if(syscall_number == SYS_CLOSE)
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < 128 && args[0] > 0)
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        file_close(thread_current()->fd_array[args[0]]);
        thread_current()->fd_array[args[0]] = NULL;
      }
    }
  }
  else if (syscall_number == SYS_READ)
  {
    if (bad_ptr_arg(args[1]))
    {
      exit(-1);
    }
    
    if (args[0] != 0 && args[0] != 1 && args[0] < 128 && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        int size = args[2];
        char* buffer = (char *)args[1];
        //stdin
        if(args[0] == 0)
        {
          int i  = 0;
          //Keep reading bytes until buffer is full
          while(i < size)
          {
            buffer[i] = input_getc();
            i++;
          }
          f->eax = i;
        }
        //some other file
        else
        {
          int read_bytes = file_read (thread_current()->fd_array[args[0]], buffer, size);
          f->eax = read_bytes;
        }
      }
      else
      {
        f->eax = -1;
      }
    } 
    else
    {
      f->eax = -1;
    }
  }
  else if (syscall_number == SYS_FILESIZE)
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < 128 && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        f->eax = file_length (thread_current()->fd_array[args[0]]);
      }
    }    
  }
  else if (syscall_number == SYS_TELL) 
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < 128 && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        f->eax = file_tell (thread_current()->fd_array[args[0]]);
      }
    }
  }

}
