# Lab 1.2.3 — Deferred Work (k_work_delayable)

## Objective

Demonstrate a "sensor timeout watchdog" using Zephyr's deferred work
(`k_work_delayable`) instead of a dedicated thread, showing how to run
delayed/rescheduled callbacks without busy-waiting or extra stack usage.

## Theory

### The problem this solves: timeouts without a dedicated thread

A common embedded pattern is "if I don't hear from X within N seconds,
something is wrong." The naive solution is a dedicated thread that sleeps
in a loop, checking elapsed time. That costs a full stack allocation and a
running thread for something that, most of the time, does nothing at all.

`k_work_delayable` solves this differently: it's a **callback scheduled to
run later**, on a shared system thread (the **system workqueue**), not on a
thread the application owns. There's no busy-waiting and no dedicated
stack per timeout — just a lightweight work item.

### `K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_handler)`

Statically defines the delayable work item at compile time and associates
it with `timeout_handler` — the function that runs when the work item
actually fires. This mirrors the same "static allocation over dynamic"
philosophy as `K_THREAD_STACK_DEFINE` in earlier labs: no runtime
initialization call needed for this single, compile-time-known instance.

### `k_work_schedule()` vs `k_work_reschedule()`

- **`k_work_schedule(&timeout_work, K_SECONDS(2))`** — schedules the work
  item to fire in 2 seconds, but only if it isn't already scheduled. Used
  once, to set the initial alarm.
- **`k_work_reschedule(&timeout_work, K_SECONDS(2))`** — cancels any
  pending fire time and sets a new one, 2 seconds from now, regardless of
  whether it was already scheduled. This is what "resets the timer" every
  time new data arrives.

Both calls are **non-blocking**: `main()`'s loop keeps running immediately
after calling them. The actual countdown and firing are handled entirely
by the kernel in the background.

### The system workqueue

The handler (`timeout_handler`) does not run on the main thread. It runs on
the **system workqueue** — a single shared thread that Zephyr already
provides, used by potentially many different deferred work items across
the whole application, not just this one. This is appropriate for short,
occasional callbacks (like this one). It is *not* appropriate for
long-running, continuous, or priority-sensitive work, since all work items
sharing that queue execute sequentially on the same stack — a slow handler
blocks every other pending work item behind it. (Contrast this with a
dedicated thread, which has its own stack and scheduling slot.)

### Two independent flows

`main()`'s `while(1)` loop and the system workqueue operate as two
completely independent flows. `main()` never explicitly waits for or
polls `timeout_handler()` — it only resets the alarm via
`k_work_reschedule()`. Whether and when the timeout actually fires is
entirely the kernel's responsibility, running on a thread the application
code never created.

## What we learned

- **Deferred work items are a lightweight alternative to dedicated threads
  for occasional, short callbacks** — no extra stack, no busy-waiting.
- **`k_work_schedule()` sets an alarm only if none is pending;
  `k_work_reschedule()` always resets it** — the distinction matters for
  correctly implementing a "reset on activity" pattern like this watchdog.
- **The system workqueue is shared infrastructure** — appropriate for
  short/occasional work, not for continuous or priority-sensitive
  processing, since it's a single thread serving potentially many
  unrelated work items across the whole application.
- **Verified on hardware**: the STM32 log showed the timeout firing exactly
  once, at t≈4.0-5.0s, matching the last reschedule (`fake_data_count == 6`)
  plus the 2-second window — confirming the reschedule/timeout mechanism
  behaved exactly as designed.
