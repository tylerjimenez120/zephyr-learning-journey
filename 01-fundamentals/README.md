# Part 1 — Fundamentals

Environment setup, first applications on two different chips, and a first real look at Device Tree.

---

## What's here

```
my_labs/
└── 1_1_3_hello_log/     printk + structured logging (LOG_INF/WRN/ERR/DBG),
                          the same main.c compiled for both STM32 and ESP32

docs/
└── setup_troubleshooting.md   Every real error hit setting up the environment,
                                 with the exact fix for each
```

---

## What was verified

### Same code, two chips

```c
// This exact file compiles and runs, unmodified, on both boards:
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
```

```bash
west build -b nucleo_f411re my_labs/1_1_3_hello_log -p always
west build -b doit_esp32_devkit_v1/esp32/procpu my_labs/1_1_3_hello_log -p always
```

Neither board-specific pin, neither vendor API — `printk`, `LOG_INF`, and `k_sleep` are Zephyr's generic kernel API. What UART it prints to, what pin a `led0` alias resolves to — that's entirely defined per-board in the Device Tree, not in application code.

### The `doit_esp32_devkit_v1` LED mismatch

The stock `blinky` sample compiled and ran correctly on the ESP32 (confirmed via serial log: `LED state: ON/OFF` alternating every 100ms) — but no physical LED lit up. The board's Device Tree expects an LED on GPIO2:

```dts
blue_led: blue_led {
    gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
};
```

Generic ESP32 DevKit clones vary by manufacturer — not all of them wire an onboard LED to that exact pin, some don't include one at all. The firmware logic was verified correct via the serial log regardless; the missing visual confirmation was a hardware assumption in the generic board definition, not a code or toolchain issue.

### Reading the Nucleo-F411RE Device Tree

Key finding, connecting directly to earlier bare-metal work: the board's `chosen` node sets `zephyr,console = &usart2;` — USART2 (PA2/PA3) is the same UART shared with the ST-Link's virtual COM port that had to be avoided in an earlier bare-metal Modbus project (which used USART1 on D8/D2 instead, specifically to keep USART2 free for debug). Zephyr deliberately reuses that same USART2/ST-Link link for its console — same hardware constraint, opposite design choice, both valid depending on whether the debug UART is needed for something else simultaneously.

The `aliases` block is what makes the portable `blinky`/`hello_log` code possible:
```dts
aliases {
    led0 = &green_led_2;   // → PA5, GPIO_ACTIVE_HIGH
    sw0 = &user_button;
};
```
Application code asks for the generic alias `led0`; each board's Device Tree resolves it to that board's actual pin.

---

## Setup

See **[docs/setup_troubleshooting.md](docs/setup_troubleshooting.md)** for the full environment setup with every error encountered and its fix — Python version incompatibility, missing pip dependencies (discovered one at a time), an outdated CMake, and a missing flashing tool.

```bash
python3.12 -m venv .venv
source .venv/bin/activate
pip install west
west init .
west update
west sdk install   # after resolving the dependencies below
```
