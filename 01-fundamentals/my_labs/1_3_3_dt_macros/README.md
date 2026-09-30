# Lab 1.3.3 — Acceder a propiedades del DT desde C con macros

## Objetivo

Leer un valor de configuración custom (`blink-period-ms`) desde el DT hacia
una constante C usando `DT_PROP`, y verificar en compile-time el estado de
periféricos usando `DT_NODE_HAS_STATUS` — confirmando en código lo que el
Lab 1.3.1 confirmó por inspección manual del DT fusionado.

## Teoría

### Las macros de acceso, según qué se necesite resolver

- **`DT_ALIAS(name)`** — resuelve un alias (`led0`, `led1`) a su nodo.
- **`DT_NODELABEL(name)`** — resuelve por label (`usart2:`, `i2c2:`), sin
  pasar por `aliases`.
- **`DT_PATH(...)`** — resuelve por ruta absoluta del árbol.
- **`DT_PROP(node, prop)`** — lee el valor de una propiedad como constante C.
  Guiones en el DT (`blink-period-ms`) se normalizan a guión bajo en la macro
  (`blink_period_ms`).
- **`DT_NODE_HAS_STATUS(node, okay)`** — booleano de compilación, habilita
  `#if` condicional sin dejar código muerto en el binario.

Todas estas macros funcionan igual sin importar si la propiedad consultada
viene del board file oficial o de un overlay propio — no distinguen origen,
solo leen del árbol ya fusionado.

### `zephyr,user` — el nodo reservado para config de aplicación

Nodo especial que Zephyr reconoce sin necesitar un binding YAML propio,
pensado exactamente para guardar valores de configuración de aplicación
(no ligados a hardware real) dentro del DT, en vez de hardcodearlos en el
código C.

### Qué pasa si la propiedad no existe en ningún lado

`DT_PROP` no devuelve `0`/`NULL` silenciosamente si la propiedad no está
declarada en ningún archivo del árbol — es **error de compilación**, porque
`devicetree_generated.h` simplemente no genera esa macro si la propiedad no
existe. Misma filosofía de "fallar rápido en build time" vista en toda la
sección.

## Verificación (hardware real, log por serial)

```
Blink period read from DT: 250 ms
i2c2 is DISABLED in this build (matches Lab 1.3.1 findings)
usart2 is ENABLED in this build (it's our console)
```

Y físicamente: LED en PA1 parpadeando a 250ms (visiblemente más rápido que
los 500ms fijos de 1.3.2), confirmando que el valor vino realmente del DT y
no de una constante fija en el código.

## Lo que aprendimos

- Las macros de `devicetree.h` son la interfaz **uniforme** para leer
  cualquier dato del DT desde C — estructura, alias, propiedades custom,
  estado de periféricos — todo con el mismo costo cero en runtime (resuelto
  en preprocesador).
- `DT_NODE_HAS_STATUS` permite que el propio binario se adapte, en build
  time, a qué hardware está realmente disponible en la placa objetivo —
  sin `#ifdef BOARD_X` manual disperso por el código.
- El DT puede llevar más que descripción de hardware: `zephyr,user` es la
  vía oficial para externalizar constantes de aplicación sin tocar el
  código C ni inventar un binding propio.
