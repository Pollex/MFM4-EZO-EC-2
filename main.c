#include "board.h"
#include "config.h"
#include "control.h"
#include "i2c_slave.h"
#include "msg.h"
#include "periph/cpu_gpio.h"
#include "periph/cpu_gpio_ll.h"
#include "periph/gpio.h"
#include "sched.h"
#include "sensors.h"
#include "shell.h"
#include "shell_commands.h"
#include "stm32l010x6.h"
#include "thread.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <ztimer.h>
#define ENABLE_DEBUG 1
#include <debug.h>

#ifndef FW_VERSION
#define FW_VERSION "NO_VER"
#endif /* ifndef FW_VERSION */

measurement_t measurement = {0};
uint8_t do_measurement = 0;
uint8_t measurement_status = STATUS_NOT_READY;
uint8_t do_initialize = 0;
uint8_t initialize_status = STATUS_NOT_READY;
kernel_pid_t main_thread_pid = 1;

static const shell_command_t shell_commands[] = {
    {"provision", "Full provisioning sequence for EC Module board",
     cmd_provision},
    {"measure", "Performs a full measurement", cmd_do_measurement},
    {"export", "Exports the currently loaded configuration", cmd_config_export},
    {"switch", "Switches the current active probe", cmd_switch_probe},
    {"set_k", "Sets the K-Value for a probe", cmd_set_k},
    {"save", "Saves the configuration to memory", cmd_save},
    {"factory", "Clears all configuration and calibrations", cmd_factory_reset},
    {"ec_cmd", "Debugging: send command to EZOEC module", cmd_ec_cmd},
    {NULL, NULL, NULL},
};

int read_slot_id(void) {
    int id = 0;
    if (gpio_read(MOD_ID1_PIN) > 0)
        id |= 1 << 0;
    if (gpio_read(MOD_ID2_PIN) > 0)
        id |= 1 << 1;
    if (gpio_read(MOD_ID3_PIN) > 0)
        id += 3;
    return id;
}

int main_shell(void) {
    sensors_init();
    char line_buf[SHELL_DEFAULT_BUFSIZE * 2];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE * 2);
    return 0;
}

#define SHELL_MAGIC 0x24C0FFEE
int should_boot_shell(void) {
    // If magic already set then we should boot to shell
    if (RTC->BKP0R == SHELL_MAGIC) {
        // clear magic to bkp0r
        PWR->CR |= PWR_CR_DBP;
        RTC->BKP0R = 0;
        return 1;
    }

    // otherwise write magic to bkp0r and queue a clear message in 500ms to
    // prevent boot delay
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP0R = SHELL_MAGIC;

    static ztimer_t clear_timer;
    static msg_t clear_msg = {.type = TASK_CLEAR_BOOT_MAGIC};
    ztimer_set_msg(ZTIMER_MSEC, &clear_timer, 500, &clear_msg, main_thread_pid);

    return 0;
}

/*
 * The system's I2C is ready in about 1.10ms after poweron.
 * This is based on a GPIO SET after i2c_slave_init, on poweron the test pin
 * will show disparity to ground up until the GPIO_SET.
 */

#define PIN_TEST GPIO_PIN(PORT_A, 15)
static msg_t _msg_queue[4] = {0};
int main(void) {
    // gpio_init(PIN_TEST, GPIO_OUT);
    // gpio_set(PIN_TEST);

    gpio_init(MOD_ID1_PIN, GPIO_IN);
    gpio_init(MOD_ID2_PIN, GPIO_IN);
    gpio_init(MOD_ID3_PIN, GPIO_IN);
    gpio_init(PRB_SEL_PIN, GPIO_OUT);
    gpio_init(BOOST_EN_PIN, GPIO_OUT);
    gpio_clear(BOOST_EN_PIN);

    main_thread_pid = thread_getpid();
    msg_init_queue(_msg_queue, 4);

    uint8_t slot_id = read_slot_id();
    if (slot_id == 0)
        slot_id = 1;
    DEBUG("SLOT: 0x%02X\n", slot_id);
    i2c_slave_init(0x10 + slot_id);

    // Duration: 0.35 ms
    config_init();

    printf("FWVER: %s\n", FW_VERSION);

    // Duration: 0.038 ms
    if (should_boot_shell())
        return main_shell();

    static msg_t msg = {0};
    for (;;) {
        DEBUG("Wait msg\n");
        msg_receive(&msg);
        switch (msg.type) {
        case TASK_SENSOR_INIT:
            DEBUG("Sensor init\n");
            do_initialize = 0;
            break;
        case TASK_MEASUREMENT:
            DEBUG("Sensor measure\n");
            measurement_status = STATUS_BUSY;
            sensors_enable();
            ztimer_sleep(ZTIMER_MSEC, 10);
            int result = sensors_init();
            if (result < 0) {
                DEBUG("ERR(%d) sensors init\n", result);
            }
            result = sensors_trigger_temperature(PROBE_A);
            if (result < 0) {
                DEBUG("ERR(%d) trigger temp A\n", result);
            }
            result = sensors_trigger_temperature(PROBE_B);
            if (result < 0) {
                DEBUG("ERR(%d) trigger temp B\n", result);
            }
            result =
                sensors_get_conductivity(PROBE_A, &measurement.conductivity_a);
            if (result < 0) {
                DEBUG("ERR(%d) conduc A\n", result);
            }
            result =
                sensors_get_conductivity(PROBE_B, &measurement.conductivity_b);
            if (result < 0) {
                DEBUG("ERR(%d) conduc B\n", result);
            }
            result =
                sensors_get_temperature(PROBE_A, &measurement.temperature_a);
            if (result < 0) {
                DEBUG("ERR(%d) get temp A\n", result);
            }
            result =
                sensors_get_temperature(PROBE_B, &measurement.temperature_b);
            if (result < 0) {
                DEBUG("ERR(%d) get temp B\n", result);
            }
            sensors_disable();

            measurement_status = STATUS_READY;
            do_measurement = 0;
            break;
        case TASK_CLEAR_BOOT_MAGIC:
            PWR->CR |= PWR_CR_DBP;
            RTC->BKP0R = 0;
            break;
        }
    }
    return 0;
}
