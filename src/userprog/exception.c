#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "lib/string.h"
#include "userprog/pagedir.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
//   sema_init(&file_read_sema, 1);
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      exit(-1);

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  //If userprog needs more than 1 page

  //1. Data has 
  //check if data has been swapped out, then we need to swap it in
  //if it has not been swapped out, and its from the filesystem then we need to read it back
  //if not, swap out data then grow stack
  void* esp = f->esp;
  if (fault_addr == NULL) {
     exit(-1);
  }
  
//   if(!user)
//   {
   //   struct supplemental_page_entry *p = page_lookup(pg_round_down(fault_addr));
   //   printf("%d\n", p->page_flag);
   //   printf("here!\n");
//      exit(-1);
//   }

     struct supplemental_page_entry *p = page_lookup(pg_round_down(fault_addr));
     if (!user && p == NULL && !thread_current()->is_performing_syscall) { //THIS IS A PROBLEM
        exit(-1);
     }

     if (fault_addr < (void*)(0x08048000)) {
         exit(-1);
     }
   0xbfffef8c;
    0xc003e928;
     //grow stack
     if (p == NULL){
         // TODO:: If not in page table then check for bad address
         if (fault_addr < (void*)((unsigned int*)esp - (unsigned int *)32) && !write) {
            exit(-1);
         }

         // printf("here4\n");
         //Create a new frame for a page to grow the stack
         struct single_frame_entry *frame = frame_add(PAL_USER | PAL_ZERO, pg_round_down(fault_addr), true, CREATE_SUP_PAGE_ENTRY);
         
         //make sure address is not in kernel space
         if (is_kernel_vaddr(pg_round_down(frame->page->user_virtual_address))) {
            exit(-1);
         }

         // if (pagedir_get_page(thread_current ()->pagedir, (void*)fault_addr) == NULL) exit(-1);

         //Install the page
         if (!install_page(pg_round_down(frame->page->user_virtual_address), frame->frame_address, frame->page->writable)) exit(-1);
      //   printf("7here\n");
         return;
      }
      
      if (p->page_flag == FROM_FILE_SYSTEM){
        
      //   printf("here5\n");
      //Create frame entry without creating a supplemental page entry
      struct single_frame_entry *frame = frame_add(PAL_USER | PAL_ZERO, pg_round_down(p->user_virtual_address), p->writable, DONT_CREATE_SUP_PAGE_ENTRY);
      uint8_t *kpage = frame->frame_address;
      frame->page = p;

      // sema_down(&file_read_sema);
      //Read from file
      if (file_read_at (p->pg_data.file, kpage, p->pg_data.read_bytes, p->pg_data.ofs) != (int) p->pg_data.read_bytes) {
         // printf("asdfhere\n");
        
        exit(-1);
      }
      memset (kpage + p->pg_data.read_bytes, 0, 4096 - p->pg_data.read_bytes);
      // sema_up(&file_read_sema);

      //Install a new page
      if (!install_page(pg_round_down(p->user_virtual_address), kpage, p->writable)) exit(-1);
      p->page_flag = FROM_FRAME_TABLE;
      // p->frame = frame;
      // return;
     }
     
     if (write == 1 && not_present == 0) exit(-1);
   // Filsystem, if mem mapped file then we write data back to file
   // If the data is not swapped out then we write it back to the file
   // Swap Table -> need to swap back in
   //   uint8_t* kpage = frame_add(PAL_USER | PAL_ZERO, p->user_virtual_address, p->writable);
     
     return;
//   }
  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
}

