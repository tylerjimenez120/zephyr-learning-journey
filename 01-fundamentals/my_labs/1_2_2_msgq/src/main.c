#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(msgq_lab, LOG_LEVEL_INF);

/* Message structure: what the producer sends, the
 * consumer receives */
struct sensor_reading {
    int id;
    int value;
};

/* Queue definition: holds up to 5 messages, each of
 * size sizeof(struct sensor_reading), 4-byte aligned */
K_MSGQ_DEFINE(sensor_queue, sizeof(struct sensor_reading), 5, 4);

#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(producer_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(consumer_stack, STACK_SIZE);

static struct k_thread producer_data;
static struct k_thread consumer_data;

void producer_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int reading_id = 0;

    while (1) {
        struct sensor_reading reading = {
            .id = reading_id++,
            .value = (reading_id * 7) % 100  /* fake sensor value */
        };

        LOG_INF("Producer: sending reading id=%d value=%d",
                reading.id, reading.value);

        /* K_NO_WAIT: if the queue is full, drop the message
         * instead of blocking -- appropriate for real-time
         * sensor data where a stale reading is worse than
         * a dropped one */
        int ret = k_msgq_put(&sensor_queue, &reading, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("Producer: queue full, reading dropped");
        }

        k_msleep(300);
    }
}

void consumer_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct sensor_reading reading;

    while (1) {
        /* K_FOREVER: block until a message is available --
         * the consumer has nothing else to do meanwhile */
        k_msgq_get(&sensor_queue, &reading, K_FOREVER);

        LOG_INF("Consumer: received reading id=%d value=%d",
                reading.id, reading.value);

        /* Simulate slower processing than production rate,
         * to eventually demonstrate queue backpressure */
        k_msleep(500);
    }
}

int main(void)
{
    k_thread_create(&producer_data, producer_stack, STACK_SIZE,
                     producer_entry, NULL, NULL, NULL,
                     5, 0, K_NO_WAIT);

    k_thread_create(&consumer_data, consumer_stack, STACK_SIZE,
                     consumer_entry, NULL, NULL, NULL,
                     5, 0, K_NO_WAIT);

    return 0;
}


/*
                              main()
                                │
        ┌───────────────────────┴───────────────────────┐
        │                                                │
  k_thread_create(producer)                    k_thread_create(consumer)
  prio 5, K_NO_WAIT                              prio 5, K_NO_WAIT
        │                                                │
        ▼                                                ▼
┌────────────────────┐                        ┌────────────────────┐
│  PRODUCER THREAD    │                        │  CONSUMER THREAD    │
│  (producer_entry)   │                        │  (consumer_entry)   │
└──────────┬───────────┘                        └──────────┬───────────┘
           │                                                │
           ▼                                                ▼
    ┌─────────────┐                                  ┌─────────────┐
    │ while (1)   │◄──────────┐                        │ while (1)   │◄──────────┐
    └──────┬──────┘           │                        └──────┬──────┘           │
           ▼                  │                               ▼                  │
  build sensor_reading        │                    k_msgq_get(&sensor_queue,     │
  { id = reading_id++,        │                        &reading, K_FOREVER)      │
    value = (id*7)%100 }      │                               │                  │
           │                  │                        ┌──────┴──────┐           │
           ▼                  │                        │ queue empty?│           │
  LOG_INF("Producer:          │                        └──┬───────┬──┘           │
     sending id=%d ...")      │                           │yes    │no             │
           │                  │                           ▼       ▼               │
           ▼                  │                        BLOCKS   dequeue          │
  k_msgq_put(&sensor_queue,   │                        (waits)  message          │
      &reading, K_NO_WAIT)    │                           │       │               │
           │                  │                           │       ▼               │
    ┌──────┴──────┐           │                           │  LOG_INF("Consumer:  │
    │ queue full? │           │                           │     received id=%d..")│
    └──┬───────┬──┘           │                           │       │               │
       │yes    │no             │                           │       ▼               │
       ▼       ▼               │                           │  k_msleep(500)       │
  LOG_WRN     message          │                           │  (slower than the    │
  ("queue     enqueued         │                           │   producer's 300ms   │
   full,      successfully     │                           │   rate -> queue      │
   reading                     │                           │   fills up over      │
   dropped")                   │                           │   time)              │
       │       │               │                           │       │               │
       └───────┴───────────────┤                           └───────┴───────────────┤
                 ▼              │                                    ▼              │
           k_msleep(300)        │                              (loop back to        │
                 │              │                               k_msgq_get)         │
                 └──────────────┘                                                  │
                                                                                     │
                                                            back to while(1) ────────┘


                    THE QUEUE ITSELF: K_MSGQ_DEFINE(sensor_queue,
                    sizeof(struct sensor_reading), 5, 4)

     Producer (300ms/msg)  ──put──►  [ ][ ][ ][ ][ ]  ──get──►  Consumer (500ms/msg)
                                      5-slot ring buffer

     Production rate (300ms) is FASTER than consumption rate (500ms)
     -> the queue fills up over time -> k_msgq_put() starts returning
     non-zero -> producer drops messages instead of blocking.
*/