# Lab 1.3.1 — Lectura del Device Tree fusionado del Nucleo-F411RE

## Objetivo

Mapear qué periféricos del STM32F411 están realmente habilitados (`status = "okay"`)
en la placa Nucleo-F411RE, usando el DT **fusionado** que genera Zephyr en build
time, no el `.dts` fuente disperso.

## Teoría

### Por qué leer `build/zephyr/zephyr.dts` y no el `.dts` fuente

El `.dts` del board (`nucleo_f411re.dts`) solo muestra lo que **ese archivo
sobreescribe** — no muestra los valores heredados de `stm32f4.dtsi`/`stm32f411.dtsi`
que no se tocan. `build/zephyr/zephyr.dts` es el resultado de fusionar board file +
todos los `.dtsi` heredados + overlays (si los hay) en un único árbol resuelto —
la única fuente confiable para saber "qué hay realmente disponible" sin rastrear
includes a mano.

### El SoC trae todo deshabilitado por defecto

El STM32F411 soporta físicamente múltiples USART, I2C, SPI, etc., pero muchos
comparten los mismos pines físicos entre sí (multiplexación de pines). El SoC
(`stm32f4.dtsi`) no puede saber de antemano qué función ganará cada pin en una
placa específica — por eso, por diseño, **todo arranca `status = "disabled"`**:
es una plantilla genérica del chip, no de una placa concreta.

### El board file activa solo lo físicamente cableado

`nucleo_f411re.dts` sobreescribe `status = "okay"` únicamente en los periféricos
que el fabricante (ST) efectivamente cableó hacia algún conector o función
integrada de esa placa específica.

## Hallazgos (evidencia de hardware real, vía `grep` sobre el DT fusionado)

| Periférico | Status | Por qué |
|---|---|---|
| `usart1` | **okay** | Ruteado al conector shield Arduino (junto con alias `arduino_i2c`/`arduino_spi` para I2C1/SPI1) |
| `usart2` | **okay** | Conectado internamente al ST-Link integrado — es la consola serial usada en todo el roadmap |
| `i2c1` | **okay** | Ruteado al conector shield Arduino |
| `i2c2` | disabled | Default del SoC — sin pines asignados en este board |
| `i2c3` | disabled | Default del SoC — sin pines asignados en este board |
| `spi1` | **okay** | Ruteado al conector shield Arduino |
| `rtc` | **okay** | No depende de pines externos — corre contra el oscilador LSI interno |
| `i2s1` | disabled | Default del SoC — sin pines asignados en este board |

## Lo que aprendimos

- El DT no inventa disponibilidad de hardware — **la documenta**, reflejando
  exactamente el diseño físico real de la placa (qué se soldó, qué se ruteó).
- Los periféricos con doble label (`i2c1: arduino_i2c:`, `spi1: arduino_spi:`)
  revelan que el mismo periférico físico sirve a dos propósitos: uso interno de
  Zephyr y acceso vía el conector estándar Arduino.
- Para usar cualquier periférico `disabled` (`i2c2`, `i2c3`, `i2s1`), es
  obligatorio un overlay propio que lo active y le asigne pines — el board file
  oficial nunca se modifica para esto (objeto del Lab 1.3.2).
