# Lab 1.2.3 (extended) — Multiple Independent Watchdogs, One Handler

## Objective

Scale the deferred-work watchdog pattern from a single timeout to N
independent sensor watchdogs (5, in this lab), all sharing a single
handler function and the same system workqueue thread — and recover
per-instance context (`sensor_id`) inside that shared handler using
`CONTAINER_OF`.

## Theory

### Why runtime init instead of `K_WORK_DELAYABLE_DEFINE`

The basic version of this lab used `K_WORK_DELAYABLE_DEFINE`, a
compile-time macro that works for exactly one, statically-known work
item. Here there are 5 (`NUM_SENSORS`), stored in an array
(`static struct sensor_watchdog sensors[NUM_SENSORS]`). Since the number
and identity of instances is only known once the array exists (even
though the array size is a compile-time constant here, the pattern
generalizes to configuration-driven counts), each one is initialized at
runtime with `k_work_init_delayable(&sensor->work, timeout_handler)`
inside a loop, instead of one static macro invocation per sensor.

### One handler, many instances: the problem

`timeout_handler()` is registered once and shared by all 5
`k_work_delayable` instances. When it fires, Zephyr only passes it a
generic `struct k_work *work` pointer — there is no built-in way to know,
from that pointer alone, which sensor's watchdog fired, or to access that
sensor's `sensor_id` or `data_count`.

### `k_work_delayable_from_work()` and `CONTAINER_OF`

Two nested recovery steps solve this:

1. **`k_work_delayable_from_work(work)`** — a safe, kernel-provided
   conversion from `struct k_work *` to `struct k_work_delayable *`. This
   is needed (rather than a direct cast) because `k_work_delayable`
   internally wraps a `k_work` alongside extra delay/timeout bookkeeping;
   the kernel function handles that structure correctly rather than
   assuming a raw reinterpretation is safe.

2. **`CONTAINER_OF(dwork, struct sensor_watchdog, work)`** — given a
   pointer to the `work` field *inside* a `sensor_watchdog` struct, and
   knowing `work`'s fixed byte offset within that struct (computed at
   compile time via `offsetof`), this macro walks backwards from the
   field's address to the address where the *containing* struct begins.
   The result is a pointer to the specific `sensor_watchdog` instance
   whose `work` field triggered the callback — recovering `sensor_id`,
   `data_count`, and everything else about that particular sensor.

This two-level nesting exists because `struct sensor_watchdog` contains a
`k_work_delayable`, which itself contains a `k_work` — the callback only
ever sees the innermost `k_work`, so both steps are needed to walk back
out to the original application-level struct.

### Selective non-rescheduling (`sensor[3]` "failing")

The main loop reschedules every sensor's watchdog every 500ms cycle,
*except* sensor 3 once `cycle > 4`. This isolates one sensor's alarm from
the others: sensors 0, 1, 2, and 4 keep getting rescheduled forever (their
alarms never expire), while sensor 3's last-set 2-second alarm is left to
count down uninterrupted, firing exactly once. This demonstrates that each
`k_work_delayable` instance is fully independent — one firing doesn't
affect, cancel, or interfere with the others, even though they all share
the same handler function and the same underlying workqueue thread.

## What we learned

- **`k_work_init_delayable()` (runtime) is the array/dynamic-count
  counterpart to `K_WORK_DELAYABLE_DEFINE()` (compile-time, single
  instance)** — same underlying mechanism, different initialization site
  depending on whether the instance count is known statically or needs a
  loop.
- **A single handler function can safely serve any number of independent
  work item instances**, as long as each instance's context is recoverable
  from the `k_work` pointer alone — which is exactly what `CONTAINER_OF`
  provides, using the struct's known memory layout rather than any
  explicit "which instance is this" parameter.
- **The system workqueue remains a single shared thread even at N=5** —
  the memory-saving case scales: 5 independent timeout state machines,
  zero dedicated thread stacks beyond the one workqueue already provided
  by the kernel.
- **Independent scheduling state per instance**: rescheduling (or not
  rescheduling) one sensor's watchdog has zero effect on the others' timers
  — each `k_work_delayable` tracks its own fire time independently, even
  while sharing the handler function and the execution thread.
- **Verified on hardware**: the STM32 log showed `Sensor 3: TIMEOUT` firing
  exactly once, at t=4.0s (cycle 8), while sensors 0, 1, 2, and 4 kept
  cycling normally afterward with no repeat of the warning — confirming
  one-shot firing for sensor 3 and permanent inactivity of its `k_work`
  from that point on, with zero interference to the other sensors.
