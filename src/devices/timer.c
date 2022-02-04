#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
  
/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* A list for storing the alarm due time and the 
   semaphore for blocking the current thread. */
static struct list thread_due_time_list;
static struct semaphore blocked_thread_list_sema;

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");

  /* timer_init is called once, so we initaizlize the list here */
  list_init (&thread_due_time_list);
  // initializating semaphore to lock the list
  sema_init(&blocked_thread_list_sema, 1);
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (loops_per_tick | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* ----------- */
list_less_func compare_ticks_func;
bool
compare_ticks_func(const struct list_elem *a, const struct list_elem *b, void *aux)
{

  if(aux == NULL){
    struct thread *t1 = list_entry (a, struct thread, blockedelem);
    struct thread *t2 = list_entry (b, struct thread, blockedelem);
    return t1->alarm_due_time < t2->alarm_due_time;
  }
  return NULL;
}

/* ----------- */
void
thread_due_time_init(struct thread *t, int64_t alarm_due_time)
{
  sema_init(&t->blocker_sema, 0);
  t->alarm_due_time = alarm_due_time;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks) 
{
  //printf("ticks before if: %lld\n", ticks);

  ASSERT (intr_get_level () == INTR_ON);

  if (ticks <= 0)
    {
      return;
    }

  int64_t start = timer_ticks ();

  //printf("ticks after if: %lld, start: %lld\n", ticks, start);

  list_less_func* compare_ticks = compare_ticks_func;

  struct thread *curr_thread = thread_current();
  //curr_thread->alarm_due_time = start + ticks;

  // struct semaphore blocker_sema;
  // sema_init(&blocker_sema, 0);
  // curr_thread->blocker_sema = blocker_sema;


  //struct sema_due_time_node *node = malloc(sizeof(struct sema_due_time_node));
  //struct semaphore *sleep_sema = malloc(sizeof(struct semaphore));
  
  thread_due_time_init(curr_thread, start + ticks);

  /* Only allow one thread to insert into the list at a time */
  // sema_init(&blocked_thread_list_sema, 1);
  sema_down(&blocked_thread_list_sema);
  list_insert_ordered (&thread_due_time_list, &curr_thread->blockedelem, compare_ticks, NULL);
  sema_up(&blocked_thread_list_sema);
  
  /* Put current thread to sleep */
  //curr_thread->status = THREAD_BLOCKED;
  sema_down(&curr_thread->blocker_sema);
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}


/* ----------- */
struct thread *
get_first_thread_due_time_node(void)
{
  struct list_elem *e = list_begin(&thread_due_time_list);
  struct thread *t = list_entry(e, struct thread, blockedelem);
  return t;
}

/* ----------- */
void
unblock_sleeping_thread(struct thread *first){
  if(first != NULL && &first->blocker_sema != NULL){
    sema_up(&first->blocker_sema);
    list_pop_front (&thread_due_time_list);
  }
}

void increase_recent_cpu_value(void) {
  thread_current()->recent_cpu_value++;
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  if(thread_mlfqs)
  {
    if (timer_ticks() == 1) 
    {
      thread_get_load_avg();
    }
  
    if (timer_ticks() % TIMER_FREQ == 0) 
    {
      thread_get_load_avg();
    }  
    increase_recent_cpu_value();
  }

  
  bool ready = true;
  /* Wake up all threads that reached the alarm_due_time */
  while (ready && list_size (&thread_due_time_list) > 0) 
    {
      //struct sema_due_time_node *first = get_first_sema_due_time_node();
      struct thread *first = get_first_thread_due_time_node();
      if (ticks >= first->alarm_due_time)
        {
          unblock_sleeping_thread(first);
          continue;
        }
      ready = false;
    }
  thread_tick ();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}
