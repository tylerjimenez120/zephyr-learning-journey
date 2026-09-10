#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

/* Registers this file as a logging "module" with its own
 * name -- this way logs are identified by source, useful
 * once you have SEVERAL .c files generating logs */
LOG_MODULE_REGISTER(hello_log, LOG_LEVEL_DBG);

int main(void)
{
    printk("Hello via printk -- simple, no severity levels\n");

    LOG_ERR("This is an ERROR level log");
    LOG_WRN("This is a WARNING level log");
    LOG_INF("This is an INFO level log");
    LOG_DBG("This is a DEBUG level log");

    int counter = 0;
    while (1) {
        LOG_INF("Counter: %d", counter++);
        k_sleep(K_SECONDS(1));
    }

    return 0;
}