#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "lib/kernel/hash.h"
#include "vm/page.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

//Array of executable file pointers
static struct file* executable_list[MAX_CHILDREN]; /* List of all the executables currently running */
static int executable_list_idx = 3;                /* Index of the most recent executable launched */
static bool executable_list_unsuccess[MAX_CHILDREN]; /* List of executables that were not able to run */

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  // fn_copy = frame_add(0);
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL) 
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Get the name of the userprog */
  char *raw_name = (char *)malloc ((strlen (file_name) + 1) * sizeof (char));

  // Add raw_name to malloc'd array so it can be freed later
  for (int i = 0; i < 30; i++) 
  {
    if (thread_current()->malloced_pointers[i] == NULL)
    {
      thread_current()->malloced_pointers[i] = raw_name;
      break;
    }
  }

  strlcpy (raw_name, file_name, PGSIZE);
  char *actual_name = strtok_r(raw_name, " ", &raw_name);

  if (actual_name == NULL) 
    return TID_ERROR;
    

  struct thread *cur_parent = thread_current();
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (actual_name, PRI_DEFAULT, start_process, fn_copy);

  for (int i = 0; i < MAX_CHILDREN; i++)
  {
    if ((cur_parent->child_process_list)[i] == -1) 
    {
      (cur_parent->child_process_list)[i] = tid;
      break;
    }
  }

  if (thread_current ()->exec_sema.value == 0)
  {
    sema_down(&(thread_current ()->exec_sema));
  }

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  if (executable_list_unsuccess[tid] == true){
    // executable_list_unsuccess[executable_list_unsuccess_idx] = false;
    // executable_list_unsuccess_idx--;
    
    //Free the malloced pointers we stored before
    int i = 0;
    while (i < 30 && thread_current ()->malloced_pointers[i] != NULL) 
    {
      free(thread_current()->malloced_pointers[i]);
      thread_current()->malloced_pointers[i] = NULL;
      i++;
    }
    return TID_ERROR;
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
  {
    // printf("inside start process %s\n", file_name);
    // printf("failed to load\n");
    sema_down(&executable_list_sema);
    executable_list_unsuccess[executable_list_idx] = true;
    sema_up(&executable_list_sema);
    thread_exit ();
  }
  executable_list_idx++;

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED)
{ 
  bool found = false;
  int child_process_index = 0;

  for (int i = 0; i < MAX_CHILDREN; i++)
  {
    if (thread_current ()->child_process_list[i] == child_tid)
    {
      found = true;
      child_process_index = i;
      break;
    }
  }

  if (!found)
  {
    return -1;
  }
  
  sema_down(&thread_current()->process_sema);
  thread_current ()->child_process_list[child_process_index] = -1;
  return thread_current ()->exit_status[child_process_index];
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  // executable_list_idx--;
  sema_down(&executable_list_sema);
  file_close(executable_list[cur->tid]);
  executable_list[cur->tid] = NULL;
  sema_up(&executable_list_sema);
  
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
    //Free the malloced pointers we stored before
    int i = 0;
    while (i < 30 && thread_current ()->malloced_pointers[i] != NULL) 
    {
      free(thread_current()->malloced_pointers[i]);
      thread_current()->malloced_pointers[i] = NULL;
      i++;
    }

    sema_up(&cur->parent->process_sema);

    // Free the malloced pointers we stored before
    i = 0;
    while (i < 30 && thread_current ()->malloced_pointers[i] != NULL) 
    {
      free(thread_current()->malloced_pointers[i]);
      thread_current()->malloced_pointers[i] = NULL;
      i++;
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  char *raw_name = (char*) malloc ((strlen (file_name) + 1) * sizeof (char));

  for (int i = 0; i < 30; i++) 
  {
    if (thread_current()->malloced_pointers[i] == NULL)
    {
      thread_current()->malloced_pointers[i] = raw_name;
      break;
    }
  }

  strlcpy (raw_name, file_name, PGSIZE);
  char *actual_name = strtok_r(raw_name, " ", &raw_name);

  if (actual_name == NULL) 
    return TID_ERROR;

  /* Open executable file. */
  sema_down(&file_modification_sema);
  file = filesys_open (actual_name);
  sema_up(&file_modification_sema);

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", actual_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  sema_down(&executable_list_sema);
  executable_list[executable_list_idx] = file;
  sema_up(&executable_list_sema);

  file_deny_write(file);


  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file)) {
        goto done;
      }

      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      
      file_ofs += sizeof phdr;

      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:

          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  /* Set up stack. */
  if (!setup_stack (esp, file_name))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */

  sema_up(&(t->parent->exec_sema));

  return success;
}

/* load() helpers. */

bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   This function creates new suplementary page table entries for
   the pages the will need to be initialized later. This entry will 
   contain information about how the page needs to be initialized.
   i.e) The page must be writable by the user process if WRITABLE is true, 
        read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      
  
      /* Create suplemental page entries so we can lazy load on page faults */
      struct page_data pg_data = save_page_data(file, ofs, page_read_bytes);
      struct supplemental_page_entry *s = new_supplemental_page_entry(FROM_FILE_SYSTEM, pg_round_down(upage), writable, pg_data);
      if (s == NULL){
        return false;
      }

      /* Advance. */
      ofs += page_read_bytes;
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  // printf("end of load segment size: %d \n", file_length(file));

  return true;
}

/* 
 Credits goes to: https://www.delftstack.com/howto/c/trim-string-in-c/
 Gets rid of spaces from left and right of a string. 
*/
char *trimString(char *str)
{
    char *end;

    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';

    return str;
} 

/* 
  Populate the stack with the filename, taking care to add nullpointers and adding
  everything in the right order.
*/
void
populate_stack (void **esp, const char *file_name)
{
  char tokens_array[30][32];
  char *token;

  //Iterate over tokens
  char *args = (char *) malloc ((sizeof (char)) * (strlen (file_name) + 1));

  //Add args to malloc'd poiners array, so we can free it later
  for (int i = 0; i < 30; i++) 
  {
    if (thread_current()->malloced_pointers[i] == NULL)
    {
      thread_current()->malloced_pointers[i] = args;
      break;
    }
  }

  strlcpy(args, file_name, PGSIZE);
  int i = 0;
 
  while ((token = strtok_r(args, " ", &args)))
  {
    strlcpy (tokens_array[i], token, (strlen(token) + 1) * sizeof (char));

    // Stores the trimmed string incase there is multiple spaces
    char* str = trimString(tokens_array[i]);
    strlcpy(tokens_array[i], str, (strlen(str) + 1) * sizeof (char));

    i++;
  }

  char *address[30];
  /* Push the array items in reverse order onto the stack */
  for(int j = i-1; j >= 0; j--)
  {
    /* Push delimiter first, then push the reversed string */
    *esp -= 1;
    memcpy(*esp, "\0", 1);
    
    *esp -= strlen(tokens_array[j]);
    memcpy(*esp, tokens_array[j], strlen(tokens_array[j]));

    address[j] = (char*) *esp;
  }

  uint32_t esp_address = (uint32_t) *esp;
  int word_align_offset = 4 - (esp_address % 4);

  /* word align the stack */
  if(word_align_offset != 0)
  {
    *esp -= word_align_offset;
    memset(*esp, 0, word_align_offset);
    
  }

  /* Push 4 sentenial bytes */
  *esp -= 4;
  memset(*esp, 0, 4);

  /* Push the addresses of the arguments */
  for(int j = i-1; j >= 0; j--)
  {
    *esp -= sizeof(int);
    memcpy(*esp, &address[j], sizeof(int));
  }

  /* Push the previous address onto the stack */
  *esp -= sizeof(int);
  char* head = (char*)*esp + sizeof(int);

  memcpy(*esp, &head, sizeof(int));

  /* Push the number of arguments onto the stack */
  *esp -= sizeof(int);
  memset(*esp, i, 1);

  /* Push the address of the return address onto the stack */
  *esp -= sizeof(int);
  memset(*esp, 0, sizeof(int));
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, const char *file_name) 
{
  bool success = false;
  
  /* Create new frame (non-lazily) for the first time when we setup the stack */
  struct single_frame_entry *frame = frame_add(PAL_USER | PAL_ZERO, ((uint8_t *) PHYS_BASE) - PGSIZE, true, CREATE_SUP_PAGE_ENTRY);

  if (frame->frame_address != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, frame->frame_address, true);
      if (success) 
      {
        *esp = PHYS_BASE;
        populate_stack(esp, file_name);
      }
      else
        page_remove(frame->page);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}