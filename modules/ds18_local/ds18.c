/*
 * SPDX-FileCopyrightText: 2017 Frits Kuipers
 * SPDX-FileCopyrightText: 2018 HAW Hamburg
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/*
 * Local fork: 1-Wire timing via cycle-burn busy-wait
 * --------------------------------------------------
 * Upstream uses ztimer_sleep(ZTIMER_USEC, ...) for the bit-slot delays. On
 * this board (STM32L010C6 @ 32 MHz, HSI*4/2 PLL) ZTIMER_USEC is configured
 * with CONFIG_ZTIMER_USEC_ADJUST_SLEEP=27, meaning every sleep is shortened
 * by ~27 us to compensate for IRQ wake-up overhead. The 1-Wire read sample
 * point is 10 us, so the upstream sleep there returns essentially
 * immediately and the bit reads alias to the master pull-down still on the
 * wire. The DS18B20 simply never works.
 *
 * The short slot/sample/recover/reset/presence delays are now busy-waits
 * driven by a calibrated `subs/bne` loop. Calibration was done by toggling
 * a GPIO pin around the burn loop and measuring with a scope:
 *
 *     500 loops -> 46.8 us
 *     100 loops ->  8.6 us
 *     -> 38.2 us / 400 loops = 95.5 ns / loop
 *     -> ~3 core cycles / loop @ 32 MHz
 *
 * That matches the Cortex-M0+ encoding (SUBS Rd,Rd,#1 = 1 cycle,
 * BNE-taken = 2 cycles), with ART prefetch hiding the 1 flash wait state.
 *
 * Loops are derived from CLOCK_CORECLOCK so the timings remain correct if
 * the clock config ever changes. The long convert delay (750 ms) stays on
 * ztimer so the CPU can sleep.
 *
 * IRQs are masked across the timing-critical section of each bit slot to
 * keep ISR jitter from pushing the sample point outside the 1-Wire window.
 */

#include "ds18_local.h"
#include "ds18_internal.h"

#include "irq.h"
#include "periph/gpio.h"
#include "ztimer.h"

#define ENABLE_DEBUG 0
#include "debug.h"

static void ds18_low(const ds18_t *dev) {
    /* Set gpio as output and clear pin */
    gpio_init(dev->params.pin, GPIO_OUT);
    gpio_clear(dev->params.pin);
}

static void ds18_release(const ds18_t *dev) {
    /* Init pin as input */
    gpio_init(dev->params.pin, dev->params.in_mode);
}

static void ds18_write_bit(const ds18_t *dev, uint8_t bit) {
    unsigned state = irq_disable();

    /* Initiate write slot */
    ds18_low(dev);

    /* Release pin when bit==1 */
    if (bit) {
        ds18_release(dev);
    }

    /* Hold for the slot duration */
    DS18_DELAY_US(DS18_DELAY_SLOT);
    ds18_release(dev);

    irq_restore(state);

    /* Inter-slot recovery — no critical timing, IRQs back on */
    DS18_DELAY_US(DS18_DELAY_RW_PULSE);
}

static int ds18_read_bit(const ds18_t *dev, uint8_t *bit) {
    unsigned state = irq_disable();

    /* Initiate read slot */
    ds18_low(dev);
    ds18_release(dev);

    /* Wait until the sample point, read, then finish the slot */
    DS18_DELAY_US(DS18_SAMPLE_TIME);
    *bit = gpio_read(dev->params.pin);

    irq_restore(state);

    DS18_DELAY_US(DS18_DELAY_R_RECOVER);
    return DS18_OK;
}

static int ds18_read_byte(const ds18_t *dev, uint8_t *byte) {
    uint8_t bit = 0;
    *byte       = 0;

    for (int i = 0; i < 8; i++) {
        if (ds18_read_bit(dev, &bit) == DS18_OK) {
            *byte |= (bit << i);
        } else {
            return DS18_ERROR;
        }
    }

    return DS18_OK;
}

static void ds18_write_byte(const ds18_t *dev, uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        ds18_write_bit(dev, byte & (0x01 << i));
    }
}

static int ds18_reset(const ds18_t *dev) {
    int res;

    /* Reset pulse low; timing isn't sub-us-critical (must be >= 480 us)
     * so we can keep IRQs enabled here. */
    ds18_low(dev);
    DS18_DELAY_US(DS18_DELAY_RESET);
    ds18_release(dev);

    /* Presence sample point — mask IRQs to keep the 60us point accurate */
    unsigned state = irq_disable();
    DS18_DELAY_US(DS18_DELAY_PRESENCE);
    res = gpio_read(dev->params.pin);
    irq_restore(state);

    /* Tail of the reset slot */
    DS18_DELAY_US(DS18_DELAY_RESET);

    return res;
}

int ds18_trigger(const ds18_t *dev) {
    int res;

    res = ds18_reset(dev);
    if (res) {
        return DS18_ERROR;
    }

    /* Please note that this command triggers a conversion on all devices
     * connected to the bus. */
    ds18_write_byte(dev, DS18_CMD_SKIPROM);
    ds18_write_byte(dev, DS18_CMD_CONVERT);

    return DS18_OK;
}

int ds18_read(const ds18_t *dev, int16_t *temperature) {
    int res;
    uint8_t b1 = 0, b2 = 0;

    DEBUG("[DS18] Reset and read scratchpad\n");
    res = ds18_reset(dev);
    if (res) {
        return DS18_ERROR;
    }

    ds18_write_byte(dev, DS18_CMD_SKIPROM);
    ds18_write_byte(dev, DS18_CMD_RSCRATCHPAD);

    if (ds18_read_byte(dev, &b1) != DS18_OK) {
        DEBUG("[DS18] Error reading temperature byte 1\n");
        return DS18_ERROR;
    }

    DEBUG("[DS18] Received byte: 0x%02x\n", b1);

    if (ds18_read_byte(dev, &b2) != DS18_OK) {
        DEBUG("[DS18] Error reading temperature byte 2\n");
        return DS18_ERROR;
    }

    DEBUG("[DS18] Received byte: 0x%02x\n", b2);

    int32_t measurement = (int16_t)(b2 << 8 | b1);
    *temperature        = (int16_t)((100 * measurement) >> 4);

    return DS18_OK;
}

int ds18_get_temperature(const ds18_t *dev, int16_t *temperature) {

    DEBUG("[DS18] Convert T\n");
    if (ds18_trigger(dev)) {
        return DS18_ERROR;
    }

    /* Long delay — keep on ztimer so the CPU can sleep */
    DEBUG("[DS18] Wait for convert T\n");
    ztimer_sleep(ZTIMER_USEC, DS18_DELAY_CONVERT);

    return ds18_read(dev, temperature);
}

int ds18_init(ds18_t *dev, const ds18_params_t *params) {
    int res;

    dev->params = *params;

    /* Deduct the input mode from the output mode. If pull-up resistors are
     * used for output then will be used for input as well. */
    dev->params.in_mode = (dev->params.out_mode == GPIO_OD_PU) ? GPIO_IN_PU : GPIO_IN;

    /* Initialize the device and the pin */
    res = gpio_init(dev->params.pin, dev->params.in_mode) == 0 ? DS18_OK : DS18_ERROR;

    return res;
}
