#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(work_lab, LOG_LEVEL_INF);

/* Simulates a sensor read-timeout watchdog: every time
 * "new data" arrives, we reset the timer. If 2 seconds
 * pass with NO new data, the deferred work fires and
 * reports "sensor timeout". */

static void timeout_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_WRN("Sensor timeout! No data received in 2 seconds");
}

/* K_WORK_DELAYABLE_DEFINE creates the deferred work item,
 * statically, at compile time -- same philosophy as the
 * static thread stacks from earlier labs. */
K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_handler);

int main(void)
{
    LOG_INF("Starting sensor timeout watchdog demo");

    /* Schedule the first timeout check 2 seconds from now */
    k_work_schedule(&timeout_work, K_SECONDS(2));

    int fake_data_count = 0;

    while (1) {
        k_msleep(500);

        fake_data_count++;

        /* Simulate: sensor data arrives normally for the
         * first few iterations, then "stops" (stops
         * rescheduling the timeout) to demonstrate the
         * watchdog actually firing. */
        if (fake_data_count <= 6) {
            LOG_INF("Sensor data received (%d)", fake_data_count);

            /* Data arrived -- push the timeout back another
             * 2 seconds. This is the "no thread needed"
             * pattern: no busy-waiting, no dedicated thread,
             * just rescheduling a deferred callback. */
            k_work_reschedule(&timeout_work, K_SECONDS(2));
        } else {
            LOG_INF("(sensor stopped responding -- not "
                     "rescheduling timeout)");
        }
    }

    return 0;
}

/*
═══════════════════════════════════════════════════════════════
  EXECUTION FLOW — main.c (Lab 1.2.3, basic version)
═══════════════════════════════════════════════════════════════

  main() [main thread]
    │
    ├─► K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_handler)
    │      (happens at COMPILE TIME, not runtime --
    │       "tags" timeout_handler to run on the
    │       system workqueue, which already exists)
    │
    ├─► k_work_schedule(&timeout_work, K_SECONDS(2))
    │      "alarm set: will fire at t=2.0s"
    │      (does NOT block -- main keeps running immediately)
    │
    ▼
  while(1) [main's loop, runs FOREVER]
    │
    ├─► k_msleep(500)
    ├─► fake_data_count++
    │
    ├─► fake_data_count <= 6 ?
    │        │
    │       YES                             NO
    │        │                               │
    │        ▼                               ▼
    │  LOG_INF("data received")      LOG_INF("sensor stopped")
    │        │                               │
    │        ▼                          (timeout is NOT
    │  k_work_reschedule(               rescheduled -- the
    │    &timeout_work,                 alarm set the LAST
    │    K_SECONDS(2))                  time keeps counting
    │        │                          down on its own)
    │  "alarm CANCELED and
    │   reset: will now
    │   fire at t_now+2.0s"
    │        │
    │        └──────────┬─────── loop repeats ───────┘


═══════════════════════════════════════════════════════════════
  PARALLEL FLOW — system workqueue [separate, pre-existing thread]
═══════════════════════════════════════════════════════════════

  (idle, waiting -- shared by ANY k_work in the whole system)
    │
    │   [main thread reschedules the alarm every 500ms,
    │    as long as fake_data_count <= 6 -- each
    │    reschedule pushes the fire time further out]
    │
    │   [main thread STOPS rescheduling after count > 6]
    │
    ▼
  2 seconds pass with NO new reschedule call
    │
    ▼
  timeout_handler(work) fires automatically
    │
    ├─► ARG_UNUSED(work)
    └─► LOG_WRN("Sensor timeout! No data received in 2 seconds")
    │
    ▼
  system workqueue goes back to idle
  (ready for the next k_work from ANYWHERE in the program)


═══════════════════════════════════════════════════════════════
  KEY POINT
═══════════════════════════════════════════════════════════════

  main()'s while(1) loop and the system workqueue run as
  TWO INDEPENDENT FLOWS. main() never explicitly waits for
  or checks on timeout_handler() -- it only "resets the
  alarm" by calling k_work_reschedule(). Whether and when
  timeout_handler() actually fires is handled entirely by
  Zephyr's kernel, in the background, on a thread the
  programmer never created.
*/