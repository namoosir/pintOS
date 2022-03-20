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
#include "vm/page.h"
#include "vm/frame.h"
#include "lib/string.h"
#include "threads/malloc.h"

#define LOWEST_ADDR ((void *) 0x08048000) /* lowest address of stack */
#define LARGE_WRITE_CHUNK 100  /* max number of bytes to write to stdout at once */

static void syscall_handler (struct intr_frame *);
void exit (int status);
static struct semaphore file_write_sema; /* semaphore for file write */
static struct semaphore file_modification_sema; /* semaphore for other file operations */
static bool sema_initialized; /*A flag to initialize semaphores only once */

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
// static bool
// put_user (uint8_t *udst, uint8_t byte)
// {
//   int error_code;
//   asm ("movl $1f, %0; movb %b2, %1; 1:"
//        : "=&a" (error_code), "=m" (*udst) : "q" (byte));
//   return error_code != -1;
// }

/*Hash action function to free elements of a hashtable*/
void free_pages_in_hash (struct hash_elem *e, void *aux UNUSED)
{
  struct supplemental_page_entry *x =  hash_entry(e, struct supplemental_page_entry, supplemental_page_elem);
  page_remove(x);
}

/*Performs memory unmapping*/
void perform_munmap(int map_id)
{
  int map_exists = 0;
  struct mapping_information *map_info;

  for (struct list_elem *e = list_begin (&thread_current()->mapping_info_list); e != list_end (&thread_current()->mapping_info_list);
          e = list_next (e))
  {
    map_info = list_entry (e, struct mapping_information, map_elem);
    if (map_id == map_info->mapping_id)
    {
      map_exists = 1;
      break;
    }
  }

  if (map_exists) 
  {
    struct file *file = map_info->file;
    //Prevent other tasks when writing
    sema_down(&file_write_sema);

    list_remove(&map_info->map_elem);

    int total_pages = (map_info->end_addr - map_info->start_addr) / PGSIZE;
    int size = map_info->file_size;
    int offset = 0;

    for (int i = 0; i < total_pages; i++) 
    {
      uint8_t *temp_page_addr = map_info->start_addr + i*PGSIZE;
      size -= PGSIZE;
      if (pagedir_is_dirty(thread_current()->pagedir, temp_page_addr))
      {
        if(size > 0) file_write_at (file, temp_page_addr, PGSIZE, offset);
        else file_write_at (file, temp_page_addr, PGSIZE + size, offset);
      }
      offset += PGSIZE;
      page_remove(page_lookup(map_info->start_addr + i*PGSIZE, thread_current()));
    }

    free(map_info);
    sema_up(&file_write_sema);
  }
}
/* 
  Exit from the process and close all files that the process has open
  and free all memory allocated by the process.

  Saves the exit status in its parent process.
*/
void
exit (int status)
{
  printf("%s: exit(%d)\n", thread_current()->name, status);
  
  //close any open file descriptors
  for (int i = 2; i < MAX_FILE_DESCRIPTORS; i++)
  {
      if (thread_current()->fd_array[i] != NULL) 
      {
        file_close(thread_current()->fd_array[i]);
        thread_current()->fd_array[i] = NULL;
      }
  }

  int child_process_index = 0;

  for (int i = 0; i < MAX_CHILDREN; i++)
  {
    if (thread_current ()->parent->child_process_list[i] == thread_current()->tid)
    {
      child_process_index = i;
      break;
    }
  }

  thread_current()->parent->exit_status[child_process_index] = status;

  //TODO: FIX SO THAT ALL MAPPED ELEMENTS ARE UNMAPPED
  perform_munmap(0);

  //free hash table entries
  // hash_action_func *free_pages = free_pages_in_hash;
  // hash_apply(&thread_current()->supplemental_page_hash_table, free_pages);
  // hash_destroy(&thread_current()->supplemental_page_hash_table, free_pages);
  //exit from the thread
  thread_exit ();
}

/* 
  Copies size bytes from *usrc_ to *dst_ making sure
  *usrc_ is part of user memory.
*/
static void
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
  {
    //ensure that usrc is on the stack
    if (usrc == NULL || is_kernel_vaddr((void*)usrc) || (void*)usrc <= LOWEST_ADDR)
      exit(-1);
    *dst = get_user (usrc);
  }
}

/* Return true if arg is a bad pointer and false otherwise */
static bool
bad_ptr_arg(int arg)
{
  if ((const char*)arg == NULL || 
        (void*)arg <= LOWEST_ADDR || is_kernel_vaddr((void *)arg) /* ||
         pagedir_get_page(thread_current ()->pagedir, (void*)arg) == NULL */ )
  {
    // printf("first: %d, second: %d, third: %d\n", (const char*)arg == NULL, is_kernel_vaddr((void *)arg), pagedir_get_page(thread_current ()->pagedir, (void*)arg) == NULL);
    return true;
  }
  return false;
}

/* 
  Handler for the different syscalls. Parses user input and then executes the correct 
  syscall.
*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_number;
  int args[3];

  if(!sema_initialized){
    sema_init(&file_read_sema, 1);
    sema_init(&file_write_sema, 1);
    sema_init(&file_modification_sema, 1);
    sema_initialized = true;
  }

  thread_current()->is_performing_syscall = true;


  
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
    if (bad_ptr_arg(args[1]))
    {
      exit(-1);
    }
    // printf("here\n");
    

    // if (page_lookup(pg_round_down((uint8_t *)args[1]), thread_current()->parent) != NULL) exit(-1);
    //Check if the file descriptor is valid
    if (args[0] != 0 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0) 
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
        //Prevent other tasks when writing
        sema_down(&file_write_sema);

        //write to some other file
        int read_bytes = file_write (thread_current()->fd_array[args[0]], buffer, size);
        f->eax = read_bytes;
        
        sema_up(&file_write_sema);
      }
      else
      {
        f->eax = -1;
        exit(-1);
      }
    } 
    else
    {
      f->eax = -1;
      exit(-1);
    }
  }
  else if (syscall_number == SYS_CREATE)
  {
    
    // ensure that argument is a valid pointer
    if (bad_ptr_arg(args[0]) || (int)args[1] < 0)
    {
      exit(-1);
    }
    sema_down(&file_modification_sema);
    //create the file
    bool success = filesys_create((const char*)args[0], args[1]);
    sema_up(&file_modification_sema);

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
  else if (syscall_number == SYS_OPEN)
  {
    if (bad_ptr_arg(args[0]))
    {
      exit(-1);
    }
    sema_down(&file_modification_sema);
    struct file* open_file = filesys_open((const char*)args[0]);
    sema_up(&file_modification_sema);
    if (open_file == NULL)
    {
      f->eax = -1;
    }
    else
    {
      int i;
      for (i = 2; i < MAX_FILE_DESCRIPTORS; i++) 
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
    if (args[0] != 0 && args[0] != 1 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0)
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        sema_down(&file_modification_sema);
        file_close(thread_current()->fd_array[args[0]]);
        thread_current()->fd_array[args[0]] = NULL;
        sema_up(&file_modification_sema);
      }
    }
  }
  else if (syscall_number == SYS_READ)
  {
    if (bad_ptr_arg(args[1]))
    {
      exit(-1);
    }

    // printf("HERE\n");
    if (args[0] != 1 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0) 
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
        //Prevent other tasks when reading
        // printf("SEMA DOWN %s\n", thread_current()->name);
        sema_down(&file_read_sema);
        // printf("entering critical section %s\n", thread_current()->name);

        //some other file
        int read_bytes = file_read (thread_current()->fd_array[args[0]], buffer, size);
        f->eax = read_bytes;

        // printf("exiting critical section %s\n", thread_current()->name);
        sema_up(&file_read_sema);
        // printf("SEMA UP %s\n", thread_current()->name);
      }
      else
      {
        f->eax = -1;
        exit(-1);
      }
    }
    else
    {
      f->eax = -1;
      exit(-1);
    }
  }
  else if (syscall_number == SYS_FILESIZE)
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        sema_down(&file_modification_sema);
        f->eax = file_length (thread_current()->fd_array[args[0]]);
        sema_up(&file_modification_sema);
      }
    }    
  }
  else if (syscall_number == SYS_TELL) 
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        sema_down(&file_modification_sema);
        f->eax = file_tell (thread_current()->fd_array[args[0]]);
        sema_up(&file_modification_sema);
      }
    }
  }
  else if (syscall_number == SYS_SEEK)
  {
    if (args[0] != 0 && args[0] != 1 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        sema_down(&file_modification_sema);
        file_seek (thread_current()->fd_array[args[0]], args[1]);
        sema_up(&file_modification_sema);
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
  else if (syscall_number == SYS_WAIT) 
  {
    f->eax = process_wait(args[0]);
  }
  else if (syscall_number == SYS_MMAP)
  {
    // printf("mapping\n");
    int file_size = 0;
    if (args[0] != 0 && args[0] != 1 && args[0] < MAX_FILE_DESCRIPTORS && args[0] > 0) 
    {
      if (thread_current()->fd_array[args[0]] != NULL) 
      {
        sema_down(&file_modification_sema);
        file_size = file_length (thread_current()->fd_array[args[0]]);
        uint8_t *upage = (uint8_t *)args[1];
        if (file_size == 0 || pg_round_down(upage) != upage || upage == 0)
        {
          f->eax = -1;
          return;
        }

        struct supplemental_page_entry *p = page_lookup(pg_round_down(upage), thread_current());
        if (p != NULL) 
        {
          f->eax = -1;
          return;
        }

        // for (struct list_elem *e = list_begin (&thread_current()->mapping_info_list); e != list_end (&thread_current()->mapping_info_list);
        //    e = list_next (e))
        // {
        //   struct mapping_information *map_info = list_entry (e, struct mapping_information, map_elem);
        //   if (upage > map_info->start_addr && upage < map_info->end_addr)
        //   {
        //     f->eax = -1;
        //     return;
        //   }
        // }

        int read_bytes = file_size;
        int zero_bytes = read_bytes % PGSIZE;
        int ofs = 0;

        struct mapping_information *new_mapping = (struct mapping_information*)malloc(sizeof(struct mapping_information));

        if (new_mapping == NULL) 
        {
          f->eax = -1;
          return;
        }
        
        new_mapping->start_addr = upage;
        struct file* file = file_reopen(thread_current()->fd_array[args[0]]);
        new_mapping->file = file;

        file_seek (file, ofs);
        while (read_bytes > 0 || zero_bytes > 0)
        {
          size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
          size_t page_zero_bytes = PGSIZE - page_read_bytes;

          struct page_data pg_data = save_page_data(file, ofs, page_read_bytes);
          struct supplemental_page_entry *s = new_supplemental_page_entry(FROM_FILE_SYSTEM, pg_round_down(upage), 1, pg_data);
          if (s == NULL){
            f->eax = -1;
            return;
          }
          ofs += page_read_bytes;
          read_bytes -= page_read_bytes;
          zero_bytes -= page_zero_bytes;
          upage += PGSIZE;
        }

        new_mapping->end_addr = upage;
        new_mapping->mapping_id = thread_current()->mapping_count;
        new_mapping->file_size = file_size;
        list_push_front(&thread_current()->mapping_info_list, &new_mapping->map_elem);

        f->eax = thread_current()->mapping_count;

        thread_current()->mapping_count++;
        
        sema_up(&file_modification_sema);
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
  else if (syscall_number == SYS_MUNMAP)
  {
    perform_munmap(args[0]);
  }
  // free(args);
}