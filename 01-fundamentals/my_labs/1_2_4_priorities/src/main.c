#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(priority_lab, LOG_LEVEL_INF);

#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(high_prio_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(low_prio_stack, STACK_SIZE);

static struct k_thread high_prio_data;
static struct k_thread low_prio_data;

/* Shared flag -- low priority thread sets it to signal
 * "I am currently running", high priority checks it to
 * prove it interrupts low priority mid-execution */
static volatile int low_prio_iteration = 0;

void high_prio_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        LOG_INF("HIGH priority running (low was at iteration %d)",
                low_prio_iteration);
        k_msleep(1000);
    }
}

void low_prio_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        low_prio_iteration++;

        /* Busy-work loop (no sleep) -- CPU-bound, on purpose,
         * to show preemptive scheduling forcibly interrupting
         * it even though it never yields voluntarily */
        for (volatile int i = 0; i < 500000; i++) {
            /* burn CPU cycles */
        }

        LOG_INF("LOW priority finished iteration %d", low_prio_iteration);
    }
}

int main(void)
{
    LOG_INF("Starting priority demo: HIGH=preempt prio 2, "
            "LOW=preempt prio 7");

    /* Lower number = higher priority. Both preemptive
     * (positive numbers). */
    k_thread_create(&high_prio_data, high_prio_stack, STACK_SIZE,
                     high_prio_entry, NULL, NULL, NULL,
                     2 /* priority */, 0, K_NO_WAIT);

    k_thread_create(&low_prio_data, low_prio_stack, STACK_SIZE,
                     low_prio_entry, NULL, NULL, NULL,
                     7 /* priority */, 0, K_NO_WAIT);

    return 0;
}

/*
                              main()
                                │
                                ▼
                LOG_INF("Starting priority demo:
                 HIGH=preempt prio 2, LOW=preempt prio 7")
                                │
        ┌───────────────────────┴───────────────────────┐
        │                                                │
  k_thread_create(HIGH)                          k_thread_create(LOW)
  priority 2, K_NO_WAIT                           priority 7, K_NO_WAIT
  (lower number = HIGHER prio)                     (higher number = LOWER prio)
        │                                                │
        ▼                                                ▼
┌──────────────────────┐                       ┌──────────────────────┐
│  HIGH_PRIO THREAD     │                       │  LOW_PRIO THREAD      │
│  (high_prio_entry)    │                       │  (low_prio_entry)     │
└───────────┬────────────┘                       └───────────┬────────────┘
            │                                                 │
            ▼                                                 ▼
     ┌─────────────┐                                   ┌─────────────┐
     │ while (1)   │◄─────────┐                          │ while (1)   │◄─────────┐
     └──────┬──────┘          │                          └──────┬──────┘          │
            ▼                 │                                 ▼                 │
   LOG_INF("HIGH running      │                       low_prio_iteration++         │
    (low was at               │                                 │                 │
    iteration %d)",           │                                 ▼                 │
    low_prio_iteration)       │                       for (i = 0; i < 500000; i++) │
            │                 │                          /* burn CPU cycles --    │
            ▼                 │                             NO k_yield/k_msleep   │
   k_msleep(1000)             │                             anywhere in this      │
   (thread BLOCKS/SLEEPS --   │                             loop: this thread     │
    fully removed from the    │                             never voluntarily     │
    ready queue; scheduler    │                             gives up the CPU) */  │
    ignores it completely     │                                 │                 │
    for this 1s window,       │                                 ▼                 │
    regardless of priority)   │                       LOG_INF("LOW finished       │
            │                 │                        iteration %d")             │
            └─────────────────┘                                 │                 │
                                                                  └─────────────────┘
                                                                  (loop repeats,
                                                                   CPU-bound, never
                                                                   sleeps)


═══════════════════════════════════════════════════════════════
  WHAT ACTUALLY HAPPENS ON THE CPU (single core, two threads
  competing for it)
═══════════════════════════════════════════════════════════════

  t=0.000s  main() creates both threads, returns
              -> scheduler picks HIGH first (prio 2 beats
                 LOW's prio 7)
  t=0.000s  HIGH: LOG_INF(...), then k_msleep(1000) -> BLOCKS
              -> HIGH is now off the ready queue entirely
  t=0.000s  scheduler has nothing higher-priority ready ->
              LOW runs
  t=0.000s..0.999s   LOW runs CONTINUOUSLY: increments,
              busy-waits 500000 iterations, logs, repeats --
              over and over, with NO gaps, because nothing
              higher-priority is ready to preempt it
  t=1.000s  HIGH's k_msleep(1000) timeout expires -> kernel
              moves HIGH from "sleeping" back to "ready"
              -> HIGH's priority (2) beats LOW's (7)
              -> scheduler PREEMPTS LOW IMMEDIATELY, mid-loop
                 (even mid busy-wait, NOT waiting for LOW's
                 current iteration to finish)
  t=1.000s  HIGH runs: LOG_INF(..., low_prio_iteration) --
              reads whatever value LOW had already written
              before being cut off
  t=1.000s  HIGH calls k_msleep(1000) again -> BLOCKS again
  t=1.000s  LOW resumes EXACTLY where it left off (mid
              busy-wait), finishes that iteration, logs
              "finished iteration N"
  t=1.000s..1.999s   LOW runs uninterrupted again
  t=2.000s  cycle repeats


═══════════════════════════════════════════════════════════════
  KEY POINTS
═══════════════════════════════════════════════════════════════

  - k_msleep() BLOCKS the calling thread -- it's removed from
    scheduling consideration entirely, regardless of its
    priority, until the timeout expires. This is why LOW gets
    ~1 full second of uninterrupted CPU time between HIGH's
    runs: HIGH isn't "letting" LOW run, HIGH is simply ASLEEP
    and not competing for the CPU at all during that window.

  - When HIGH's sleep expires, preemption is IMMEDIATE and
    happens at essentially any instruction boundary -- LOW's
    busy-wait for-loop has no yield points, yet HIGH still cuts
    in mid-loop. This is the defining property of PREEMPTIVE
    scheduling (positive priority numbers in Zephyr): the
    kernel forcibly switches threads based on priority, without
    the lower-priority thread's cooperation.

  - HIGH's LOW participation share of CPU time is small
    (~1 log line + immediate re-sleep, once per second) not
    because of priority, but because of ITS OWN k_msleep(1000)
    call -- priority only decides who wins WHEN BOTH THREADS
    ARE READY to run at the same time; it says nothing about
    how much a thread chooses to sleep.
*/