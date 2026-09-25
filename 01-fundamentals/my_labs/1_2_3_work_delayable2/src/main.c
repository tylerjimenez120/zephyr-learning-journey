#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(work_lab, LOG_LEVEL_INF);

/* Simulates N independent sensors, each with its own
 * timeout watchdog -- all of them sharing ONE system
 * workqueue stack, instead of needing N dedicated
 * thread stacks. Demonstrates the real memory-saving
 * case: many small, occasional tasks. */

#define NUM_SENSORS 5

struct sensor_watchdog {
    int sensor_id;
    struct k_work_delayable work;
    int data_count;
};

static struct sensor_watchdog sensors[NUM_SENSORS];

static void timeout_handler(struct k_work *work)
{
    /* CONTAINER_OF recovers the full sensor_watchdog struct
     * from the k_work pointer the callback receives -- this
     * is how ONE handler function can serve ALL sensors,
     * knowing WHICH one triggered it. */
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct sensor_watchdog *sensor =
        CONTAINER_OF(dwork, struct sensor_watchdog, work);

    LOG_WRN("Sensor %d: TIMEOUT -- no data in 2 seconds",
            sensor->sensor_id);
}/*
1. Recibo un k_work
   (lo que Zephyr me entrega en el callback)

2. Hago un casteo inteligente y seguro
   (k_work_delayable_from_work — no un casteo ciego,
    uno oficial provisto por Zephyr)

3. Me sitúo en la dirección INICIAL de la estructura
   en base al campo que me diste
   (CONTAINER_OF — usa el offset del campo "work"
    para calcular dónde empieza sensor_watchdog completo)
*/


static void init_sensor_watchdog(struct sensor_watchdog *sensor, int id)
{
    sensor->sensor_id = id;
    sensor->data_count = 0;
    k_work_init_delayable(&sensor->work, timeout_handler);
    k_work_schedule(&sensor->work, K_SECONDS(2));
}/*
K_WORK_DELAYABLE_DEFINE (version basica)
k_work_init_delayable (version multiples sensores)
*/

int main(void)
{
    LOG_INF("Starting %d independent sensor watchdogs, "
            "sharing ONE system workqueue stack", NUM_SENSORS);

    for (int i = 0; i < NUM_SENSORS; i++) {
        init_sensor_watchdog(&sensors[i], i);
    }

    int cycle = 0;

    while (1) {
        k_msleep(500);
        cycle++;

        for (int i = 0; i < NUM_SENSORS; i++) {
            /* Sensor 3 "fails" after cycle 4 -- stops being
             * rescheduled, so its watchdog (and ONLY its
             * watchdog) will eventually fire, independent
             * of the other 4 sensors still working fine. */
            if (i == 3 && cycle > 4) {
                continue;
            }/*
            Cuando i==3 Y cycle>4 se cumple:
            disparar su timeout UNA VEZ
   → sensor 3 se SALTA (continue), deja de
     reprogramarse
            */

            sensors[i].data_count++;
            k_work_reschedule(&sensors[i].work, K_SECONDS(2));
        }

        LOG_INF("Cycle %d: rescheduled watchdogs for all "
                "healthy sensors", cycle);
    }

    return 0;
}


/*
                              main()
                                │
                                ▼
                LOG_INF("Starting 5 independent
                 sensor watchdogs, sharing ONE
                 system workqueue stack")
                                │
                                ▼
                  ┌─────────────────────────┐
                  │ for (i = 0; i < 5; i++)  │
                  │   init_sensor_watchdog   │
                  │     (&sensors[i], i)     │
                  └────────────┬─────────────┘
                                │
             ┌──────────────────┴──────────────────┐
             ▼  (per sensor i)                       │
   sensor->sensor_id  = i                             │
   sensor->data_count = 0                             │
   k_work_init_delayable(&sensor->work,                │
                          timeout_handler)             │
        (RUNTIME init -- one k_work_delayable          │
         per array element, unlike the basic           │
         lab's single compile-time instance)            │
             │                                          │
             ▼                                          │
   k_work_schedule(&sensor->work, K_SECONDS(2))         │
        "alarm set for sensor i: fires at t=2.0s"       │
             │                                          │
             └──────────────────┬──────────────────────┘
                                 ▼  (after all 5 initialized)
                          cycle = 0
                                 │
                                 ▼
                        ┌────────────────┐
                        │  while (1)     │◄──────────────┐
                        └───────┬────────┘                │
                                 ▼                          │
                          k_msleep(500)                     │
                          cycle++                            │
                                 │                            │
                                 ▼                            │
                    ┌─────────────────────────┐              │
                    │ for (i = 0; i < 5; i++)  │              │
                    └────────────┬─────────────┘              │
                                 ▼                              │
                      ┌───────────────────┐                    │
                      │ i == 3 AND          │                   │
                      │ cycle > 4 ?         │                   │
                      └───┬─────────────┬───┘                   │
                          │yes           │no                     │
                          ▼               ▼                      │
                     continue;    sensors[i].data_count++        │
                     (skip --     k_work_reschedule(              │
                      sensor 3's    &sensors[i].work,              │
                      alarm keeps   K_SECONDS(2))                  │
                      counting      "alarm CANCELED and            │
                      down, never   RESET: fires at                │
                      reset again)  t_now+2.0s"                    │
                          │               │                        │
                          └───────┬───────┘                        │
                                  ▼  (after all 5 checked)          │
                        LOG_INF("Cycle %d: rescheduled              │
                         watchdogs for all healthy                  │
                         sensors")                                  │
                                  │                                 │
                                  └─────────────────────────────────┘
                                        (loop repeats forever)


═══════════════════════════════════════════════════════════════
  PARALLEL FLOW — system workqueue [ONE shared thread, serving
  ALL 5 sensor_watchdog work items]
═══════════════════════════════════════════════════════════════

  (idle, waiting)
       │
       │  sensors[0], [1], [2], [4]: main keeps calling
       │  k_work_reschedule() every 500ms cycle FOREVER
       │  -> their alarms never expire
       │
       │  sensor[3]: main STOPS rescheduling after cycle > 4
       │  -> its last-set 2-second alarm keeps counting down
       │     on its own, uninterrupted
       ▼
  2 seconds pass with NO reschedule call for sensor[3]
       │
       ▼
  timeout_handler(work) fires
   -- but WHICH sensor? The handler is shared by all 5.
       │
       ▼
  struct k_work_delayable *dwork =
      k_work_delayable_from_work(work)
       │  (safe, kernel-provided cast: k_work -> k_work_delayable,
       │   because a k_work_delayable CONTAINS a k_work as its
       │   first-level inner field)
       ▼
  struct sensor_watchdog *sensor =
      CONTAINER_OF(dwork, struct sensor_watchdog, work)
       │  (walks BACKWARDS from the "work" field's address to
       │   the START of the sensor_watchdog struct that contains
       │   it, using the compile-time-known offset of "work"
       │   inside struct sensor_watchdog)
       ▼
  LOG_WRN("Sensor %d: TIMEOUT -- no data in 2 seconds",
          sensor->sensor_id)     <-- prints "Sensor 3"
       │
       ▼
  system workqueue goes back to idle, ready to serve the
  NEXT work item from ANY of the 5 sensors (or anywhere
  else in the program)


═══════════════════════════════════════════════════════════════
  KEY POINT: ONE handler function, N independent watchdogs
═══════════════════════════════════════════════════════════════

  timeout_handler() has no idea, by itself, which sensor
  triggered it -- Zephyr only hands it a generic struct k_work
  pointer. CONTAINER_OF is what recovers the full context
  (sensor_id, data_count, etc.) by exploiting the KNOWN memory
  layout of sensor_watchdog: since "work" is a field at a fixed
  offset inside that struct, subtracting that offset from the
  work item's address gives the address of the struct that
  contains it -- regardless of WHICH element of the sensors[]
  array it came from.

  This is what lets 5 independent timeout state machines share
  a single handler function AND a single shared execution
  thread (the system workqueue), instead of needing 5 separate
  handler functions or 5 dedicated thread stacks.
*/