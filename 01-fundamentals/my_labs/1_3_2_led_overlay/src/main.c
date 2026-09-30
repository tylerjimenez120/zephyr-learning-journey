#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_overlay_lab, LOG_LEVEL_INF);

/* DT_ALIAS(led1) resolves to the node our overlay declared --
 * proves the overlay's alias is visible from C exactly like the
 * board's own built-in led0 alias. */
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

int main(void)
{
	if (!gpio_is_ready_dt(&led1)) {
		LOG_ERR("External LED device not ready");
		return -1;
	}

	int ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure external LED: %d", ret);
		return ret;
	}

	LOG_INF("Toggling external LED (PA1) via overlay-defined alias");

	while (1) {
		gpio_pin_toggle_dt(&led1);
		k_msleep(500);
	}

	return 0;
}