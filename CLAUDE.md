# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a sensor module firmware for the MultiFlexMeter (MFM4) - a modular IoT sensing device with pluggable sensor modules. This repository implements the Electrical Conductivity (EC) module firmware, which measures water conductivity using Atlas Scientific EZO-EC sensors and temperature using DS18B20 sensors.

The module supports dual probes (PROBE_A and PROBE_B) with independent calibration and measurement capabilities.

## Platform & Hardware

- **RTOS**: RIOT OS (2024.07 release)
- **MCU**: STM32L010x6 (ARM Cortex-M0+)
- **Board**: `ec-module` (custom board in `boards/ec-module/`)
- **Communication**: I2C slave interface to MFM master device
- **Sensors**:
  - Atlas Scientific EZO-EC module (UART at 115200 baud)
  - 2x DS18B20 temperature sensors (1-Wire)

## Build Commands

```bash
# Build the firmware
make

# Flash to device (requires programmer setup)
make flash

# Flash and connect to serial console
make flash term

# Clean build artifacts
make clean

# Build with debugging disabled (production)
DEVELHELP=0 make
```

The firmware uses RIOT's build system. The `RIOTBASE` variable points to the RIOT OS submodule at `./RIOT`.

## Architecture

### Communication Protocol (MFM)

The module implements an I2C slave interface via the `mfm_comm` module to communicate with the MFM master device. The protocol handles:
- Sensor initialization requests
- Measurement trigger requests
- Data payload transmission (conductivity and temperature for both probes)
- Error reporting

The main event loop processes messages:
- `MSG_MFR_INIT`: Initialize sensors (from MFM master)
- `MSG_DO_MEASURE`: Perform measurement cycle (from MFM master)
- `TASK_CLEAR_BOOT_MAGIC`: Internal boot magic timeout

### Measurement Flow

1. MFM master triggers measurement via I2C
2. Main thread receives `MSG_DO_MEASURE`
3. Power on sensors (`sensors_enable()`)
4. Initialize EC sensor with calibration data
5. Trigger temperature conversions for both probes
6. Switch to PROBE_A, get conductivity and temperature
7. Switch to PROBE_B, get conductivity and temperature
8. Power off sensors (`sensors_disable()`)
9. Send results back to MFM master via I2C

### Module Structure

```
/
├── main.c                  # Main event loop and MFM communication
├── shell_commands.c        # Interactive shell for provisioning
├── sensors.c/h            # Sensor abstraction (EC + temperature)
├── config.c/h             # EEPROM configuration management
├── control.h              # Message types and data structures
├── boards/ec-module/      # Board-specific configuration
│   └── include/
│       ├── board.h        # Pin definitions
│       ├── periph_conf.h  # Peripheral configuration
│       └── gpio_params.h  # GPIO parameters
└── modules/
    ├── ezoec/             # Atlas EZO-EC driver
    │   ├── ezoec.c
    │   └── include/ezoec.h
    ├── ds18/              # DS18B20 temperature sensor driver
    └── mfm_comm/          # MFM I2C slave protocol
        ├── mfm_comm.c
        └── include/mfm_comm.h
```

## Configuration & Calibration

Configuration is stored in EEPROM with the following structure:
- Magic header: "MFM01"
- Flags: calibration status for each probe
- K-values: probe cell constants (with 1 decimal precision)
- Calibration data: exported calibration from EZO-EC (2 probes)

### Shell Commands for Provisioning

Access the shell by setting the boot magic (double-reset within 500ms) or connecting via USB:

- **`provision`**: Full provisioning sequence
  - Configures EZO-EC baudrate (115200)
  - Factory resets EZO-EC
  - Prompts for K-values for both probes
  - Performs 3-point calibration (dry, low, high) for each probe
  - Saves calibration to EEPROM

- **`measure`**: Manual measurement of both probes

- **`export`**: Display current K-values and calibration data

- **`switch <A/B>`**: Switch active probe for debugging

- **`set_k <A/B> <value>`**: Set K-value for a probe

- **`save`**: Persist configuration to EEPROM

- **`factory`**: Clear all configuration and calibration data

- **`ec_cmd <command>`**: Send raw command to EZO-EC module

## Boot Behavior

The module implements a dual-boot system:
1. **Normal mode** (default): I2C slave mode for MFM operation
2. **Shell mode**: Interactive provisioning/calibration via UART

To enter shell mode, trigger a reset twice within 500ms window (stored in RTC backup register).

## Important Notes

### RIOT OS Patches

The project requires RIOT OS 2026.01 with custom patches for I2C slave support. The patch file `0001-i2c-slave.patch` must be applied to the RIOT submodule.

### Probe Switching

The hardware multiplexes a single EZO-EC module between two probes using `PRB_SEL_PIN` (GPIO PA6). When switching probes:
1. Set the GPIO to select probe
2. Load the correct K-value to EZO-EC
3. Load the correct calibration data to EZO-EC
4. Perform measurement

### Timing Constraints

- I2C ready time: ~1.1ms after power-on
- Config init: ~0.35ms
- Measurement time: 12 seconds (configured in `mfm_comm_params`)
- Temperature conversion: triggered at start, read at end

### Error Handling

Measurement errors are propagated to MFM master via the `mfm_comm` error reporting mechanism. Application-specific errors use `MFM_COMM_ERR_APP`.

## Development Workflow

1. **Initial setup**: Clone RIOT OS 2024.07 to `./RIOT` and apply patches
2. **Build**: `make` to compile firmware
3. **Provisioning**: Flash firmware, enter shell mode, run `provision` command
4. **Testing**: Use `measure` command to verify both probes
5. **Deployment**: Flash to module, install in MFM device

## Hardware Pin Mapping

Key pins defined in `boards/ec-module/include/board.h`:
- `LPUART_TX/RX`: PA2/PA3 (USB serial console)
- `PRB_SEL_PIN`: PA6 (probe multiplexer)
- `DQ_A_PIN`/`DQ_B_PIN`: PA5/PA7 (temperature sensors)
- `BOOST_EN_PIN`: PB12 (sensor power enable)
- `MFM_COMM_ID1/2/3`: PB9/PB5/PB8 (module identification)

I2C slave address is determined by module slot position on MFM device.
