# Zephyr Environment Setup — Troubleshooting Log

Every error hit setting up Zephyr from a clean machine, in the order they appeared, with the exact fix for each. None of these are exotic — they're the standard friction points of a first-time Zephyr install, documented here so the next person (or future me) doesn't have to rediscover them one `ModuleNotFoundError` at a time.

---

## 1. Missing `west sdk install` dependency: `patoolib`

```
ModuleNotFoundError: No module named 'patoolib'
```

**Fix**: the PyPI package name differs from the module it exposes.
```bash
pip install patool
```

---

## 2. `west sdk install` fails: CMake too old

```
CMake Error: CMake 3.28.0 or higher is required. You are running version 3.25.1
```

**Fix**: upgrade CMake inside the venv (isolated from the system CMake used by other projects).
```bash
pip install --upgrade cmake
```

---

## 3. `west build` fails: Python version incompatibility

```
TypeError: PurePath.relative_to() got an unexpected keyword argument 'walk_up'
```

**Cause**: `walk_up` was added to `Path.relative_to()` in Python 3.12. West 1.5.0 assumes it's available. The venv had been created with Python 3.11.

**Fix**: recreate the venv with Python 3.12.
```bash
deactivate
rm -rf .venv
python3.12 -m venv .venv
source .venv/bin/activate
pip install west patool requests semver tqdm jsonschema
pip install --upgrade cmake   # lost when the venv was recreated
```

---

## 4. `west build` fails again: missing `elftools`

```
ModuleNotFoundError: No module named 'elftools'
```

**Fix**:
```bash
pip install pyelftools
```

At this point the STM32 build succeeded end-to-end (156/156 build steps, `zephyr.elf` generated).

---

## 5. `west flash` fails: STM32CubeProgrammer not installed

```
FATAL ERROR: required program .../STM32_Programmer_CLI not found
```

**Cause**: Zephyr's default flashing tool for ST boards is STM32CubeProgrammer, a separate ST-provided tool, not the OpenOCD already in use for earlier bare-metal projects.

**Fix**: use OpenOCD explicitly instead of installing a new tool.
```bash
west flash --runner openocd
```

(A non-fatal `Unable to match requested speed 2000 kHz, using 1800 kHz` warning appears — harmless, the ST-Link simply falls back to the closest supported SWD clock speed.)

---

## 6. ESP32 build fails: missing `esptool`

```
CMake Error: esptool>=5.0.2 not found in PATH.
Please install it using: west packages pip --install
```

**Fix**: the error message names the exact fix. This installs all SoC-specific Python dependencies Zephyr needs for the target being built (Espressif tooling wasn't needed for the STM32 build, so it hadn't surfaced until building for ESP32).
```bash
west packages pip --install
```

---

## Summary

```
patool            → west sdk install dependency
cmake (upgraded)  → build system version requirement
Python 3.12       → west 1.5.0 requirement (walk_up in pathlib)
pyelftools        → kernel object list generation
openocd runner    → STM32 flashing (avoids installing ST's own tool)
west packages pip --install → ESP32-specific tooling (esptool)
```

None of these block a working setup — they're all one-line fixes once identified. The friction is entirely in *discovering* which dependency is missing next, since `west sdk install` and `west build` don't validate their own Python requirements upfront.
