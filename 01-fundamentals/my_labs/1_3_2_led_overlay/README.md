# Lab 1.3.2 — Overlay para agregar un LED externo

## Objetivo

Declarar un LED conectado a un pin libre (PA1) mediante un overlay propio,
sin modificar el board file oficial `nucleo_f411re.dts`, y confirmar que la
misma API genérica de GPIO funciona igual sobre hardware "no oficial".

## Teoría

### El mecanismo de extensión (`&label`)

El overlay usa `&leds { ... };` — el mismo patrón `&label` que ya vimos en
`&usart1`/`&i2c1` del board file. No crea un nodo nuevo: **extiende** el nodo
`leds` que el board ya define, agregando un hijo nuevo (`led_ext`) junto al
`led_2` (verde, onboard) que ya existía. Lo mismo ocurre con `aliases`:
`/ { aliases { led1 = &external_led; }; };` agrega `led1` junto al `led0` ya
existente, sin reemplazar el nodo completo.

### Combinación, no sustitución

El resultado final (`build/zephyr/zephyr.dts`) contiene **todo** lo del board
file original, más lo que el overlay agregó. Es un mecanismo acumulativo
(análogo a cómo un segundo stylesheet CSS agrega/sobreescribe reglas sin
descartar el primero), no una elección entre archivo A o archivo B.

### El overlay es opcional y condicional

CMake busca automáticamente `boards/<nombre_exacto_del_board>.overlay` por
convención de ruta. Si no existe, el build sigue usando solo el DT del board,
sin error ni diferencia de comportamiento — el mecanismo de fusión siempre
existe, pero solo se activa si el archivo está presente.

### Por qué el pin PA1 es "nada" sin overlay

A diferencia de `led0` (ya declarado de fábrica en `nucleo_f411re.dts`), PA1
no tiene ningún nodo, alias, ni referencia en el DT hasta que el overlay lo
declara. Sin overlay, `DT_ALIAS(led1)` sería un **error de compilación**: el
build no genera código con valores "vacíos" o inventados, falla explícito.

## Hardware

LED + resistencia (~330Ω) en serie entre el pin **A1** del conector Arduino
del Nucleo y **GND**.

## Verificación

Confirmado en hardware real: LED en la protoboard encendiéndose/apagándose
cada 500ms, controlado exclusivamente por `DT_ALIAS(led1)` resuelto contra el
overlay — el código C (`gpio_pin_toggle_dt`, `gpio_pin_configure_dt`) es
idéntico al usado con `led0`, único cambio es el origen del nodo DT.

## Lo que aprendimos

- Un overlay no reemplaza el board file — lo **fusiona/extiende**, propiedad
  por propiedad, nodo por nodo.
- Sin overlay, el mismo resultado físico (togglear PA1) es posible con la API
  de bajo nivel (`DEVICE_DT_GET(DT_NODELABEL(gpioa))` + número de pin
  hardcodeado), pero se pierde: validación en build time de que el pin existe,
  portabilidad entre boards, y nombre semántico — exactamente lo que el DT
  existe para resolver.
- El patrón real de portabilidad es: **1 `main.c` universal × N overlays**
  (uno por board soportado) — el código de aplicación nunca se multiplica, solo
  la descripción de hardware.
