# Lab 1.2.4 — Thread Priorities & Preemptive Scheduling

## Objective

Demonstrate that a higher-priority thread preempts a lower-priority,
CPU-bound thread mid-execution — without the lower-priority thread ever
voluntarily yielding — and observe how `k_msleep()` affects scheduling
independently of priority.

## Theory

### Priority numbers in Zephyr

Zephyr uses signed priority numbers: **negative = cooperative**,
**positive = preemptive**, and **lower number = higher priority** within
each class. Both threads in this lab use positive (preemptive) priorities:
HIGH at 2, LOW at 7 — so HIGH always wins whenever both are ready to run at
the same time.

`CONFIG_NUM_PREEMPT_PRIORITIES` (15 in this build) sets the number of
valid preemptive priority levels (0–14 here), confirming both 2 and 7 are
in range.

### Preemptive vs. cooperative — why this matters

A cooperative thread (negative priority) is **not** automatically
interrupted by a higher-priority thread — it must hit a voluntary yield
point (`k_yield()`, `k_sleep()`, a blocking call, etc.) before the
scheduler can switch away from it. This is a real safety consideration:
in a cooperative scheduling context, a single thread that never yields can
starve every other thread indefinitely, even ones with technically higher
priority.

A preemptive thread, in contrast, can be forcibly switched out by the
kernel at (essentially) any point, as soon as a higher-priority thread
becomes ready — with no cooperation required from the thread being
preempted. This lab uses two preemptive threads specifically to
demonstrate that LOW's busy-wait loop (with zero yield points) still gets
interrupted, because both threads are in the preemptive class.

### The deliberately busy `for` loop in `low_prio_entry()`

```c
for (volatile int i = 0; i < 500000; i++) { /* burn CPU cycles */ }
```

This loop has no sleep, no yield, nothing that would voluntarily hand the
CPU back to the scheduler. It exists specifically to prove that
preemption in Zephyr's preemptive priority class does *not* depend on the
lower-priority thread's cooperation — the kernel can and does cut it off
mid-loop.

### `k_msleep()` and its effect on scheduling

`high_prio_entry()` calls `k_msleep(1000)` after each run. This **blocks**
the thread — removes it from the ready queue entirely for that duration,
regardless of its priority. A sleeping thread does not compete for the
CPU at all; it isn't "yielding turns" to LOW, it's simply not a candidate
for scheduling until its timeout expires. This is why LOW gets
uninterrupted stretches of roughly 1 second at a time: not because LOW
"wins" against HIGH, but because HIGH isn't in the race during that
window.

When HIGH's sleep timeout expires, the kernel moves it back to "ready" and
immediately preempts LOW (since 2 < 7), cutting into LOW's busy-wait loop
at essentially any point — not waiting for LOW's current iteration to
finish.

### Timing evidence: mid-loop preemption, not turn-taking

Log ordering confirms genuine mid-execution preemption:

```
[15.645] HIGH priority running (low was at iteration 1130)
[15.706] LOW priority finished iteration 1130
```

`low_prio_iteration++` is the *first* line of LOW's loop body, before the
busy-wait. HIGH's log shows `1130` — meaning LOW had already incremented
to 1130 and was mid-busy-wait when HIGH preempted it. LOW's own "finished
iteration 1130" print comes *after* HIGH's log — proving LOW resumed
exactly where it was cut off and completed that same iteration, rather
than HIGH waiting for LOW to finish first.

## What we learned

- **Preemptive priority does not depend on the lower-priority thread
  yielding** — the kernel forcibly switches threads based on priority
  alone, interrupting a busy-wait loop mid-execution with no cooperation
  required.
- **`k_msleep()` removes a thread from scheduling consideration
  entirely**, independent of its priority — a sleeping high-priority
  thread does not block a lower-priority thread from running; it simply
  isn't competing for the CPU during that time.
- **Priority decides *who wins when both are ready*, not *how much CPU
  time each gets overall*** — HIGH's small share of total run time here is
  a consequence of its own `k_msleep(1000)` call, not of losing to LOW's
  priority.
- **A hidden failure mode of `CONFIG_LOG_MODE_DEFERRED`**: this lab
  initially produced *zero* serial output, even with
  `CONFIG_ASSERT`/`CONFIG_HW_STACK_PROTECTION` enabled (ruling out a
  crash). Root cause: deferred logging relies on a dedicated, low-priority
  log-processing thread to actually flush queued messages to UART. Since
  `low_prio_entry()` (priority 7) never yields, and the log thread's
  priority is lower still, the log thread was **starved indefinitely** —
  never scheduled, so the log queue never drained, producing total
  silence despite the application logic running correctly underneath.
  Switching to `CONFIG_LOG_MODE_IMMEDIATE=y` (synchronous, in-place
  logging, no dependency on a separate thread being scheduled) resolved
  it immediately and confirmed the real cause.
- **Verified on hardware**: STM32 log showed `HIGH priority running`
  firing roughly once per second, always immediately followed by LOW
  resuming and finishing the iteration it was cut off on — confirming true
  preemptive interruption of a non-yielding CPU-bound thread.