#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(uart_cli_lab, LOG_LEVEL_INF);

#define UART_NODE DT_CHOSEN(zephyr_console)
static const struct device *uart_dev = DEVICE_DT_GET(UART_NODE);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define MSG_BUF_SIZE 32

/* Queue carrying complete lines from the ISR to the main thread --
 * same producer/consumer pattern as Lab 1.2.2. */
K_MSGQ_DEFINE(cmd_queue, MSG_BUF_SIZE, 4, 4);

static char rx_buf[MSG_BUF_SIZE];
static int rx_buf_pos;


static void print_prompt(void)
{
	const char *prompt = "\r\n> ";

	for (int i = 0; prompt[i] != '\0'; i++) {
		uart_poll_out(uart_dev, prompt[i]);
	}
}

static void process_command(const char *cmd)
{
	if (strcmp(cmd, "led on") == 0) {
		gpio_pin_set_dt(&led, 1);
		LOG_INF("LED turned ON via CLI");
	} else if (strcmp(cmd, "led off") == 0) {
		gpio_pin_set_dt(&led, 0);
		LOG_INF("LED turned OFF via CLI");
	} else if (strcmp(cmd, "help") == 0) {
		LOG_INF("Commands: 'led on', 'led off', 'help'");
	} else if (strlen(cmd) > 0) {
		LOG_WRN("Unknown command: '%s'", cmd);
	}
}

/* ISR context -- reads the FIFO and only enqueues complete lines.
 * No LOG_INF, no gpio calls here -- all deferred to the main thread. */
static void uart_isr_callback(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);
	uint8_t c;


    uart_irq_update(dev);


	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while (uart_fifo_read(dev, &c, 1) == 1) {
		if (c == '\r' || c == '\n') {
			rx_buf[rx_buf_pos] = '\0';
			k_msgq_put(&cmd_queue, rx_buf, K_NO_WAIT);
			rx_buf_pos = 0;
		} else if (rx_buf_pos < MSG_BUF_SIZE - 1) {
			rx_buf[rx_buf_pos++] = c;
			uart_poll_out(dev, c); /* local echo */
		}
	}
}

int main(void)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -1;
	}

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED device not ready");
		return -1;
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	uart_irq_callback_set(uart_dev, uart_isr_callback);
	uart_irq_rx_enable(uart_dev);

	LOG_INF("Minimal CLI ready. Type 'help' and press Enter.");
	print_prompt();

	char cmd[MSG_BUF_SIZE];

	while (1) {
		k_msgq_get(&cmd_queue, cmd, K_FOREVER);
		process_command(cmd);
		print_prompt();
	}

	return 0;
}

/*
┌─────────────────────────── BOOT (main thread) ───────────────────────────┐
│ device_is_ready(uart_dev) ?                                              │
│ device_is_ready(led.port) ?                                              │
│        │ (fail -> log error, return)                                    │
│        ▼                                                                 │
│ gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE)                        │
│ uart_irq_callback_set(uart_dev, uart_isr_callback)  -- registers handler │
│ uart_irq_rx_enable(uart_dev)                        -- enables HW IRQ    │
│ print_prompt()                                      -- first "> "       │
└───────────────────────────────────┬──────────────────────────────────────┘
                                     │
                     ┌───────────────┴────────────────┐
                     ▼                                 ▼
     ┌─────────── MAIN THREAD LOOP ───────┐   ┌──── UART ISR (async,     ────┐
     │                                     │   │     fires per keystroke)    │
     │ while (1) {                        │   │                             │
     │   k_msgq_get(&cmd_queue, cmd_buf,  │   │ uart_irq_update(dev)        │
     │               K_FOREVER)           │◄──┼─┐uart_irq_rx_ready(dev)?    │
     │   ─── BLOCKED here until ───       │   │ │   no -> return            │
     │       something is queued          │   │ │   yes ▼                  │
     │                                     │   │ │ while(uart_fifo_read==1){│
     │   // woke up: got a line           │   │ │   if c == '\r'/'\n':     │
     │   process_command(cmd_buf)         │   │ │     rx_buf[pos]='\0'     │
     │   print_prompt()                   │   │ │     k_msgq_put(cmd_queue,│
     │ }                                   │   │ │                rx_buf) ─┼─┐
     └─────────────────────────────────────┘   │ │     pos = 0             │ │
                                                │ │   else if pos < MAX-1:  │ │
                                                │ │     rx_buf[pos++] = c   │ │
                                                │ │     uart_poll_out(c)    │ │
                                                │ │     (local echo)        │ │
                                                │ │ }                       │ │
                                                └─┴─────────────────────────┘ │
                                                                              │
                     ┌────────────────────────────────────────────────────────┘
                     │  k_msgq_put() unblocks the k_msgq_get() above
                     ▼
            (main thread resumes, as shown left)



*/

/*
 * Step-by-step: UART interrupt-driven CLI flow
 *
 * Boot (runs once):
 *
 * 1. device_is_ready(uart_dev) and device_is_ready(led.port) — confirm
 *    both peripherals are ready before use.
 * 2. gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) — configure the
 *    LED pin as output, initially off.
 * 3. uart_irq_callback_set(uart_dev, uart_isr_callback) — register which
 *    function runs on a UART interrupt.
 * 4. uart_irq_rx_enable(uart_dev) — enable RX interrupt generation at
 *    the hardware level.
 * 5. print_prompt() — print the first "> " so the user knows the CLI
 *    is ready.
 * 6. Enter the main while(1) loop and block on
 *    k_msgq_get(&cmd_queue, cmd_buf, K_FOREVER).
 *
 * Repeated cycle, once per keystroke (ISR context):
 *
 * 7.  A byte arrives on UART -> interrupt fires -> uart_isr_callback runs.
 * 8.  uart_irq_update(dev) + uart_irq_rx_ready(dev) — confirm the
 *     interrupt was caused by RX.
 * 9.  while (uart_fifo_read(dev, &c, 1) == 1) — drain whatever byte(s)
 *     are currently in the FIFO.
 * 10. If c is '\r'/'\n': terminate the string (rx_buf[pos] = '\0'),
 *     call k_msgq_put(&cmd_queue, rx_buf), reset pos = 0.
 * 11. Otherwise: store c in rx_buf[pos++] (if there's room) and echo
 *     it locally via uart_poll_out.
 * 12. The ISR exits (FIFO empty) — the main thread stays asleep unless
 *     step 10 just happened.
 *
 * When step 10 happens (full line, Enter pressed):
 *
 * 13. The k_msgq_put call wakes the main thread, which was blocked at
 *     step 6.
 * 14. k_msgq_get copies the queued line into cmd_buf and returns.
 * 15. process_command(cmd_buf) — matches against "led on", "led off",
 *     "help", and executes the corresponding action.
 * 16. print_prompt() — prints "> " again.
 * 17. Back to step 6 (blocked, waiting for the next complete command).
 */