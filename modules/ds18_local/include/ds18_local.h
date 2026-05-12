/*
 * Copyright (C) 2017 Frits Kuipers
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#pragma once

/**
 * @defgroup    drivers_ds18 DS18 temperature sensor driver
 * @ingroup     drivers_sensors
 * @ingroup     drivers_saul
 * @brief       Driver interface for the DS18 temperature sensors
 *
 * This driver provides @ref drivers_saul capabilities.
 * Currently the driver has the following limitations:
 *- Does not allow addressing devices, only supports a single device on the bus.
 *- The 1-Wire bus handling is hardcoded to the driver.
 *- Does not allow configuration of sampling width.
 *
 * @note Due to timing issues present on some boards this drivers features two
 * ways of reading information from the sensor. The optimized uses accurate
 * delays to handle this, while the second way polls the line for changes. If
 * you know that your board can handle ~3us resolution with the xtimer module,
 * then the optimized way is recommended. To used the optimized way add the
 * ds18_optimized module. Also this driver test application has a whitelist of
 * the boards this driver has been tested on and known to work.
 *
 * @{
 *
 * @file
 * @brief       Driver for Maxim Integrated DS1822 and DS18B20 temperature
 *              sensors.
 *
 * @author      Frits Kuipers <frits.kuipers@gmail.com>
 */

#include <stdint.h>

#include "periph/gpio.h"
#include "periph_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name ds18 status return codes
 * @{
 */
#define DS18_OK                       (0)
#define DS18_ERROR                    (-1)
/** @} */

/**
 * @name 1-Wire busy-wait timing primitive (see ds18.c for calibration notes)
 * @{
 */
#define DS18_BURN_CYCLES_PER_LOOP   3U
#define DS18_BURN_LOOPS(us) \
    (((uint32_t)(us) * (CLOCK_CORECLOCK / 1000000U) \
      + (DS18_BURN_CYCLES_PER_LOOP - 1U)) / DS18_BURN_CYCLES_PER_LOOP)

static inline __attribute__((always_inline)) void ds18_burn_loops(uint32_t loops)
{
    __asm__ volatile(".syntax unified\n"
                     "1: subs %0, %0, #1\n"
                     "   bne  1b\n"
                     : "+l"(loops)::"cc");
}

/** Busy-wait `us` microseconds. Loop count is constant-folded when `us` is. */
#define DS18_DELAY_US(us) ds18_burn_loops(DS18_BURN_LOOPS(us))
/** @} */

/**
 * @brief Device initialization parameters
 */
typedef struct {
    gpio_t pin;             /**< Pin the sensor is connected to */
    gpio_mode_t out_mode;   /**< Pin output mode */
    gpio_mode_t in_mode;    /**< Pin input mode (usually deduced from output mode) */
} ds18_params_t;

/**
 * @brief   Device descriptor for a ds18 device
 */
typedef struct {
    ds18_params_t params;   /**< Device Parameters */
} ds18_t;

/**
 * @brief   Initialize a ds18 device
 *
 * @param[out] dev          device descriptor
 * @param[in]  params       ds18 initialization struct
 *
 *
 * @return                   0 on success
 * @return                  -1 on error
 */
int ds18_init(ds18_t *dev, const ds18_params_t *params);

/**
 * @brief Triggers a temperature conversion
 * @note This also triggers a conversion on all devices connected to the bus
 *
 * @param[in] dev           device descriptor
 *
 * @return                  0 on success
 * @return                 -1 on error
 */
int ds18_trigger(const ds18_t *dev);

/**
 * @brief Reads the scratchpad for the last conversion
 *
 * @param[in] dev           device descriptor
 * @param[out] temperature  buffer to write the temperature in centi-degrees
 *
 * @return                  0 on success
 * @return                 -1 on error
 */
int ds18_read(const ds18_t *dev, int16_t *temperature);

/**
 * @brief   convenience function for triggering a conversion and reading the
 * value
 *
 * @note This function will block for the conversion time. The current
 * implementation of the driver uses 12-bit resolution, so this time is 750 ms.
 *
 * @param[in] dev           device descriptor
 * @param[out] temperature  buffer to write the temperature in centi-degrees
 *
 * @return                   0 on success
 * @return                  -1 on error
 */
int ds18_get_temperature(const ds18_t *dev, int16_t *temperature);

#ifdef __cplusplus
}
#endif

/** @} */
