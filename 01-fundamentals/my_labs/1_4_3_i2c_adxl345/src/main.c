/*
 * Lab 1.4.3 - I2C with ADXL345 using raw register access
 *
 * I2C basics:
 *  - Two shared lines (SDA/SCL), many devices, each with a 7-bit address.
 *  - Register-addressed access: write the register address, then read/write
 *    its value (the API does both phases in a single transaction).
 *
 * This lab talks to the sensor registers directly through Zephyr's generic
 * I2C API (no Sensor API, no in-tree driver).
 *
 * 0x53 = I2C address (where the sensor listens, SDO -> GND).
 * 0xE5 = chip ID stored in register 0x00 (identifies the model).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_adxl345_lab, LOG_LEVEL_INF);

/* ADXL345 registers */
#define ADXL345_REG_DEVID       0x00 /* chip ID (read-only) */
#define ADXL345_REG_POWER_CTL   0x2D /* power mode (boots in standby) */
#define ADXL345_REG_DATA_FORMAT 0x31 /* range and resolution */
#define ADXL345_REG_DATAX0      0x32 /* first of 6 data regs (X0..Z1) */

#define ADXL345_DEVID_VALUE     0xE5 /* expected chip ID */
#define ADXL345_MEASURE_BIT     0x08 /* POWER_CTL: start measuring */
#define ADXL345_FULL_RES        0x08 /* DATA_FORMAT: full resolution, +-2g */

/* Full resolution: 3.9 mg/LSB -> mg = raw * 39 / 10 */
/* Raw counts -> milli-g (integer math, no floats) */
#define RAW_TO_MG(raw)          (((int32_t)(raw) * 39) / 10)

/* Bus + address resolved at compile time from the overlay node */
/* Built from merged board .dts + overlay: { bus = i2c1, addr = 0x53 } */
static const struct i2c_dt_spec adxl = I2C_DT_SPEC_GET(DT_NODELABEL(adxl345));

int main(void)
{
	uint8_t devid;  /* chip ID read from the sensor */
	uint8_t buf[6]; /* raw X,Y,Z bytes */
	int ret;        /* 0 = OK, negative = error */

	/* Checks the STM32 I2C controller only, not the sensor */
	if (!i2c_is_ready_dt(&adxl)) {
		LOG_ERR("I2C bus not ready");
		return 0;
	}

	/* Sanity check: WHO_AM_I-style register must read 0xE5 */
	/* Step 1: did the sensor answer? */
	ret = i2c_reg_read_byte_dt(&adxl, ADXL345_REG_DEVID, &devid);
	if (ret < 0) {
		LOG_ERR("DEVID read failed (%d) - check wiring/address", ret);
		return 0;
	}
	LOG_INF("DEVID = 0x%02X (expected 0x%02X)", devid, ADXL345_DEVID_VALUE);
	/* Step 2: is it the expected chip? */
	if (devid != ADXL345_DEVID_VALUE) {
		LOG_ERR("Unexpected device ID");
		return 0;
	}

	/* Configure: full resolution +-2g, then enter measurement mode */
	/* Format first, measure last: first sample already uses the right setup */
	ret = i2c_reg_write_byte_dt(&adxl, ADXL345_REG_DATA_FORMAT, ADXL345_FULL_RES);
	ret |= i2c_reg_write_byte_dt(&adxl, ADXL345_REG_POWER_CTL, ADXL345_MEASURE_BIT);
	if (ret < 0) {
		LOG_ERR("Configuration failed");
		return 0;
	}

	while (1) {
		/* Burst read X0,X1,Y0,Y1,Z0,Z1 (little-endian, 16-bit signed) */
		/* One transaction: all 3 axes come from the same sample */
		ret = i2c_burst_read_dt(&adxl, ADXL345_REG_DATAX0, buf, sizeof(buf));
		if (ret < 0) {
			LOG_ERR("Burst read failed (%d)", ret);
		} else {
			/* Low byte first; int16_t keeps the sign */
			int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
			int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
			int16_t z = (int16_t)((buf[5] << 8) | buf[4]);

			LOG_INF("X=%d mg  Y=%d mg  Z=%d mg",
				RAW_TO_MG(x), RAW_TO_MG(y), RAW_TO_MG(z));
		}
		k_msleep(500);
	}

	return 0;
}

/*
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
*/