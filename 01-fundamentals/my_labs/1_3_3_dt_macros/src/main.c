#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dt_macros_lab, LOG_LEVEL_INF);

#define APP_NODE DT_PATH(zephyr_user)
#define BLINK_PERIOD_MS DT_PROP(APP_NODE, blink_period_ms)

#define LED1_NODE DT_ALIAS(led1)
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

int main(void)
{
	LOG_INF("Blink period read from DT: %d ms", BLINK_PERIOD_MS);

	/* Compile-time check -- confirms in code what Lab 1.3.1 found
	 * via grep: i2c2 is disabled by default on this board. */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c2), okay)
	LOG_INF("i2c2 is ENABLED in this build");
#else
	LOG_INF("i2c2 is DISABLED in this build (matches Lab 1.3.1 findings)");
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(usart2), okay)
	LOG_INF("usart2 is ENABLED in this build (it's our console)");
#endif

	if (!gpio_is_ready_dt(&led1)) {
		LOG_ERR("External LED device not ready");
		return -1;
	}

	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);

	while (1) {
		gpio_pin_toggle_dt(&led1);
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}