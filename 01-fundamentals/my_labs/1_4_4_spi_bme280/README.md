# Lab 1.4.4 — SPI with the BME280 using raw register access

## Objective

Talk to an SPI sensor (BME280 / BMP280) through Zephyr's generic SPI API,
reading and writing its registers directly — no Sensor API, no in-tree
driver. Bus, chip select and clock frequency come from a Devicetree overlay;
the code reads the chip ID, loads the factory calibration data and converts
raw readings to temperature and pressure (and humidity on a real BME280)
with Bosch's integer formulas.

## Hardware

Nucleo-F411RE, Arduino header (`spi1`, enabled in the board file):

| BME280 | Nucleo |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL (SCLK) | D13 (PA5) |
| SDA (MOSI) | D11 (PA7) |
| SDO (MISO) | D12 (PA6) |
| CSB (CS) | D10 (PB6) |

On this module the last pin is printed `sdd`; it is SDO.

## Theory

### SPI in one paragraph

Four lines: SCLK, MOSI, MISO and one CS per device. There are no addresses:
the master selects a device by pulling its CS low. The bus is **full duplex**:
for every byte the master sends, one byte comes back on MISO. Whoever
generates the clock is the master (the STM32).

### SPI vs I2C

| | I2C (Lab 1.4.3) | SPI (this lab) |
|---|---|---|
| Device selection | 7-bit address on the bus | One CS line per device |
| Lines | 2 shared | 3 shared + 1 CS per device |
| Direction | Half duplex | Full duplex |
| Register access | API adds the address phase | We build the command byte ourselves |

### The command byte

The first byte of every transaction tells the BME280 what to do:

```
bit 7      bits 6..0
R/W        register address
1 = read   (the register to access)
0 = write
```

- Read: `reg | 0x80`. Write: `reg & 0x7F`.
- Multi-byte reads auto-increment the register address, so one transaction
  can fetch a whole block.

### Full duplex: the first received byte is garbage

While the command byte goes out, the sensor has nothing meaningful to send,
so `rx[0]` is discarded. The real data starts at `rx[1]`. To read `n` bytes
we clock `n + 1` bytes and keep `rx[1..n]`.

```
tx:  [cmd][ 00 ][ 00 ][ 00 ] ...
rx:  [xx ][ d0 ][ d1 ][ d2 ] ...   <- rx[0] discarded
```

### The overlay

```dts
&spi1 {
	cs-gpios = <&gpiob 6 GPIO_ACTIVE_LOW>; /* D10 = chip select */
	bme280: bme280@0 {
		compatible = "bosch,bme280";
		reg = <0>;
		spi-max-frequency = <1000000>;
		status = "okay";
	};
};
```

- The board file already describes the `spi1` controller and its pins.
  The overlay adds what is plugged in.
- `cs-gpios` lives in the **bus** node: it is the list of CS lines. The
  device's `reg` is the **index** into that list (`0` = first entry), not a
  bus address.
- `spi-max-frequency` is the highest clock the device accepts; Zephyr uses
  it when configuring the transfer.
- The Sensor API is not enabled, so the in-tree driver is not built: the
  node only provides bus + CS + frequency to the SPI API.

### Compile-time resolution

```c
#define BME280_SPI_OP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
static const struct spi_dt_spec bme =
	SPI_DT_SPEC_GET(DT_NODELABEL(bme280), BME280_SPI_OP);
```

`SPI_DT_SPEC_GET` merges the overlay with the board file and fills
`{ bus, config(cs, frequency, operation) }` as constants in flash.
`BME280_SPI_OP` adds what the Devicetree cannot know: 8-bit words, MSB first.
No `CPOL`/`CPHA` flags means **SPI mode 0** (clock idle low, sample on the
rising edge); the BME280 accepts modes 0 and 3.

`spi_is_ready_dt()` checks the STM32 controller only, not the sensor.

### Registers used

| Register | Address | Purpose |
|---|---|---|
| `ID` | `0xD0` | Chip ID: `0x60` = BME280, `0x58` = BMP280 |
| `RESET` | `0xE0` | Write `0xB6` = soft reset |
| `CTRL_HUM` | `0xF2` | Humidity oversampling. Applied only after `CTRL_MEAS` is written |
| `CTRL_MEAS` | `0xF4` | Temperature/pressure oversampling + power mode (`0x27` = x1, x1, normal) |
| `DATA` | `0xF7` | press(3) temp(3) hum(2) = 8 bytes, read in one burst |
| `0x88`–`0x9F` | | Calibration `T1..T3`, `P1..P9` (24 bytes) |
| `0xA1`, `0xE1`–`0xE7` | | Calibration `H1`, `H2..H6` (BME280 only) |

The sensor boots in **sleep**: `CTRL_HUM` first, `CTRL_MEAS` last starts
measuring.

### Calibration and compensation

Each chip stores factory-measured coefficients. The raw ADC value is not a
temperature: Bosch's integer formula combines it with the coefficients.

- Read the coefficients **once** at boot (little-endian; `T1`, `P1`, `H1`,
  `H3` are unsigned, the others signed).
- Temperature is computed first: it produces `t_fine`, which the pressure
  and humidity formulas reuse.
- Results: temperature in 0.01 °C, pressure in Pa (we print hPa), humidity
  in %RH × 1024. All integer math, no floats.
- `bme_read()` is limited to 8 bytes per transaction, so the 24 calibration
  bytes are read as three chunks of 8.

## Program flow

```
                 ┌──────────────────────┐
                 │        main()        │
                 └──────────┬───────────┘
                            ▼
                ┌───────────────────────┐   no
                │ spi_is_ready_dt(&bme) ├────────► LOG_ERR, exit
                │ (STM32 SPI1 ready?)   │
                └───────────┬───────────┘
                            │ yes
                            ▼
                ┌───────────────────────┐   ret<0
                │ read ID (reg 0xD0)    ├────────► LOG_ERR, exit
                │ transaction OK?       │   (SPI/CS problem)
                └───────────┬───────────┘
                            │ ok
                            ▼
                ┌───────────────────────┐   other
                │ ID == 0x60 or 0x58 ?  ├────────► LOG_ERR, exit
                │ supported chip?       │   (0x00/0xFF: wiring/CS)
                └───────────┬───────────┘
                            │ yes (0x58 = BMP280: no humidity)
                            ▼
                ┌───────────────────────┐
                │ soft reset (0xB6)     │
                │ k_msleep(10)          │
                └───────────┬───────────┘
                            ▼
                ┌───────────────────────┐   ret<0
                │ read calibration      ├────────► LOG_ERR, exit
                │  T1..T3, P1..P9       │
                │  (24 B, 3 x 8 B)      │
                │  H1..H6 (BME280 only) │
                │ parse little-endian   │
                └───────────┬───────────┘
                            ▼
                ┌───────────────────────┐   ret<0
                │ write CTRL_HUM  (x1)  ├────────► LOG_ERR, exit
                │ write CTRL_MEAS (0x27)│
                │  (starts measuring)   │
                └───────────┬───────────┘
                            ▼
        ┌──────────────────────────────────────────────┐
        │ while (1)                                    │
        │                                              │
        │  burst read 8 bytes from DATA (0xF7)         │
        │      │                                       │
        │      ├─ error ──► LOG_ERR                    │
        │      │                                       │
        │      └─ ok ──► adc_t = buf[3..5] (20 bit)    │
        │                t = compensate_temp()         │
        │                    └─► also yields t_fine    │
        │                adc_p = buf[0..2] (20 bit)    │
        │                p = compensate_press(t_fine)  │
        │                ──► LOG_INF Temp, Press       │
        │                                              │
        │                if BME280:                    │
        │                  adc_h = buf[6..7] (16 bit)  │
        │                  h = compensate_hum(t_fine)  │
        │                  ──► LOG_INF Hum             │
        │                                              │
        │  k_msleep(1000)  ─────────────────────┐      │
        │      ▲                                │      │
        │      └────────────────────────────────┘      │
        └──────────────────────────────────────────────┘
```

## Verification (real hardware, serial log)

```
<inf> spi_bme280_lab: Temp = 28.47 C
<inf> spi_bme280_lab: Press = 1003.60 hPa
<inf> spi_bme280_lab: Temp = 28.47 C
<inf> spi_bme280_lab: Press = 1003.54 hPa
```

- Temperature is stable and plausible for a room with a warm board nearby.
- Pressure ≈ 1003.5 hPa is a normal sea-level value.
- No `Hum` line is printed: the humidity path runs only when `ID == 0x60`.
  The module used here behaves like a **BMP280** (`ID = 0x58`), a look-alike
  without humidity that is often sold as a BME280. The humidity code is
  implemented but **not verified on hardware**.

## Troubleshooting (what went wrong on the way)

### 1. ADXL345 module: `DEVID = 0x00` on SPI

The first version of this lab used an ADXL345 breakout (GY-291 style). SPI
always read `0x00`: a mode/frequency sweep did not change it, and probing
MISO showed the line held low by the module. Cause: a **0 Ω resistor (R4)
ties SDO to GND** on that board, so 4-wire SPI cannot work unless it is
removed. The module was fine for I2C (Lab 1.4.3) — this is a board strap,
not a firmware bug. We switched to a BME280 module instead of desoldering.

### 2. `ID = 0xFF` with the BME280: SDO and CSB swapped

MISO read `0xFF` and did not follow the internal pull-down. Cause: the SDO
and CSB wires were **interchanged** (SDO on D10, CSB on D12), so the MISO
pin was reading CSB, which the module holds high with a pull-up. How it was
isolated:

- A bit-banged SPI probe (plain GPIO, SPI1 disabled in the overlay) also read
  `0xFF` → the STM32 SPI peripheral, the driver and the overlay were cleared.
- MISO read `1` with pull-down enabled, and regardless of CS → the line was
  held high from outside, not floating.

After swapping the two wires, the ID and the readings were correct.

### 3. Reusing a `spi_config` pointer hides config changes

When sweeping SPI modes in a loop, reusing the same `spi_config` object
(same pointer) makes the STM32 driver skip reconfiguration, so the sweep
silently tests nothing new. Use distinct config objects (a static array).
Also, on this clock tree the minimum SPI1 frequency is 375 kHz, so
100 kHz is rejected.

### 4. `0x00` vs `0xFF` as a clue

- `0x00` → MISO held low (or undriven with the default pull-down).
- `0xFF` → MISO held high, or driven high by something else.

Neither means "STM32 problem". Isolate with a GPIO-only bit-bang read.

## What we learned

- SPI selects devices with CS lines instead of addresses; in Zephyr the CS
  list belongs to the bus node and the device's `reg` is its index.
- Full duplex means every read has a throw-away first byte: clock `n + 1`,
  keep `n`.
- The register address byte carries the read/write flag (bit 7); sensors
  like the BME280 auto-increment on burst reads.
- Raw sensor values need factory calibration: read the coefficients once,
  compute `t_fine` first, and keep everything in integer math.
- Check the chip ID and read it as a clue: the ID value (`0x58`, `0x00`,
  `0xFF`) tells which layer is failing.
- Cheap breakout modules can have hidden straps (R4 on the ADXL345 board)
  or be a different chip than advertised (BMP280 sold as BME280).
- When SPI fails, a software bit-bang over plain GPIO separates hardware
  from driver/Devicetree problems in a single run.
