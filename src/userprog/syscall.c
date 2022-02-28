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
#include "process.h"

#define LOWEST_ADDR ((void *) 0x08048000)
#define LARGE_WRITE_CHUNK 100

static void syscall_handler (struct intr_frame *);
static struct semaphore file_modification_sema;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  sema_init(&file_modification_sema, 1);
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
  
  //close any open file descriptors
  for (int i = 2; i < 128; i++)
  {
      if (thread_current()->fd_array[i] != NULL) 
      {
        file_close(thread_current()->fd_array[i]);
        thread_current()->fd_array[i] = NULL;
      }
  }
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
  
  // printf("first\n");

  //extract the syscall number
  copy_in (&syscall_number, f->esp, sizeof syscall_number);

    // printf("second\n");

  //check if the entire address of the syscall is in the stack
  if ((uint32_t) pg_round_up (f->esp) - (uint32_t) f->esp < sizeof(int)) 
  {
    exit(-1);
  }

    // printf("third\n");


  //extract the 3 arguments
  copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 3);

    // printf("fourth\n");

  // printf("sycall number %d\n", syscall_number);
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
    if (bad_ptr_arg(args[1]))
    {
      exit(-1);
    }
    //Prevent other tasks when writing
    sema_down(&file_modification_sema);
    
    if (args[0] != 0 && args[0] < 128 && args[0] > 0) 
    {
      int size = args[2];
      char* buffer = (char *)args[1];
      //stdout
      if(args[0] == 1)
      {
        //execute the write on STDOUT_FILENO
        //Write to buffer in chunks of 100 bytes
        int total_written = 0;
        bool break_into_chunks = false;
        for(int i = 0; i < size; i += LARGE_WRITE_CHUNK)
        {
          int writing_size = size - i;

          //if the writing size is less than 100, break into small chunks
          if(writing_size > LARGE_WRITE_CHUNK)
          {
            break_into_chunks = true;
            
          } else
          {
            break_into_chunks = false;
          }

          //calculate the proper chunk size
          if(break_into_chunks){
            putbuf(buffer + i, LARGE_WRITE_CHUNK);
            total_written += LARGE_WRITE_CHUNK;
          }
          else{
            putbuf(buffer + i, size - i);
            total_written += size-i;
          }
        } 
        f->eax = total_written;
      }
      else if (thread_current()->fd_array[args[0]] != NULL) 
      {
        //write to some other file
        int read_bytes = file_write (thread_current()->fd_array[args[0]], buffer, size);
        f->eax = read_bytes;
      }
      else
      {
        sema_up(&file_modification_sema);

        f->eax = -1;
        exit(-1);
      }
    } 
    else
    {
      sema_up(&file_modification_sema);

      f->eax = -1;
      exit(-1);
    }

    sema_up(&file_modification_sema);
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

    sema_down(&file_modification_sema);

    //remove the file
    bool success = filesys_remove((const char*)args[0]);
    
    //set the returned value
    f->eax = success;
    sema_up(&file_modification_sema);
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
    //Prevent other tasks when reading
    sema_down(&file_modification_sema);
    // printf("HERE\n");
    if (args[0] != 1 && args[0] < 128 && args[0] > 0) 
    {
      int size = args[2];
      char* buffer = (char *)args[1];
      //stdin
      if(args[0] == 0)
      {
        int i = 0;
        //Keep reading bytes until buffer is full
        while(i < size)
        {
          buffer[i] = input_getc();
          i++;
        }
        f->eax = i;
      }
      else if (thread_current()->fd_array[args[0]] != NULL) 
      {

        //some other file
        int read_bytes = file_read (thread_current()->fd_array[args[0]], buffer, size);
        f->eax = read_bytes;
      }
      else
      {
        
        sema_up(&file_modification_sema);
        f->eax = -1;
        exit(-1);
      }
    }
    else
    {
      
      sema_up(&file_modification_sema);
      f->eax = -1;
      exit(-1);
    }
    sema_up(&file_modification_sema);
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
  else if (syscall_number == SYS_SEEK)
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < 128 && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        file_seek (thread_current()->fd_array[args[0]], args[1]);
      }
    }
  }
  else if (syscall_number == SYS_EXEC)
  {
    if (bad_ptr_arg(args[0]))
    {
      exit(-1);
    }
    
    // sema_down(&(thread_current ()->exec_sema));

    tid_t child_pid = process_execute ((const char*) args[0]);

    f->eax = child_pid;
  }

}
