#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mutex_lab, LOG_LEVEL_INF);

/* Shared resource -- both threads will try to access this */
static int shared_counter = 0;

/* The mutex protecting the shared resource */
K_MUTEX_DEFINE(counter_mutex);

/* Static thread stacks -- reserved at compile time, not from heap */
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE);

static struct k_thread thread_a_data;
static struct k_thread thread_b_data;

void thread_a_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_mutex_lock(&counter_mutex, K_FOREVER);

        int local_copy = shared_counter;
        LOG_INF("Thread A: read %d", local_copy);
        k_msleep(50); /* simulate work while holding the mutex */
        shared_counter = local_copy + 1;
        LOG_INF("Thread A: wrote %d", shared_counter);

        k_mutex_unlock(&counter_mutex);
        k_msleep(200);
    }
}

void thread_b_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_mutex_lock(&counter_mutex, K_FOREVER);

        int local_copy = shared_counter;
        LOG_INF("Thread B: read %d", local_copy);
        k_msleep(50);
        shared_counter = local_copy + 10;
        LOG_INF("Thread B: wrote %d", shared_counter);

        k_mutex_unlock(&counter_mutex);
        k_msleep(200);
    }
}

int main(void)
{
    k_thread_create(&thread_a_data, thread_a_stack, STACK_SIZE,
                     thread_a_entry, NULL, NULL, NULL,
                     5 /* priority */, 0, K_NO_WAIT);

    k_thread_create(&thread_b_data, thread_b_stack, STACK_SIZE,
                     thread_b_entry, NULL, NULL, NULL,
                     5 /* priority */, 0, K_NO_WAIT);

    return 0;
}


/*
                              main()
                                │
        ┌───────────────────────┴───────────────────────┐
        │                                                │
  k_thread_create(A)                              k_thread_create(B)
  prio 5, K_NO_WAIT                                prio 5, K_NO_WAIT
        │                                                │
        ▼                                                ▼
┌───────────────────┐                          ┌───────────────────┐
│  THREAD A          │                          │  THREAD B          │
│  (thread_a_entry)  │                          │  (thread_b_entry)  │
└─────────┬──────────┘                          └─────────┬──────────┘
          │                                                │
          ▼                                                ▼
   ┌─────────────┐                                  ┌─────────────┐
   │ while (1)   │◄────────────┐                     │ while (1)   │◄────────────┐
   └──────┬──────┘             │                     └──────┬──────┘             │
          ▼                    │                            ▼                    │
 k_mutex_lock(counter_mutex,   │                   k_mutex_lock(counter_mutex,   │
     K_FOREVER)                │                       K_FOREVER)                │
          │                    │                            │                    │
          ▼                    │                            ▼                    │
   ┌──────────────┐            │                     ┌──────────────┐            │
   │ mutex free?  │            │                     │ mutex free?  │            │
   └───┬──────┬───┘            │                     └───┬──────┬───┘            │
       │no    │yes              │                         │no    │yes              │
       ▼      ▼                │                         ▼      ▼                │
    BLOCKS   enters             │                      BLOCKS   enters             │
   (waits)   critical section   │                     (waits)   critical section   │
       │         │              │                         │         │              │
       │         ▼              │                         │         ▼              │
       │  local_copy = shared_counter                     │  local_copy = shared_counter
       │         │              │                         │         │              │
       │         ▼              │                         │         ▼              │
       │  LOG_INF("A: read %d") │                         │  LOG_INF("B: read %d") │
       │         │              │                         │         │              │
       │         ▼              │                         │         ▼              │
       │  k_msleep(50)          │                         │  k_msleep(50)          │
       │  (mutex STILL held,    │                         │  (mutex STILL held,    │
       │   the other thread     │                         │   the other thread     │
       │   cannot enter here)   │                         │   cannot enter here)   │
       │         │              │                         │         │              │
       │         ▼              │                         │         ▼              │
       │  shared_counter =      │                         │  shared_counter =      │
       │    local_copy + 1      │                         │    local_copy + 10     │
       │         │              │                         │         │              │
       │         ▼              │                         │         ▼              │
       │  LOG_INF("A: wrote %d")│                         │  LOG_INF("B: wrote %d")│
       │         │              │                         │         │              │
       │         ▼              │                         │         ▼              │
       │  k_mutex_unlock() ─────┼──► releases mutex,       │  k_mutex_unlock() ─────┼──► releases mutex,
       │         │              │    wakes up whichever     │         │              │    wakes up whichever
       │         │              │    thread (A or B) was    │         │              │    thread (A or B) was
       │         │              │    blocked waiting for it │         │              │    blocked waiting for it
       │         ▼              │                         │         ▼              │
       │  k_msleep(200)         │                         │  k_msleep(200)         │
       │         │              │                         │         │              │
       └─────────┴──────────────┘                         └─────────┴──────────────┘
                 (back to while(1))                                  (back to while(1))


LEGEND:
- shared_counter: the shared resource, a single global variable.
- counter_mutex : the lock that serializes access; ONLY one thread
                  (A or B) can be between lock()/unlock() at a time.
- The "read → k_msleep(50) → write" window is the critical section:
  without the mutex, the other thread could read shared_counter
  BETWEEN the read and the write of the first one (classic race
  condition: lost update). With the mutex, the second thread stays
  blocked in k_mutex_lock() until the first one calls
  k_mutex_unlock().
- Both threads have the SAME priority (5) -> there's no preemption
  between them based on priority; they alternate because each one
  sleeps (k_msleep), voluntarily releasing the CPU, not because one
  "beats" the other.
*/