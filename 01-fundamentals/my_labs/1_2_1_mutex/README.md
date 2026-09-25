# Lab 1.2.1 — Mutex

## Objective

Demonstrate mutual exclusion in Zephyr using `k_mutex` to protect a shared
resource accessed concurrently by two threads of equal priority.

## Theory

### The problem: race conditions

Two threads, A and B, both execute a read-modify-write sequence on the same
global variable (`shared_counter`):

```c
int local_copy = shared_counter;   // read
k_msleep(50);                      // simulate work
shared_counter = local_copy + N;   // write
```

Without protection, the scheduler can preempt/switch between A and B at any
point in this sequence — including between the read and the write. If B
reads `shared_counter` while A is still "working" on its own local copy (in
the `k_msleep(50)` window), B's write and A's write can both be based on the
same stale value, and one of the two updates gets silently overwritten (a
**lost update**). This is a classic race condition, and it gets worse, not
better, the longer the critical section takes (which is why the 50ms sleep
is deliberately placed *inside* the critical section here — it maximizes
the window where corruption could happen if the mutex weren't there).

### The fix: `k_mutex`

A mutex (mutual exclusion lock) guarantees that only one thread can be
between `k_mutex_lock()` and `k_mutex_unlock()` at a time. Any other thread
calling `k_mutex_lock()` on the same mutex blocks (goes to sleep, off the
run queue) until the owner calls `k_mutex_unlock()`.

- `K_MUTEX_DEFINE(counter_mutex)` — defines and statically initializes the
  mutex at compile time (no runtime init call needed, unlike
  `k_mutex_init()`).
- `k_mutex_lock(&counter_mutex, K_FOREVER)` — block indefinitely until the
  mutex is available. Zephyr also supports a timeout value or `K_NO_WAIT`
  for a non-blocking try-lock.
- Zephyr's `k_mutex` is **recursive-safe** for the *same* thread (a thread
  can lock it multiple times if it already owns it) and implements
  **priority inheritance**: if a low-priority thread holds the mutex and a
  high-priority thread blocks on it, the low-priority thread is temporarily
  boosted to the high-priority thread's priority to prevent priority
  inversion. This lab doesn't exercise priority inheritance directly since
  both threads run at the same priority (5), but it's a key property that
  distinguishes `k_mutex` from a plain binary semaphore.

### Why both threads at the same priority matters here

Thread A and Thread B are both created at priority 5. Neither one preempts
the other based on priority — they interleave purely because each one
voluntarily gives up the CPU via `k_msleep()`. This isolates the
demonstration to the mutex mechanism alone, without priority-based
preemption complicating the picture (that's covered separately in Lab
1.2.4).

## What we learned

- **Mutual exclusion is enforced by blocking, not by trickery.** The second
  thread doesn't "wait its turn" cooperatively — it's physically removed
  from the ready queue by `k_mutex_lock()` until the mutex is released.
- **The critical section boundary matters.** Everything between `lock()`
  and `unlock()` — including the `k_msleep(50)` — is protected. This
  confirms that `k_msleep()` inside a critical section does NOT release the
  mutex; it only yields the CPU, and the mutex stays held by the sleeping
  thread.
- **Verified on hardware**: the STM32 Nucleo-F411RE log showed a
  consistent, non-corrupted incrementing sequence
  (e.g., `3608 → 3609 (A,+1) → 3619 (B,+10) → 3620 (A,+1) → 3630 (B,+10)`),
  confirming no lost updates occurred across many alternations between A
  and B.
- **Static thread stacks** (`K_THREAD_STACK_DEFINE`) reserve memory for
  each thread at compile time, in contrast to FreeRTOS's default of
  dynamically allocating stacks from the heap in `xTaskCreate()`. This is
  relevant for real-time/embedded contexts where heap fragmentation and
  allocation-time jitter are undesirable.
