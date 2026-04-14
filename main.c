#include "board.h"
#include "config.h"
#include "ds18.h"
#include "ezoec.h"
#include "mfm_comm.h"
#include "msg.h"
#include "periph/cpu_gpio.h"
#include "periph/eeprom.h"
#include "periph/gpio.h"
#include "periph/uart.h"
#include "sched.h"
#include "shell.h"
#include "stm32l010x6.h"
#include "thread.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <ztimer.h>
#define ENABLE_DEBUG 0
#include <debug.h>

#ifndef FW_VERSION
#define FW_VERSION "NO_VER"
#endif /* ifndef FW_VERSION */

// ==================================
// Variables
// ==================================
static ezoec_t ec                     = {0};
static ds18_t t1                      = {0};
static ds18_t t2                      = {0};
static const ezoec_params_t ec_params = {
    .baud_rate = 115200,
    .uart      = UART_DEV(1),
};
static const ds18_params_t t1_params = {
    .pin      = DQ_A_PIN,
    .out_mode = GPIO_OD_PU,
};
static const ds18_params_t t2_params = {
    .pin      = DQ_B_PIN,
    .out_mode = GPIO_OD_PU,
};

typedef enum {
    PROBE_A,
    PROBE_B,
} probe_t;

typedef struct {
    uint32_t conductivity_a;
    uint32_t conductivity_b;
    int16_t temperature_b;
    int16_t temperature_a;
} measurement_t;

// ==================================
// Flags
// ==================================
// measurement_t measurement = {0};
volatile struct __attribute__((packed)) {
    uint32_t conductivity_a;
    uint32_t conductivity_b;
    int16_t temperature_b;
    int16_t temperature_a;
} wire_measurement                  = {0};
static kernel_pid_t main_thread_pid = 1;

// ==================================
// Errors (bit flags for multiple failures)
// ==================================
typedef enum APP_ERROR {
    ERR_NONE           = 0,
    ERR_SENSOR_INIT    = (1 << 0), // Sensor initialization failed
    ERR_TEMP_A_TRIGGER = (1 << 1), // Temperature A trigger failed
    ERR_TEMP_B_TRIGGER = (1 << 2), // Temperature B trigger failed
    ERR_CONDUCTIVITY_A = (1 << 3), // Conductivity probe A failed
    ERR_CONDUCTIVITY_B = (1 << 4), // Conductivity probe B failed
    ERR_TEMP_A_READ    = (1 << 5), // Temperature A read failed
    ERR_TEMP_B_READ    = (1 << 6), // Temperature B read failed
} APP_ERROR;

// ==================================
// MSG Commands
// ==================================
typedef enum APP_MSG {
    MSG_UNKNOWN,
    MSG_MFR_INIT,
    MSG_DO_MEASURE,
    MSG_CLEAR_BOOT_MAGIC,
} APP_MSG;

// ==================================
// MFM Communications
// ==================================

static int mfm_comm_sensor_init(void *arg);
static int mfm_comm_perform_measurement(void *arg);
static const mfm_comm_params_t mfm_comm_params = {
    .firmware_version       = FW_VERSION,
    .module_type            = 0xFF,
    .measurement_time       = 15000,
    .sensor_count           = 1,
    .sensor_init_fn         = &mfm_comm_sensor_init,
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
    if (msg_try_send(&msg, main_thread_pid) != 1) {
        DEBUG("mfm_comm sensor init msg not sent.");
        return -1; // Action was not sent.
    };

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
    if (msg_try_send(&msg, main_thread_pid) != 1) {
        DEBUG("mfm_comm perform msg not sent.");
        return -1; // Action was not sent.
    };

    return 0;
}

// ==================================
// Config
// ==================================

eeprom_config_t eeprom_config = {0};

int config_has_calibration(uint8_t probe) {
    if (probe == PROBE_A)
        return (eeprom_config.flags & CFG_FLAG_A_CALIBRATED) > 0;
    if (probe == PROBE_B)
        return (eeprom_config.flags & CFG_FLAG_B_CALIBRATED) > 0;
    return -EINVAL;
}

int config_init(void) {
    eeprom_read(0, &eeprom_config, sizeof(eeprom_config));
    if (strcmp(eeprom_config.magic, CFG_MAGIC_HEADER) != 0) {
        puts("!!! NOTICE: corrupted config, resetting");
        config_clear();
        config_persist();
    }
    // if (!config_has_calibration(PROBE_A)) {
    //     puts("!!! NOTICE: Probe A is not calibrated");
    // }
    // if (!config_has_calibration(PROBE_B)) {
    //     puts("!!! NOTICE: Probe B is not calibrated");
    // }
    return 0;
}

int config_clear(void) {
    memset(&eeprom_config, 0x00, sizeof(eeprom_config));
    memcpy(&eeprom_config.magic, CFG_MAGIC_HEADER, sizeof(CFG_MAGIC_HEADER));
    return 0;
}

int config_persist(void) { return eeprom_write(0, &eeprom_config, sizeof(eeprom_config)); }

// ==================================
// Functions
// ==================================

int sensors_init(void) {
    int result = ezoec_init(&ec, &ec_params);
    if (result < 0) {
        printf("EZOEC Initialization error: %d\n", result);
        return -1;
    }

    result = ds18_init(&t1, &t1_params);
    if (result < 0) {
        printf("DS18B20 A Initialization error: %d\n", result);
        return -1;
    }

    result = ds18_init(&t2, &t2_params);
    if (result < 0) {
        printf("DS18B20 B Initialization error: %d\n", result);
        return -1;
    }

    return 0;
}

void sensors_enable(void) {
    gpio_init(BOOST_EN_PIN, GPIO_OUT);
    gpio_set(BOOST_EN_PIN);
}
void sensors_disable(void) {
    gpio_clear(BOOST_EN_PIN);
    gpio_init(BOOST_EN_PIN, GPIO_IN);
}

int sensors_trigger_temperature(probe_t probe) {
    ds18_t *t = &t1;
    if (probe == PROBE_B) {
        t = &t2;
    }
    int result = ds18_trigger(t);
    if (result < 0) {
        return result;
    }
    return 0;
}
int sensors_get_temperature(probe_t probe, int16_t *out) {
    ds18_t *t = &t1;
    if (probe == PROBE_B) {
        t = &t2;
    }
    *out       = 0;
    int result = ds18_read(t, out);
    if (result < 0) {
        return result;
    }
    return 0;
}

static int switch_probe(uint8_t index) {
    gpio_write(PRB_SEL_PIN, index);
    return 0;
}

int sensors_get_conductivity(probe_t probe, uint32_t *out) {
    int result;
    *out = 0;

    // Switch probe
    switch_probe(probe);

    // Set probe K
    uint8_t k = eeprom_config.k_values[probe];
    if (k > 0) {
        result = ezoec_set_k(&ec, k);
        if (result < 0) {
            return result;
        }
    } else {
        printf("Warning: probe %c has no K value set\n", probe == PROBE_A ? 'A' : 'B');
    }

    // Load calibration into ezoec
    if (config_has_calibration(probe)) {
        result = ezoec_cal_import(&ec, &eeprom_config.calibration[probe]);
        if (result < 0) {
            return result;
        }
        // TODO: Add justification for 1s delay.
        ztimer_sleep(ZTIMER_MSEC, 1000);
    } else {
        printf("Warning: probe %c has no calibration\n", probe == PROBE_A ? 'A' : 'B');
    }

    result = ezoec_measure(&ec, out);
    if (result < 0) {
        *out = 0;
        return result;
    }

    return 0;
}

// ==================================
// Shell commands
// ==================================

static char *_int_to_string(uint32_t k, uint8_t precision, char *buf) {
    enum { buf_len = 20 };
    static char internal_buf[buf_len + 1] = {0};
    if (buf == NULL)
        buf = internal_buf;
    char *ptr = &buf[buf_len - 1];

    if (k == 0) {
        *ptr-- = '0';
        return ptr + 1;
    }

    int decimals = 0;
    while (k > 0) {
        char decimal = 0x30 + (k % 10);
        k /= 10;

        *ptr-- = decimal;
        if (++decimals == precision) {
            *ptr-- = '.';
            if (k == 0)
                *ptr-- = '0';
        }
    }
    return ptr + 1;
}

#define STABLE_READING_SAMPLES 12
static int wait_for_stable_readings(uint32_t timeout, uint32_t tolerance_uS) {
    uint32_t readings[STABLE_READING_SAMPLES] = {0};
    uint8_t total_readings                    = 0;

    uint32_t start = ztimer_now(ZTIMER_MSEC);
    int result     = 0;
    for (;;) {
        //
        // Check if we've reached timeout limit
        if (ztimer_now(ZTIMER_MSEC) - start > timeout) {
            return -ETIMEDOUT;
        }

        //
        // Get a new measurement
        result = ezoec_measure(&ec, &readings[total_readings % STABLE_READING_SAMPLES]);
        if (result < 0) {
            return result; // Return the error code
        }
        // Convert nS to uS
        readings[total_readings % STABLE_READING_SAMPLES] /= 1000;
        total_readings++;

        // Only check std with X samples
        printf("Collecting samples: %2d \t-->\t%ld uS\n", total_readings,
               readings[(total_readings - 1) % STABLE_READING_SAMPLES]);
        if (total_readings < STABLE_READING_SAMPLES) {
            continue;
        }

        //
        // Calculate Standard Deviation
        uint8_t sample_count = total_readings < STABLE_READING_SAMPLES ? total_readings : STABLE_READING_SAMPLES;
        uint32_t mean        = 0;
        for (int i = 0; i < sample_count; i++) {
            mean += readings[i];
        }
        mean /= sample_count;

        uint32_t variance = 0;
        for (int i = 0; i < sample_count; i++) {
            int32_t deviation = readings[i] - mean;
            variance += deviation * deviation; // Square the deviation
        }
        variance /= sample_count;

        printf("Mean: %ld\tVariance²: %6ld\nMax variance² allowed: %6ld\n", mean, variance,
               tolerance_uS * tolerance_uS);

        // Validate
        if (variance < tolerance_uS * tolerance_uS) {
            return 0; // Return 0 on stable readings
        }
    }

    return 0; // This line is unreachable due to the loop
}

static void _wait_for_enter(void) {
    while (getchar() != 0x0A)
        ;
}

static int32_t _read_int(uint8_t precision) {
    enum { rx_buffer_size = 20 };
    static char rx_buffer[rx_buffer_size] = {0};

    char *ptr_start = rx_buffer;
    char *ptr_end   = ptr_start; // will be incremented during reading
    char *ptr       = ptr_start;

    uint8_t c = 0;
    for (;;) {
        c = getchar();
        if (c == 0x0A) {
            break;
        }
        *ptr_end++ = c;
        if ((ptr_end - ptr_start) >= rx_buffer_size) {
            break;
        }
    }
    if (c != 0x0A) {
        return -ENOBUFS;
    }

    // Convert to int
    int32_t value    = 0;
    uint8_t decimals = 0;
    while (ptr < ptr_end) {
        if (*ptr == '.') {
            decimals = ptr_end - ptr - 1;
            ptr++;
            continue;
        }

        value *= 10;
        value += (*ptr - 0x30);
        ptr++;
    }
    // Correct for potential missing decimals
    for (int x = precision - decimals; x > 0; x--) {
        value *= 10;
    }

    return value;
}

static int32_t _read_mS(void) { return _read_int(3); }
static int32_t _read_kvalue(void) { return _read_int(1); }

#define CAL_TOLERANCE_DRY_uS  1000
#define CAL_TOLERANCE_LOW_uS  1000
#define CAL_TOLERANCE_HIGH_uS 1000
int cmd_provision(int argc, char **argv) {
    int calibrate_a = 1;
    int calibrate_b = 1;

    if (argc >= 2) {
        if (argv[1][0] == 'A' || argv[1][0] == 'a') {
            calibrate_b = 0;
        } else if (argv[1][0] == 'B' || argv[1][0] == 'b') {
            calibrate_a = 0;
        } else {
            printf("Usage: %s [A|B]\n", argv[0]);
            return -1;
        }
    }

    puts("1. Fixing EZOEC configuration");
    ezoec_params_t params = {
        .baud_rate = 9600,
        .uart      = UART_DEV(1),
    };
    int result = ezoec_init(&ec, &params);
    if (result < 0) {
        printf("Could not initialize EZOEC on factory settings, perhaps its "
               "already fixed: %d\n",
               result);
    } else {
        ezoec_set_baud(&ec, 115200);
        puts("Fixed ezoec baudrate");
    }
    result = ezoec_init(&ec, &ec_params);
    if (result < 0) {
        printf("Could not initialize EZOEC after fix, aborting: %d\n", result);
        return result;
    }
    // Factory reset (does not affect baud)
    result = ezoec_factory(&ec);
    if (result < 0) {
        printf("Could not factory reset EZOEC: %d\n", result);
        return result;
    }
    puts("Factory reset EZOEC");
    // Disable continuous mode
    result = ezoec_cmd(&ec, 0, NULL, "C,0");
    if (result < 0) {
        printf("Could not disable continuous mode: %d\n", result);
        return result;
    }
    puts("Disabled EZOEC continuous reading");
    // Disable LED
    result = ezoec_cmd(&ec, 0, NULL, "L,0");
    if (result < 0) {
        printf("Could not disable LED: %d\n", result);
        return result;
    }
    puts("Disabled EZOEC LED");

    int k_value;
    if (calibrate_a) {
    retry_ka:
        puts("2. K-Value of probe A: ");
        k_value = _read_kvalue();
        if (k_value < 0) {
            printf("error(%d), try again\n", k_value);
            goto retry_ka;
        }
        eeprom_config.k_values[0] = k_value;
        printf("Got %s\n", _int_to_string(k_value, 1, NULL));
    }

    if (calibrate_b) {
    retry_kb:
        puts("3. K-Value of probe B: ");
        k_value = _read_kvalue();
        if (k_value < 0) {
            printf("error(%d), try again\n", k_value);
            goto retry_kb;
        }
        eeprom_config.k_values[1] = k_value;
        printf("Got %s\n", _int_to_string(k_value, 1, NULL));
    }

    for (int probe = 0; probe < 2; probe++) {
        if (probe == PROBE_A && !calibrate_a)
            continue;
        if (probe == PROBE_B && !calibrate_b)
            continue;
        printf("Switching to probe: %c (K: %s)\n", probe == 0 ? 'A' : 'B',
               _int_to_string(eeprom_config.k_values[probe], 1, NULL));
        switch_probe(probe);
        result = ezoec_set_k(&ec, eeprom_config.k_values[probe]);
        if (result < 0) {
            printf("There are issues setting the K value (error %d).\n", result);
            return result;
        }
    retry_dry:
        printf("4%c.1: Dry calibration. Make sure the probe is dry and "
               "press "
               "enter...\n",
               probe == 0 ? 'A' : 'B');
        _wait_for_enter();
        puts("Waiting for readings to stabalize...");
        result = wait_for_stable_readings(10000, CAL_TOLERANCE_DRY_uS);
        if (result < 0) {
            printf("There are issues getting a stable reading (error %d). "
                   "Check connections and dryness.\n",
                   result);
            goto retry_dry;
        }
        result = ezoec_cal_dry(&ec);
        if (result < 0) {
            printf("Could not perform calibration: %d\n", result);
            return result;
        }

    retry_low:
        printf("5%c.1: Low calibration. Put probe in low mS solution.\n", probe == 0 ? 'A' : 'B');
        puts("Solution concentration in mS: ");
        // We ask the user for mS with max 3 decimals so store as uS
        uint32_t uS = _read_mS();
        printf("Calibrating for: %s mS\n", _int_to_string(uS, 3, NULL));
        puts("Waiting for readings to stabalize...");
        result = wait_for_stable_readings(10000, CAL_TOLERANCE_LOW_uS);
        if (result < 0) {
            printf("There are issues getting a stable reading (error %d). "
                   "Check for trapped air.\n",
                   result);
            goto retry_low;
        }
        result = ezoec_cal_low(&ec, uS);
        if (result < 0) {
            printf("Could not perform calibration: %d\n", result);
            return result;
        }

    retry_high:
        printf("6%c.1: High calibration. Put probe in high uS solution.\n", probe == 0 ? 'A' : 'B');
        puts("Solution concentration in mS: ");
        uS = _read_mS();
        printf("Calibrating for: %s mS\n", _int_to_string(uS, 3, NULL));
        puts("Waiting for readings to stabalize...");
        result = wait_for_stable_readings(10000, CAL_TOLERANCE_HIGH_uS);
        if (result < 0) {
            printf("There are issues getting a stable reading (error %d). "
                   "Check for trapped air.\n",
                   result);
            goto retry_high;
        }
        result = ezoec_cal_high(&ec, uS);
        if (result < 0) {
            printf("Could not perform calibration: %d\n", result);
            return result;
        }

        printf("7%c.1: Calibration done, exporting calibration values...", probe == 0 ? 'A' : 'B');
        result = ezoec_cal_export(&ec, &eeprom_config.calibration[probe]);
        if (result < 0) {
            printf("Could not export calibration: %d\n", result);
            return result;
        }
        eeprom_config.flags |= CFG_FLAG_A_CALIBRATED << probe;
    }

    puts("7. Provisioning is now done, press enter to save...");
    _wait_for_enter();
    config_persist();
    puts("Saved, module is ready for use. If a probe is swapped, run this "
         "procedure again!");

    return 0;
}

int cmd_ec_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (argc < 2) {
        printf("Usage: %s <command>\n", argv[0]);
        return -1;
    }

    ezoec_writeline(&ec, argv[1]);
    char buf[RX_MAX_LINE_LEN] = {0};
    int result                = 0;
    for (;;) {
        result = ezoec_readline(&ec, buf, RX_MAX_LINE_LEN, 1000);
        if (result < 0)
            break;
        buf[result] = 0;
        printf("<<< %s\n", buf);
    }

    return 0;
}

int cmd_do_measurement(int argc, char **argv) {
    (void)argc;
    (void)argv;

    measurement_t measurement = {0};

    sensors_enable();
    ztimer_sleep(ZTIMER_MSEC, 1000);

    puts("Triggering temperature sensors");
    int result = sensors_trigger_temperature(PROBE_A);
    if (result < 0) {
        printf("ERR(%d) trigger temp A\n", result);
    }
    result = sensors_trigger_temperature(PROBE_B);
    if (result < 0) {
        printf("ERR(%d) trigger temp B\n", result);
    }
    puts("Measuring conductivity on probe A");
    result = sensors_get_conductivity(PROBE_A, &measurement.conductivity_a);
    if (result < 0) {
        printf("ERR(%d) conduc A\n", result);
    }
    puts("Measuring conductivity on probe B");
    result = sensors_get_conductivity(PROBE_B, &measurement.conductivity_b);
    if (result < 0) {
        printf("ERR(%d) conduc B\n", result);
    }
    puts("Reading temperature sensors");
    result = sensors_get_temperature(PROBE_A, &measurement.temperature_a);
    if (result < 0) {
        printf("ERR(%d) get temp A\n", result);
    }
    result = sensors_get_temperature(PROBE_B, &measurement.temperature_b);
    if (result < 0) {
        printf("ERR(%d) get temp B\n", result);
    }

    sensors_disable();

    char buf[20 * 4 + 1] = {0};
    printf("===========================================\n"
           "           Conductivity        Temperature \n"
           "PROBE A    %s uS                %s C       \n"
           "PROBE B    %s uS                %s C       \n",
           _int_to_string(measurement.conductivity_a, 3, buf), _int_to_string(measurement.temperature_a, 2, buf + 20),
           _int_to_string(measurement.conductivity_b, 3, buf + 40),
           _int_to_string(measurement.temperature_b, 2, buf + 60));

    return 0;
}

void _print_ezoec_calibration(ezoec_calibration_t *cal) {
    for (int ix = 0; ix < EZOEC_CALIBRATION_MAX_LINES; ix++) {
        printf("%.*s", EZOEC_CALIBRATION_LINE_LENGTH, cal->line[ix]);
    }
    puts("");
}
int cmd_config_export(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("=============== PROBE A ===================");
    printf("K-Value: %s\nCalibration: ", _int_to_string(eeprom_config.k_values[PROBE_A], 1, NULL));
    _print_ezoec_calibration(&eeprom_config.calibration[PROBE_A]);
    puts("\n\n============== PROBE B ====================");
    printf("K-Value: %s\nCalibration: ", _int_to_string(eeprom_config.k_values[PROBE_B], 1, NULL));
    _print_ezoec_calibration(&eeprom_config.calibration[PROBE_B]);
    puts("\n");

    return 0;
}

int cmd_switch_probe(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (argc < 2) {
        printf("Usage: %s <A/B>\n", argv[0]);
        return 0;
    }

    uint8_t probe = PROBE_A;
    if (argv[1][0] == 'a' || argv[1][0] == 'A') {
        probe = PROBE_A;
    } else if (argv[1][0] == 'b' || argv[1][0] == 'B') {
        probe = PROBE_B;
    } else {
        printf("Usage: %s <A/B>\n", argv[0]);
        return 0;
    }

    switch_probe(probe);

    return 0;
}

int cmd_save(int argc, char **argv) {
    (void)argc;
    (void)argv;

    config_persist();
    puts("Saved");

    return 0;
}

int cmd_set_k(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (argc < 3) {
        printf("Usage: %s <A/B> <K_Value>\nThe K value can have up to 1 decimal\n", argv[0]);
        return 0;
    }

    uint8_t probe = PROBE_A;
    if (argv[1][0] == 'a' || argv[1][0] == 'A') {
        probe = PROBE_A;
    } else if (argv[1][0] == 'b' || argv[1][0] == 'B') {
        probe = PROBE_B;
    } else {
        printf("Usage: %s <A/B> <K_Value>\nThe K value can have up to 1 decimal\n", argv[0]);
        return 0;
    }

    int k_value;
retry_k:
    k_value = _read_kvalue();
    if (k_value < 0) {
        printf("error(%d), try again\n", k_value);
        goto retry_k;
    }
    eeprom_config.k_values[probe] = k_value;

    return 0;
}

int cmd_factory_reset(int argc, char **argv) {
    (void)argc;
    (void)argv;

    config_clear();
    puts("Config cleared, use the `save` command to save the empty config");

    return 0;
}

int cmd_boost(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <on|off>\n", argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        sensors_enable();
        puts("Booster enabled");
    } else if (strcmp(argv[1], "off") == 0) {
        sensors_disable();
        puts("Booster disabled");
    } else {
        printf("Usage: %s <on|off>\n", argv[0]);
        return -1;
    }

    return 0;
}

int cmd_temp(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int status = ds18_init(&t1, &t1_params);
    if (status < 0) {
        printf("Error init A: %d\n", status);
    }
    status = ds18_init(&t2, &t2_params);
    if (status < 0) {
        printf("Error init B: %d\n", status);
    }

    int16_t out_a = 0;
    int16_t out_b = 0;

    status = ds18_trigger(&t1);
    if (status < 0) {
        printf("Error trig A: %d\n", status);
    }
    status = ds18_trigger(&t2);
    if (status < 0) {
        printf("Error trig B: %d\n", status);
    }

    ztimer_sleep(ZTIMER_MSEC, 750);

    status = ds18_read(&t1, &out_a);
    if (status < 0) {
        printf("Error read A: %d\n", status);
    }
    status = ds18_read(&t2, &out_b);
    if (status < 0) {
        printf("Error read B: %d\n", status);
    }

    printf("Temperature A: %d\n", out_a);
    printf("Temperature B: %d\n", out_b);

    return 0;
}

static const shell_command_t shell_commands[] = {
    {"provision", "Full provisioning sequence for EC Module board [A|B]", cmd_provision     },
    {"measure",   "Performs a full measurement",                          cmd_do_measurement},
    {"export",    "Exports the currently loaded configuration",           cmd_config_export },
    {"switch",    "Switches the current active probe",                    cmd_switch_probe  },
    {"set_k",     "Sets the K-Value for a probe",                         cmd_set_k         },
    {"save",      "Saves the configuration to memory",                    cmd_save          },
    {"factory",   "Clears all configuration and calibrations",            cmd_factory_reset },
    {"ec_cmd",    "Debugging: send command to EZOEC module",              cmd_ec_cmd        },
    {"boost",     "Enable or disable the 5V booster",                     cmd_boost         },
    {"temp",      "Get temperature",                                      cmd_temp          },
    {NULL,        NULL,                                                   NULL              },
};

int main_shell(void) {
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
    static msg_t clear_msg = {.type = MSG_CLEAR_BOOT_MAGIC};
    ztimer_set_msg(ZTIMER_MSEC, &clear_timer, 500, &clear_msg, main_thread_pid);

    return 0;
}

// ==================================
// Main routine
// ==================================

#define PIN_TEST GPIO_PIN(PORT_A, 15)
static msg_t _msg_queue[8] = {0};
int main(void) {
    // gpio_init(PIN_TEST, GPIO_OUT);
    // gpio_set(PIN_TEST);

    gpio_init(PRB_SEL_PIN, GPIO_OUT);
    gpio_init(BOOST_EN_PIN, GPIO_IN);

    main_thread_pid = thread_getpid();
    msg_init_queue(_msg_queue, 8);

    // Setup I2C with master.
    mfm_comm_init(&mfm_comm, mfm_comm_params);

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
            uint8_t error_flags       = ERR_NONE;

            sensors_enable();
            ztimer_sleep(ZTIMER_MSEC, 1000);

            int result = sensors_init();
            if (result < 0) {
                DEBUG("ERR(%d) sensors init\n", result);
                sensors_disable();
                mfm_comm_measurement_error(&mfm_comm, ERR_SENSOR_INIT);
                break;
            }

            if (config_has_calibration(PROBE_A)) {
                result = ds18_trigger(&t1);
                if (result < 0) {
                    DEBUG("ERR(%d) trigger temp A\n", result);
                    error_flags |= ERR_TEMP_A_TRIGGER;
                }
            }
            if (config_has_calibration(PROBE_B)) {
                result = ds18_trigger(&t2);
                if (result < 0) {
                    DEBUG("ERR(%d) trigger temp B\n", result);
                    error_flags |= ERR_TEMP_B_TRIGGER;
                }
            }
            ztimer_now_t t_start = ztimer_now(ZTIMER_MSEC);

            if (config_has_calibration(PROBE_A)) {
                result = sensors_get_conductivity(PROBE_A, &measurement.conductivity_a);
                if (result < 0) {
                    DEBUG("ERR(%d) conduc A\n", result);
                    measurement.conductivity_a = 0;
                    error_flags |= ERR_CONDUCTIVITY_A;
                }
            }
            if (config_has_calibration(PROBE_B)) {
                result = sensors_get_conductivity(PROBE_B, &measurement.conductivity_b);
                if (result < 0) {
                    DEBUG("ERR(%d) conduc B\n", result);
                    measurement.conductivity_b = 0;
                    error_flags |= ERR_CONDUCTIVITY_B;
                }
            }

            ztimer_acquire(ZTIMER_MSEC);
            ztimer_periodic_wakeup(ZTIMER_MSEC, &t_start, 750);
            ztimer_release(ZTIMER_MSEC);
            if (config_has_calibration(PROBE_A)) {
                result = ds18_read(&t1, &measurement.temperature_a);
                if (result < 0) {
                    DEBUG("ERR(%d) get temp A\n", result);
                    measurement.temperature_a = 0;
                    error_flags |= ERR_TEMP_A_READ;
                }
            }
            if (config_has_calibration(PROBE_B)) {
                result = ds18_read(&t2, &measurement.temperature_b);
                if (result < 0) {
                    DEBUG("ERR(%d) get temp B\n", result);
                    measurement.temperature_b = 0;
                    error_flags |= ERR_TEMP_B_READ;
                }
            }
            sensors_disable();

            // Report any errors.
            if (error_flags != ERR_NONE) {
                printf("ERR: %02X\n", error_flags);
                mfm_comm_measurement_error(&mfm_comm, error_flags);
            }

            wire_measurement.conductivity_a = measurement.conductivity_a;
            wire_measurement.conductivity_b = measurement.conductivity_b;
            wire_measurement.temperature_a  = measurement.temperature_a;
            wire_measurement.temperature_b  = measurement.temperature_b;

            mfm_comm_measurement_finish(&mfm_comm, (void *)&wire_measurement, sizeof(wire_measurement));
        } break;
        case MSG_CLEAR_BOOT_MAGIC:
            PWR->CR |= PWR_CR_DBP;
            RTC->BKP0R = 0;
            break;
        }
    }
    return 0;
}
