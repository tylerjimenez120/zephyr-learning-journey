# Zephyr RTOS — Learning Journey

A documented, growing journey through Zephyr RTOS — from a clean environment to (eventually) drivers, power management, networking, BLE, security, and OTA — written up as I go, real errors included.

**Hardware used**: STM32 Nucleo-F411RE, ESP32 (DevKit V1 clone), ADXL345.
**Prior experience**: bare-metal STM32, HAL, FreeRTOS, ESP-IDF, TinyML on-device inference, industrial protocols (Modbus RTU, CAN Bus) — all covered in other repos, all referenced here where relevant.

---

## Why Zephyr

Every other project in my portfolio used a vendor-specific stack: STM32 HAL, ESP-IDF. Each one ties the code to one chip family. Zephyr is different — it borrows Linux's Device Tree model, so the *same application code* runs unmodified across completely different silicon (ARM Cortex-M, Xtensa, RISC-V), with hardware description living in a separate `.dts` file per board.

```
| Aspect              | Vendor SDK (HAL/ESP-IDF) | Zephyr                    |
|---------------------|---------------------------|----------------------------|
| Driver architecture | Ad-hoc per vendor         | Unified Device Driver Model |
| Hardware description| Manual init calls in C    | Device Tree (like Linux)   |
| Portability         | None across vendors       | Same app, multiple chips   |
| Security             | Manual                    | PSA Certified, TF-M native  |
```

---

## Structure

Each numbered folder is a self-contained stage, with its own README and labs.

```
01-fundamentals/    Setup, first apps on STM32 + ESP32, Device Tree basics
02-drivers/          (upcoming) Writing a Zephyr driver for the ADXL345
03-power/            (upcoming) Power management, Deep Sleep equivalent
04-networking/       (upcoming) TCP/MQTT/TLS over Zephyr's IP stack
05-ble/              (upcoming) Bluetooth LE with Zephyr's native stack
06-security-ota/     (upcoming) MCUboot, OTA, PSA crypto
```

---

## Part 1 — Fundamentals

**[→ Go to 01-fundamentals/](01-fundamentals/)**

Environment setup (with every real error hit along the way, documented), the same `main.c` running unmodified on both an STM32 and an ESP32, and a full breakdown of what a Device Tree actually does — line by line.

---

## Following along

This repo grows as the journey continues — each new stage gets its own folder and README, cross-linked from here.
