# Lab 1.4.3 — I2C with the ADXL345 using raw register access

## Objective

Talk to an I2C sensor (ADXL345 accelerometer) through Zephyr's generic I2C
API, reading and writing its registers directly — no Sensor API, no in-tree
driver. The sensor is described in a Devicetree overlay and the code
resolves bus + address from it at compile time.

## Hardware

Nucleo-F411RE, Arduino header (`i2c1`, `okay` since Lab 1.3.1):

| ADXL345 | Nucleo |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | D14 (PB9) |
| SCL | D15 (PB8) |
| SDO | GND (I2C address `0x53`) |

## Theory

### I2C in one paragraph

Two shared lines (SDA/SCL), many devices, each with a 7-bit address. The
master (STM32) starts every transaction by sending the target's address plus
a read/write bit; only the device whose address matches answers with an ACK.
Access is **register-addressed**: write the register address, then read or
write its value (the API does both phases in one transaction).

### Two different "IDs" — do not confuse them

| Value | What it is | Where it lives |
|---|---|---|
| `0x53` | I2C **address**: where the sensor listens on the bus | Hardwired in the chip, selected by the SDO pin (`0x1D` if SDO is high) |
| `0xE5` | **DEVID**: chip model identifier | Constant stored in sensor register `0x00` |

The address gets us to the sensor; reading `DEVID` confirms that what
answered at `0x53` really is an ADXL345. The STM32, as bus master, has no
address of its own here: it only sends the target's address.

### The overlay: only the external device is added

```dts
&i2c1 {
	adxl345: adxl345@53 {
		compatible = "adi,adxl345";
		reg = <0x53>;
		status = "okay";
	};
};
```

- The board `.dts` already describes the `i2c1` controller and its pins
  (PB8/PB9). It does **not** know what is plugged into the Arduino header.
- The overlay extends `&i2c1` with a child node: "a device at address 0x53
  on this bus", plus a label (`adxl345`) to reference it from C.
- `compatible` links the node to a binding so Zephyr treats it as an I2C
  device and validates `reg`.
- It tells the board nothing at runtime: it is a compile-time description.
  If the SDO wiring changes, only `reg` changes — `main.c` stays untouched.
- The Sensor API is not enabled in `prj.conf`, so the in-tree ADXL345
  driver is not built: the node only provides bus + address to the I2C API.

### Compile-time resolution

```c
static const struct i2c_dt_spec adxl = I2C_DT_SPEC_GET(DT_NODELABEL(adxl345));
```

At build time Zephyr merges the board `.dts` with the overlay
(`build/zephyr/zephyr.dts`), and `I2C_DT_SPEC_GET` reads that merged tree to
fill `{ bus = i2c1, addr = 0x53 }`. Nothing is read from the overlay at
runtime: the values are constants stored in flash.

### `i2c_is_ready_dt()` checks the controller, not the sensor

It only confirms that the STM32's `i2c1` controller was initialized by
Zephyr. Whether the sensor is connected and responding is checked
separately by reading `DEVID`.

### ADXL345 register map (the ones used)

| Register | Address | Purpose |
|---|---|---|
| `DEVID` | `0x00` | Chip ID (`0xE5`), read-only |
| `POWER_CTL` | `0x2D` | Power mode. Boots in **standby**; bit 3 (`Measure`) = 1 starts measuring |
| `DATA_FORMAT` | `0x31` | Bit 3 `FULL_RES` = 1 → 3.9 mg/LSB; range bits at 0 → ±2g |
| `DATAX0` | `0x32` | First of 6 consecutive data registers (`0x32`–`0x37`: X0,X1,Y0,Y1,Z0,Z1) |

`POWER_CTL` belongs to the **sensor**, not the STM32: the ADXL345 starts in
standby by manufacturer design (power saving), so our code must enable
measurement explicitly.

### Initialization order

1. Verify `DEVID` (transaction OK **and** value `== 0xE5`).
2. Write `DATA_FORMAT` first, `POWER_CTL` last — so the very first sample
   already uses the right resolution and range.

### Reading data

- One **burst read** of 6 bytes from `DATAX0`: all three axes belong to the
  same sample.
- Each axis is 2 bytes, **little-endian** (low byte first), 16-bit
  **signed**: `(int16_t)((hi << 8) | lo)`.
- `RAW_TO_MG(raw) = raw * 39 / 10` converts counts to milli-g with integer
  math (no floats, so `LOG_INF` needs no FP printf support). Multiply
  first, divide after, to keep precision.

## Program flow

```
                 ┌──────────────────────┐
                 │        main()        │
                 └──────────┬───────────┘
                            ▼
                ┌───────────────────────┐   no
                │ i2c_is_ready_dt(&adxl)├────────► LOG_ERR, exit
                │ (STM32 i2c1 ready?)   │
                └───────────┬───────────┘
                            │ yes
                            ▼
                ┌───────────────────────┐   ret<0
                │ read DEVID (reg 0x00) ├────────► LOG_ERR, exit
                │ sensor answered?      │   (wiring / address)
                └───────────┬───────────┘
                            │ ok
                            ▼
                ┌───────────────────────┐   != 0xE5
                │ DEVID == 0xE5 ?       ├────────► LOG_ERR, exit
                │ right chip?           │   (not an ADXL345)
                └───────────┬───────────┘
                            │ yes
                            ▼
                ┌───────────────────────┐   ret<0
                │ write DATA_FORMAT     ├────────► LOG_ERR, exit
                │   (full res, +-2g)    │
                │ write POWER_CTL       │
                │   (Measure = 1)       │
                └───────────┬───────────┘
                            │ ok
                            ▼
        ┌──────────────────────────────────────────┐
        │ while (1)                                │
        │                                          │
        │  burst read 6 bytes from DATAX0 (0x32)   │
        │      │                                   │
        │      ├─ error ──► LOG_ERR                │
        │      │                                   │
        │      └─ ok ──► X = buf[1]<<8 | buf[0]    │
        │                Y = buf[3]<<8 | buf[2]    │
        │                Z = buf[5]<<8 | buf[4]    │
        │                RAW_TO_MG() ──► LOG_INF   │
        │                                          │
        │  k_msleep(500)  ─────────────────────┐   │
        │      ▲                               │   │
        │      └───────────────────────────────┘   │
        └──────────────────────────────────────────┘
```

## Verification (real hardware, serial log)

```
*** Booting Zephyr OS build v4.4.0-14810-g1dbf149f7dd5 ***
<inf> i2c_adxl345_lab: DEVID = 0xE5 (expected 0xE5)
<inf> i2c_adxl345_lab: X=101 mg  Y=-538 mg  Z=-682 mg
<inf> i2c_adxl345_lab: X=89 mg  Y=-518 mg  Z=-694 mg
<inf> i2c_adxl345_lab: X=101 mg  Y=-530 mg  Z=-709 mg
...
```

- `DEVID = 0xE5` confirms the sensor answered at `0x53` and is an ADXL345.
- Readings are stable (±4 mg noise) and the board was tilted, so X, Y and Z
  are all non-zero.
- Total magnitude ≈ √(117² + 530² + 670²) ≈ 860 mg: a bit under 1 g, which
  is plausible for an uncalibrated sensor (factory sensitivity and offset
  tolerances are noticeable).

## Troubleshooting

`DEVID read failed (-5)` means nobody acknowledged the address (NACK).
Check wiring, the pull-ups on SDA/SCL, and the SDO level (floating SDO or
tied high gives address `0x1D` instead of `0x53`).

## What we learned

- An overlay can describe external hardware that the board file cannot
  know about: the board describes what ST soldered, the overlay describes
  what we plug in.
- `0x53` (bus address, hardwired, selects the device) and `0xE5` (chip ID,
  stored in a register, confirms the model) are different things.
- `I2C_DT_SPEC_GET` turns the merged Devicetree into a constant
  `{bus, address}` struct; the generic I2C API then works on any I2C device.
- Checking a peripheral has two levels: the controller is ready
  (`i2c_is_ready_dt`) and the device really answers with the right ID.
- Many sensors boot in a low-power state: configure first, enable
  measurement last.
- Multi-byte sensor data: burst-read for a coherent sample, assemble
  little-endian bytes, and keep the sign with `int16_t`.
