# Lab 1.4.2 — Minimal UART CLI with interrupt-driven reception

## Objective

Implement a minimal serial CLI using Zephyr's interrupt-driven UART API
(`uart_irq_*`), with a producer/consumer pattern (ISR → `k_msgq` → main
thread) to decouple byte capture from command processing — reusing the
same decoupling scheme seen in Lab 1.2.2.

## Theory

### The three tiers of UART API in Zephyr

- **Polling** (`uart_poll_in`/`uart_poll_out`) — one byte at a time,
  blocking, no interrupts. Used in this lab only for output (prompt and
  echo), where briefly blocking the main thread is acceptable.
- **Interrupt-driven** (`uart_irq_*`, requires
  `CONFIG_UART_INTERRUPT_DRIVEN=y`) — the hardware signals when data is
  ready; the CPU does no active polling. Used here for reception.
- **Async** (DMA-based) — not covered in this lab.

### `DT_CHOSEN(zephyr_console)` and `DT_ALIAS(led0)` — no overlay needed

Both macros resolve against properties already declared in
`nucleo_f411re.dts` out of the box: `chosen { zephyr,console = &usart2; }`
and the `led0` alias. Same "already declared, no overlay needed" pattern
as `led0`/`sw0` in Lab 1.4.1 — an overlay is only required to declare
hardware the board file doesn't already describe.

### The producer/consumer flow

```
BOOT (runs once)
  device_is_ready(uart) / device_is_ready(led)
  gpio_pin_configure_dt(&led, OUTPUT_INACTIVE)
  uart_irq_callback_set(uart_dev, uart_isr_callback)
  uart_irq_rx_enable(uart_dev)
  print_prompt()
  -> while(1): k_msgq_get(&cmd_queue, cmd_buf, K_FOREVER)   [blocked]

PER KEYSTROKE (ISR context, async relative to the main thread)
  UART interrupt fires -> uart_isr_callback
  uart_irq_update(dev)
  if (!uart_irq_rx_ready(dev)) return
  while (uart_fifo_read(dev, &c, 1) == 1):
      if c is '\r'/'\n':
          rx_buf[pos] = '\0'
          k_msgq_put(&cmd_queue, rx_buf, K_NO_WAIT)   -> wakes the main thread
          pos = 0
      else (if there's room, pos < MSG_BUF_SIZE-1):
          rx_buf[pos++] = c
          uart_poll_out(dev, c)   -- local echo

WHEN A FULL LINE IS QUEUED (main thread)
  k_msgq_get returns -> cmd_buf holds the line
  process_command(cmd_buf)   -- "led on" / "led off" / "help"
  print_prompt()
  -> blocks again on k_msgq_get
```

### Why the ISR's `while` doesn't break the "short ISR" rule

The STM32F411's USART hardware FIFO is small (practically no depth), and
UART is a serial protocol: bytes arrive one at a time at the baudrate's
pace, never in bulk. That's why the ISR fires once per keystroke (or a
handful of bytes), and the inner `while` only drains whatever is already
available at that instant — it never waits for more to arrive. The work
per invocation is O(1) and bounded.

**Design nuance worth flagging:** this is a lab-specific pedagogical
simplification, not general best practice. The stricter, more defensive
discipline (the right one for production) is that an ISR should **only
notify** (e.g. `k_sem_give`) without touching buffers or doing logic —
with all line-ending decisions made in a separate task. This lab
prioritizes demonstrating the `uart_irq_*` API directly.

### The overflow guard

```c
} else if (rx_buf_pos < MSG_BUF_SIZE - 1) {
    rx_buf[rx_buf_pos++] = c;
    uart_poll_out(dev, c);
}
```

If the buffer is already full, the incoming byte is silently dropped —
without this guard, a line longer than `MSG_BUF_SIZE` would write past
`rx_buf`'s bounds, corrupting adjacent memory.

### `K_MSGQ_DEFINE(cmd_queue, MSG_BUF_SIZE, 4, 4)`

Four parameters: name, byte size per element, max depth (how many
pending lines it can hold), internal buffer alignment. The queue handles
mutual exclusion between the interrupt context (producer) and the thread
context (consumer) internally — without it there would be a race
condition.

## Build issues encountered (and root cause)

- **Empty `CMakeLists.txt`** → CMake with no `project()`, `ninja: no work
  to do`. Cause: the initial scaffolding left the file empty, missing the
  Zephyr boilerplate (`find_package(Zephyr...)`, `project(...)`,
  `target_sources`).
- **`BOARD is not being defined`** after fixing the CMakeLists → stale
  build cache from the previous attempt (which never cached `BOARD`
  since it had no `project()`). Fixed with `rm -rf build` instead of
  relying on `-p always`.
- **`invalid use of void expression` on `uart_irq_update(dev)`** → in
  this installed Zephyr version, `uart_irq_update()` returns `void`, not
  `int`. Fixed by separating the call (`uart_irq_update(dev);`) from the
  check (`if (!uart_irq_rx_ready(dev)) return;`), removing the
  dependency on its return value.

## Verification (real hardware, serial log)

```
dsdsd
<wrn> uart_cli_lab: Unknown command: 'dsdsd'

led on
<inf> uart_cli_lab: LED turned ON via CLI

led off
<inf> uart_cli_lab: LED turned OFF via CLI
```

Confirmed: unknown command correctly detected, LED physically
turning on/off in response to text commands received over UART with
fully interrupt-driven reception (zero polling on the RX path).

## What we learned

- Interrupt-driven reception fully decouples byte capture (ISR context)
  from command processing (thread context) — same producer/consumer
  pattern from 1.2.2, now triggered by real hardware instead of a
  software timer.
- `k_msgq` resolves synchronization between both contexts without manual
  mutexes — the main thread wakes up only when a complete line is
  available.
- A "short" ISR doesn't necessarily mean zero logic — it means bounded,
  non-blocking work. But the stricter, more defensive practice (ISR =
  notify only, all logic deferred) remains the recommended one outside a
  focused API-learning context.
- An empty `CMakeLists.txt` or a stale `build/` directory from a prior
  attempt can produce CMake errors with no apparent connection to the C
  code — always suspect the build state first before assuming a logic
  bug.
