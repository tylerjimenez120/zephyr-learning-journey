# Lab 1.2.2 — Message Queue (k_msgq)

## Objective

Demonstrate producer/consumer communication between two threads using a
Zephyr message queue (`k_msgq`), and observe backpressure behavior when the
producer's rate exceeds the consumer's rate.

## Theory

### Why a message queue instead of a shared variable + mutex

Lab 1.2.1 protected a *single* shared value with a mutex — both threads
needed the same, current value. Here the requirement is different: the
producer generates a *stream* of discrete readings, and the consumer must
process each one, in order, without losing data due to a race. A mutex
alone doesn't solve this — you'd need extra bookkeeping (flags, indices) to
avoid the consumer reading the same value twice or missing one entirely. A
message queue solves this natively: it's a bounded FIFO with built-in
synchronization.

### `K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_reading), 5, 4)`

- **`sensor_queue`** — the queue object, statically allocated.
- **`sizeof(struct sensor_reading)`** — size of each message slot. The
  queue is copy-based: `k_msgq_put()` copies the struct's bytes into the
  queue's internal buffer, `k_msgq_get()` copies them back out. There's no
  pointer sharing, so the producer's local `reading` variable can safely go
  out of scope or be reused right after `k_msgq_put()` returns.
- **`5`** — maximum number of queued messages (queue depth).
- **`4`** — byte alignment for each message slot.

### `K_NO_WAIT` vs `K_FOREVER` — two different philosophies

- **Producer uses `K_NO_WAIT`**: if the queue is full, `k_msgq_put()`
  returns immediately with a non-zero error code instead of blocking. This
  is deliberate: for real-time sensor data, a producer thread blocking
  because the queue is full would stall sensor sampling itself, and a
  *stale* reading delivered late is often worse than a reading dropped
  outright. The producer logs a warning and moves on to the next
  300ms cycle.
- **Consumer uses `K_FOREVER`**: it has nothing else to do until a message
  arrives, so blocking indefinitely is correct and efficient — the thread
  is simply removed from the run queue (using zero CPU) until
  `k_msgq_put()` on the other side wakes it up.

### Backpressure

The producer produces every 300ms; the consumer takes 500ms to "process"
each message. Over time the consumer falls behind, the queue's 5 slots
fill up, and `k_msgq_put()` starts failing. This is intentional: it
demonstrates what happens at a *bounded* queue's limit under sustained rate
mismatch — a realistic scenario in embedded systems where a fast sensor
outpaces a slower processing/transmission stage (e.g., a slow UART, a rate
-limited network link).

## What we learned

- **A message queue solves ordered, discrete data transfer better than a
  raw shared variable** — no manual bookkeeping needed to avoid duplicate
  reads or lost writes between producer and consumer.
- **`K_NO_WAIT` and `K_FOREVER` are not interchangeable defaults** — the
  choice depends on what each side of the queue can afford to do when the
  queue is at its limit. A producer for real-time data usually can't afford
  to block; a consumer with no other job usually can.
- **Verified on hardware**: the STM32 log showed explicit
  `queue full, reading dropped` warnings, and the consumer's received IDs
  skipped values (e.g., `80, 82, 84` — missing `81`, `83`), confirming that
  dropped messages under backpressure behaved exactly as designed, with no
  silent corruption or duplication.
- **Bounded queues make backpressure visible and explicit**, rather than
  hiding it behind unbounded growth (which an unbounded queue/heap-based
  buffer would do, at the cost of unpredictable memory usage — undesirable
  in a memory-constrained embedded target).
