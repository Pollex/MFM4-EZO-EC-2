#include "board.h"
#include "config.h"
#include "control.h"
#include "mfm_comm.h"
#include "msg.h"
#include "periph/cpu_gpio.h"
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
#include <sys/unistd.h>
#include <ztimer.h>
#define ENABLE_DEBUG 1
#include <debug.h>

#ifndef FW_VERSION
#define FW_VERSION "NO_VER"
#endif /* ifndef FW_VERSION */

// ==================================
// Flags
// ==================================
// measurement_t measurement = {0};
volatile struct __attribute__((packed)) {
    uint32_t conductivity_a;
    uint32_t conductivity_b;
    int16_t temperature_b;
    int16_t temperature_a;
} wire_measurement = {0};
kernel_pid_t main_thread_pid = 1;

// ==================================
// Errors (bit flags for multiple failures)
// ==================================
typedef enum APP_ERROR {
    ERR_NONE = 0,
    ERR_SENSOR_INIT = (1 << 0),    // Sensor initialization failed
    ERR_TEMP_A_TRIGGER = (1 << 1), // Temperature A trigger failed
    ERR_TEMP_B_TRIGGER = (1 << 2), // Temperature B trigger failed
    ERR_CONDUCTIVITY_A = (1 << 3), // Conductivity probe A failed
    ERR_CONDUCTIVITY_B = (1 << 4), // Conductivity probe B failed
    ERR_TEMP_A_READ = (1 << 5),    // Temperature A read failed
    ERR_TEMP_B_READ = (1 << 6),    // Temperature B read failed
} APP_ERROR;

// ==================================
// MSG Commands
// ==================================
typedef enum APP_MSG {
    MSG_UNKNOWN,
    MSG_MFR_INIT,
    MSG_DO_MEASURE,
} APP_MSG;

// ==================================
// MFM Communications
// ==================================

static int mfm_comm_sensor_init(void *arg);
static int mfm_comm_perform_measurement(void *arg);
static const mfm_comm_params_t mfm_comm_params = {
    .firmware_version = FW_VERSION,
    .module_type = 0xFF,
    .measurement_time = 15000,
    .sensor_count = 1,
    .sensor_init_fn = &mfm_comm_sensor_init,
    .perform_measurement_fn = &mfm_comm_perform_measurement,
};
static mfm_comm_t mfm_comm;

/**
 * @brief Called from an interrupt when the MFM tasks us to do a mfr sensor
 * init.
 *
 * @param arg
 * @return 0 on success, negative on error.
 */
static int mfm_comm_sensor_init(void *arg) {
    (void)arg;

    msg_t msg;

    msg.type = MSG_MFR_INIT;
    msg_try_send(&msg, main_thread_pid);

    return 0;
}

/**
 * @brief Called from an interrupt when the MFM tasks us to start a measurement.
 *
 * @param arg
 * @return 0 on success, negative on error.
 */
static int mfm_comm_perform_measurement(void *arg) {
    (void)arg;

    msg_t msg;

    msg.type = MSG_DO_MEASURE;
    msg_try_send(&msg, main_thread_pid);

    return 0;
}

// ==================================
// Shell commands
// ==================================

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

// ==================================
// Main routine
// ==================================

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

    gpio_init(PRB_SEL_PIN, GPIO_OUT);
    gpio_init(BOOST_EN_PIN, GPIO_OUT);
    gpio_clear(BOOST_EN_PIN);

    main_thread_pid = thread_getpid();
    msg_init_queue(_msg_queue, 4);

    // Setup I2C with master.
    mfm_comm_init(&mfm_comm, mfm_comm_params);

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
        case MSG_MFR_INIT:
            DEBUG("Sensor init\n");
            mfm_comm_sensor_init_finish(&mfm_comm);
            break;
        case MSG_DO_MEASURE: {
            DEBUG("Sensor measure\n");

            measurement_t measurement = {0};
            uint8_t error_flags = ERR_NONE;

            sensors_enable();
            ztimer_sleep(ZTIMER_MSEC, 10);

            int result = sensors_init();
            if (result < 0) {
                DEBUG("ERR(%d) sensors init\n", result);
                sensors_disable();
                mfm_comm_measurement_error(&mfm_comm, ERR_SENSOR_INIT);
                break;
            }
            result = sensors_trigger_temperature(PROBE_A);
            if (result < 0) {
                DEBUG("ERR(%d) trigger temp A\n", result);
                error_flags |= ERR_TEMP_A_TRIGGER;
            }
            result = sensors_trigger_temperature(PROBE_B);
            if (result < 0) {
                DEBUG("ERR(%d) trigger temp B\n", result);
                error_flags |= ERR_TEMP_B_TRIGGER;
            }
            result =
                sensors_get_conductivity(PROBE_A, &measurement.conductivity_a);
            if (result < 0) {
                DEBUG("ERR(%d) conduc A\n", result);
                measurement.conductivity_a = 0;
                error_flags |= ERR_CONDUCTIVITY_A;
            }
            result =
                sensors_get_conductivity(PROBE_B, &measurement.conductivity_b);
            if (result < 0) {
                DEBUG("ERR(%d) conduc B\n", result);
                measurement.conductivity_b = 0;
                error_flags |= ERR_CONDUCTIVITY_B;
            }
            result =
                sensors_get_temperature(PROBE_A, &measurement.temperature_a);
            if (result < 0) {
                DEBUG("ERR(%d) get temp A\n", result);
                measurement.temperature_a = 0;
                error_flags |= ERR_TEMP_A_READ;
            }
            result =
                sensors_get_temperature(PROBE_B, &measurement.temperature_b);
            if (result < 0) {
                DEBUG("ERR(%d) get temp B\n", result);
                measurement.temperature_b = 0;
                error_flags |= ERR_TEMP_B_READ;
            }
            sensors_disable();

            // Report any errors.
            if (error_flags != ERR_NONE) {
                mfm_comm_measurement_error(&mfm_comm, error_flags);
            }

            wire_measurement.conductivity_a = measurement.conductivity_a;
            wire_measurement.conductivity_b = measurement.conductivity_b;
            wire_measurement.temperature_a = measurement.temperature_a;
            wire_measurement.temperature_b = measurement.temperature_b;

            mfm_comm_measurement_finish(&mfm_comm, (void *)&wire_measurement,
                                        sizeof(wire_measurement));
        } break;
        case TASK_CLEAR_BOOT_MAGIC:
            PWR->CR |= PWR_CR_DBP;
            RTC->BKP0R = 0;
            break;
        }
    }
    return 0;
}
