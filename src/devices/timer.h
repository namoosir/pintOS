#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include <list.h>
#include <threads/synch.h>

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100

struct thread *get_first_thread_due_time_node(void);
void unblock_sleeping_thread(struct thread *first);
void thread_due_time_init(struct thread *t, int64_t alarm_due_time);
void increase_recent_cpu_value(void);

void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

#endif /* devices/timer.h */
