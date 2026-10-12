/*
 * Lab 1.4.4 - SPI with BME280 using raw register access
 *
 * SPI basics:
 *  - Four lines: SCLK, MOSI, MISO and one CS (chip select) per device.
 *  - No addresses: the master pulls CS low to select the device.
 *  - Full duplex: while the master sends, the slave answers on MISO.
 *
 * Talks to the sensor registers directly through Zephyr's generic SPI API
 * (no Sensor API, no in-tree driver). Reads the chip ID and the temperature.
 * Extended: also computes pressure and humidity.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_bme280_lab, LOG_LEVEL_INF);

/* BME280 registers */
#define BME280_REG_CALIB_T      0x88 /* dig_T1..T3 calibration (6 bytes) */
#define BME280_REG_ID           0xD0 /* chip ID (read-only) */
#define BME280_REG_RESET        0xE0 /* write 0xB6 = soft reset */
#define BME280_REG_CTRL_HUM     0xF2 /* humidity oversampling (write before CTRL_MEAS) */
#define BME280_REG_CTRL_MEAS    0xF4 /* temp/pressure oversampling + power mode */
#define BME280_REG_DATA         0xF7 /* press(3) temp(3) hum(2) = 8 bytes
Es la dirección base donde residen las lecturas en bruto (ADC raw values) ya digitalizadas por el sensor.
*/
#define BME280_REG_CALIB_H1     0xA1 /* dig_H1 (1 byte) */
#define BME280_REG_CALIB_H2     0xE1 /* dig_H2..H6 (7 bytes) */

#define BME280_ID_BME280        0x60 /* expected chip ID */
#define BME280_ID_BMP280        0x58 /* look-alike without humidity, often sold as BME280 */
#define BME280_RESET_CMD        0xB6
#define BME280_CTRL_HUM_X1      0x01 /* humidity oversampling x1 */
#define BME280_CTRL_MEAS_NORMAL 0x27 /* temp x1, pressure x1, normal mode */

/* SPI command byte: bit7 = 1 read / 0 write, bits6..0 = register */


#define BME280_SPI_READ         0x80 //se lee data en bruto
/*
se usa para CUALQUIER lectura, no solo para los datos en bruto.
Sirve para indicarle al sensor que vas a pedir/leer información de cualquier registro, ya sean:
Datos en bruto (ADC): Muestras de temperatura, presión o humedad (0xF7).
Chip ID: Para verificar que el integrado responde (0xD0).
Coeficientes de calibración: dig_T, dig_P, dig_H (0x88, 0x8E, 0xA1, 0xE1).
*/

#define BME280_SPI_WRITE_MASK   0x7F //se envían comandos de configuración
/*
0xE0 (BME280_REG_RESET): para enviarle el comando de reinicio 0xB6.
0xF2 (BME280_REG_CTRL_HUM): para configurar el oversampling de humedad.
0xF4 (BME280_REG_CTRL_MEAS): para configurar oversampling y modo de energía.
*/

#define BME280_MAX_READ         8    /* largest read used: data block (8 bytes) 
tamaño máximo del búfer SPI de transmisión/recepción en bytes para una sola llamada a la función bme_read()
Presión (3 bytes) + Temperatura (3 bytes) + Humedad (2 bytes) = 8 bytes

Cable TX (Tú -> Sensor): [ COMANDO ] [ DUMMY ] [ DUMMY ] ... [ DUMMY ]
Cable RX (Sensor -> Tú): [ BASURA  ] [ DATO1 ] [ DATO2 ] ... [ DATO8 ]

En el cable que sale (TX): Tu intención es mandar 1 byte útil (el comando) y luego 8 bytes inútiles (relleno/dummy para dar reloj).
En el cable que entra (RX): Pasa exactamente lo contrario. Recibes 1 byte inútil (basura/descartable) y luego 8 bytes útiles (los datos reales que querías).
*/

/* SPI mode 0 (CPOL=0, CPHA=0), 8-bit words, MSB first */
#define BME280_SPI_OP           (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)/*
para que dos chips por SPI se entiendan, tienen que ponerse de acuerdo en 3 reglas básicas.


### 1. ¿Cómo enviamos los datos? `SPI_WORD_SET(8)`
Los datos se mandan en paquetes de **1 byte (8 bits)** a la vez.

### 2. ¿Qué bit sale primero?  `SPI_TRANSFER_MSB`
Se manda primero el bit de la izquierda (el más pesado / MSB) y al final el de la derecha.

* *Ejemplo:* Para enviar `1000 0000`, sale primero el `1` y luego los ceros.

### 3. ¿Cómo coordinamos el ritmo?  Modo SPI 0 (`CPOL=0`, `CPHA=0`)
Es el "ritmo del reloj" para transferir la información en ambas direcciones:

* **Reloj en reposo (`CPOL=0`):** Cuando no hay comunicación, el cable de reloj está apagado ($0\text{V}$).
* **Momento de envío (transmisión):** El que transmite (máster o sensor) cambia y coloca el bit en la línea cuando el reloj **BAJA** (de $3.3\text{V}$ a $0\text{V}$).
* **Momento de lectura (captura) (`CPHA=0`):** El que recibe captura el bit en el momento exacto en que el reloj **SUBE** (de $0\text{V}$ a $3.3\text{V}$).

---
*/

/* Temperature calibration coefficients stored in the sensor */
struct bme280_calib {
	uint16_t t1;
	int16_t t2;
	int16_t t3;
	/* Pressure coefficients */
	uint16_t p1;
	int16_t p2, p3, p4, p5, p6, p7, p8, p9;
	/* Humidity coefficients */
	uint8_t h1;
	int16_t h2;
	uint8_t h3;
	int16_t h4, h5;
	int8_t h6;
};


//La macro extrae del Overlay/DTS el bus, pin CS y frecuencia, y los une con la configuración del modo SPI en la estructura bme en tiempo de compilación*/
static const struct spi_dt_spec bme = SPI_DT_SPEC_GET(DT_NODELABEL(bme280), BME280_SPI_OP);

/* Write one register: [reg with bit7 cleared, value] */
static int bme_write_reg(uint8_t reg, uint8_t val)
{
	//Arma la trama de 2 bytes. El primer byte apaga el bit 7 (reg & 0x7F) para indicar escritura , y el segundo byte (val) es el valor a guardar.
	uint8_t tx[2] = { reg & BME280_SPI_WRITE_MASK, val };

	//Adaptan el arreglo tx al formato que exige la API de Zephyr RTOS.
	const struct spi_buf buf = { .buf = tx, .len = sizeof(tx) };
	const struct spi_buf_set set = { .buffers = &buf, .count = 1 };

	//Adaptan el arreglo tx al formato que exige la API de Zephyr RTOS.
	return spi_write_dt(&bme, &set);
}
//En Escritura (bme_write_reg): Mandas 2 bytes reales (Dirección + Dato a guardar). El sensor guarda esos datos en su memoria.


/* Read len bytes starting at reg: send command + dummy bytes, keep rx[1..] */
static int bme_read(uint8_t reg, uint8_t *data, size_t len)
{
	uint8_t tx[1 + BME280_MAX_READ] = { 0 };
	uint8_t rx[1 + BME280_MAX_READ];
	int ret;

	if (len == 0 || len > BME280_MAX_READ) {
		return -EINVAL;
	}

	/* Command byte; the address auto-increments on multi-byte reads */
	tx[0] = reg | BME280_SPI_READ;

	const struct spi_buf tx_buf = { .buf = tx, .len = len + 1 };
	const struct spi_buf rx_buf = { .buf = rx, .len = len + 1 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	ret = spi_transceive_dt(&bme, &tx_set, &rx_set);
	if (ret < 0) {
		return ret;
	}

	/* rx[0] was clocked in during the command byte: discard it */
	memcpy(data, &rx[1], len);
	return 0;
}
//En Lectura (bme_read): Mandas 1 comando + 8 ceros de rellenado (dummy) únicamente para mover el reloj y poder recibir los datos del sensor.
/*
Por el cable TX (salida): Envías [1 Comando + 8 Dummies]

Por el cable RX (entrada): Recibes [1 Basura/Dummy + 8 Datos reales]
*/


/* Two bytes, low byte first, to a 16-bit value */
static uint16_t le16(const uint8_t *b)
{
	return (uint16_t)((b[1] << 8) | b[0]);
}

/* Bosch integer compensation formula: returns temperature in 0.01 degC */
static int32_t bme280_compensate_temp(int32_t adc_t, const struct bme280_calib *c,
				      int32_t *t_fine)
{
	int32_t var1 = (((adc_t >> 3) - ((int32_t)c->t1 << 1)) * (int32_t)c->t2) >> 11;
	int32_t d = (adc_t >> 4) - (int32_t)c->t1;
	int32_t var2 = (((d * d) >> 12) * (int32_t)c->t3) >> 14;

	/* t_fine is shared with the pressure and humidity formulas */
	*t_fine = var1 + var2;

	return (*t_fine * 5 + 128) >> 8;
}

/* Bosch integer formula: returns pressure in Pa (needs t_fine) */
static uint32_t bme280_compensate_press(int32_t adc_p, const struct bme280_calib *c,
					int32_t t_fine)
{
	int32_t var1 = (t_fine >> 1) - 64000;
	int32_t var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)c->p6;
	uint32_t p;

	var2 = var2 + ((var1 * (int32_t)c->p5) << 1);
	var2 = (var2 >> 2) + ((int32_t)c->p4 << 16);
	var1 = ((((int32_t)c->p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
		(((int32_t)c->p2 * var1) >> 1)) >> 18;
	var1 = ((32768 + var1) * (int32_t)c->p1) >> 15;
	if (var1 == 0) {
		return 0; /* avoid division by zero */
	}

	p = (((uint32_t)(1048576 - adc_p) - (uint32_t)(var2 >> 12))) * 3125;
	if (p < 0x80000000U) {
		p = (p << 1) / (uint32_t)var1;
	} else {
		p = (p / (uint32_t)var1) * 2;
	}

	var1 = ((int32_t)c->p9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
	var2 = ((int32_t)(p >> 2) * (int32_t)c->p8) >> 13;

	return (uint32_t)((int32_t)p + ((var1 + var2 + (int32_t)c->p7) >> 4));
}

/* Bosch integer formula: returns humidity in %RH * 1024 (needs t_fine) */
static uint32_t bme280_compensate_hum(int32_t adc_h, const struct bme280_calib *c,
				      int32_t t_fine)
{
	int32_t v = t_fine - 76800;

	v = (((((adc_h << 14) - ((int32_t)c->h4 << 20) - ((int32_t)c->h5 * v)) + 16384) >> 15) *
	     (((((((v * (int32_t)c->h6) >> 10) *
		  (((v * (int32_t)c->h3) >> 11) + 32768)) >> 10) + 2097152) *
	       (int32_t)c->h2 + 8192) >> 14));
	v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)c->h1) >> 4);

	/* Clamp to 0..100 %RH */
	v = v < 0 ? 0 : v;
	v = v > 419430400 ? 419430400 : v;

	return (uint32_t)(v >> 12);
}

int main(void)
{
	uint8_t id;      /* chip ID read from the sensor */
	uint8_t cal[24]; /* raw calibration bytes */
	uint8_t h1;      /* raw dig_H1 */
	uint8_t hcal[7]; /* raw dig_H2..H6 */
	uint8_t buf[8];  /* raw press/temp/hum bytes */
	struct bme280_calib calib;
	int ret;         /* 0 = OK, negative = error */

	/* Checks the STM32 SPI controller only, not the sensor */
	if (!spi_is_ready_dt(&bme)) {
		LOG_ERR("SPI bus not ready");
		return 0;
	}

	/* Step 1: did the transaction work? */
	ret = bme_read(BME280_REG_ID, &id, 1);
	if (ret < 0) {
		LOG_ERR("ID read failed (%d)", ret);
		return 0;
	}
	LOG_INF("ID = 0x%02X (BME280 = 0x%02X, BMP280 = 0x%02X)",
		id, BME280_ID_BME280, BME280_ID_BMP280);

	/* Step 2: is it a supported chip? (0x00 = MISO stuck low, 0xFF = MISO floating/high) */
	if (id != BME280_ID_BME280 && id != BME280_ID_BMP280) {
		LOG_ERR("Unexpected chip ID - check wiring/CS");
		return 0;
	}
	if (id == BME280_ID_BMP280) {
		LOG_WRN("This is a BMP280 (no humidity), temperature still works");
	}

	/* Soft reset, then wait for the sensor to restart */
	ret = bme_write_reg(BME280_REG_RESET, BME280_RESET_CMD);
	k_msleep(10);

	/* Read the temperature calibration coefficients (little-endian) */
	/* 24 bytes (T1..T3 + P1..P9) in chunks of 8, the max read size */
	for (int i = 0; i < (int)sizeof(cal); i += BME280_MAX_READ) {
		ret |= bme_read(BME280_REG_CALIB_T + i, &cal[i], BME280_MAX_READ);
	}

	/* Humidity coefficients exist only on the real BME280 */
	if (id == BME280_ID_BME280) {
		ret |= bme_read(BME280_REG_CALIB_H1, &h1, 1);
		ret |= bme_read(BME280_REG_CALIB_H2, hcal, sizeof(hcal));
	}
	if (ret < 0) {
		LOG_ERR("Reset/calibration read failed");
		return 0;
	}
	calib.t1 = le16(&cal[0]);
	calib.t2 = (int16_t)le16(&cal[2]);
	calib.t3 = (int16_t)le16(&cal[4]);
	calib.p1 = le16(&cal[6]);
	calib.p2 = (int16_t)le16(&cal[8]);
	calib.p3 = (int16_t)le16(&cal[10]);
	calib.p4 = (int16_t)le16(&cal[12]);
	calib.p5 = (int16_t)le16(&cal[14]);
	calib.p6 = (int16_t)le16(&cal[16]);
	calib.p7 = (int16_t)le16(&cal[18]);
	calib.p8 = (int16_t)le16(&cal[20]);
	calib.p9 = (int16_t)le16(&cal[22]);
	if (id == BME280_ID_BME280) {
		calib.h1 = h1;
		calib.h2 = (int16_t)le16(&hcal[0]);
		calib.h3 = hcal[2];
		/* H4 and H5 are 12-bit values sharing byte 0xE5 */
		calib.h4 = (int16_t)(((int8_t)hcal[3] << 4) | (hcal[4] & 0x0F));
		calib.h5 = (int16_t)(((int8_t)hcal[5] << 4) | (hcal[4] >> 4));
		calib.h6 = (int8_t)hcal[6];
	}

	/* CTRL_HUM first (only applied after CTRL_MEAS), CTRL_MEAS last starts measuring */
	ret = bme_write_reg(BME280_REG_CTRL_HUM, BME280_CTRL_HUM_X1);
	ret |= bme_write_reg(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_NORMAL);
	if (ret < 0) {
		LOG_ERR("Configuration failed");
		return 0;
	}

	while (1) {
		/* Burst read press(3) temp(3) hum(2) in one transaction */
		ret = bme_read(BME280_REG_DATA, buf, sizeof(buf));
		if (ret < 0) {
			LOG_ERR("Burst read failed (%d)", ret);
		} else {
			int32_t t_fine;

			/* Temperature is 20 bits: msb<<12 | lsb<<4 | xlsb>>4 */
			int32_t adc_t = ((int32_t)buf[3] << 12) |
					((int32_t)buf[4] << 4) |
					(buf[5] >> 4);
			int32_t t = bme280_compensate_temp(adc_t, &calib, &t_fine);

			/* Pressure has the same 20-bit layout, in buf[0..2] */
			int32_t adc_p = ((int32_t)buf[0] << 12) |
					((int32_t)buf[1] << 4) |
					(buf[2] >> 4);
			uint32_t p = bme280_compensate_press(adc_p, &calib, t_fine);

			LOG_INF("Temp = %s%d.%02d C", t < 0 ? "-" : "",
				abs(t) / 100, abs(t) % 100);
			/* Pa / 100 = hPa, two decimals */
			LOG_INF("Press = %u.%02u hPa", p / 100, p % 100);

			if (id == BME280_ID_BME280) {
				/* Humidity is 16 bits: msb<<8 | lsb */
				int32_t adc_h = ((int32_t)buf[6] << 8) | buf[7];
				uint32_t h = bme280_compensate_hum(adc_h, &calib, t_fine);
				uint32_t rh100 = (h * 100) >> 10; /* %RH * 100 */

				LOG_INF("Hum = %u.%02u %%RH", rh100 / 100, rh100 % 100);
			}
		}
		k_msleep(1000);
	}

	return 0;
}



/*
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
*/