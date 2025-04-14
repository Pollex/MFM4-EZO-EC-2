#include "config.h"
#include "control.h"
#include "modules/ezoec/include/ezoec.h"
#include "periph/uart.h"
#include "sensors.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ztimer.h>

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

static int switch_probe(uint8_t index) {
    gpio_write(PRB_SEL_PIN, index);
    return 0;
}

#define STABLE_READING_SAMPLES 12
static int wait_for_stable_readings(uint32_t timeout, uint32_t tolerance_uS) {
    uint32_t readings[STABLE_READING_SAMPLES] = {0};
    uint8_t total_readings = 0;

    uint32_t start = ztimer_now(ZTIMER_MSEC);
    int result = 0;
    for (;;) {
        //
        // Check if we've reached timeout limit
        if (ztimer_now(ZTIMER_MSEC) - start > timeout) {
            return -ETIMEDOUT;
        }

        //
        // Get a new measurement
        result = ezoec_measure(
            &ec, &readings[total_readings % STABLE_READING_SAMPLES]);
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
        uint8_t sample_count = total_readings < STABLE_READING_SAMPLES
                                   ? total_readings
                                   : STABLE_READING_SAMPLES;
        uint32_t mean = 0;
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

        printf("Mean: %ld\tVariance²: %6ld\nMax variance² allowed: %6ld\n",
               mean, variance, tolerance_uS * tolerance_uS);

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
    char *ptr_end = ptr_start; // will be incremented during reading
    char *ptr = ptr_start;

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
    int32_t value = 0;
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

#define CAL_TOLERANCE_DRY_uS 1000
#define CAL_TOLERANCE_LOW_uS 1000
#define CAL_TOLERANCE_HIGH_uS 1000
int cmd_provision(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("1. Fixing EZOEC configuration");
    ezoec_params_t params = {
        .baud_rate = 9600,
        .uart = UART_DEV(1),
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

retry_ka:
    puts("2. K-Value of probe A: ");
    int k_value = _read_kvalue();
    if (k_value < 0) {
        printf("error(%d), try again\n", k_value);
        goto retry_ka;
    }
    eeprom_config.k_values[0] = k_value;
    printf("Got %s\n", _int_to_string(k_value, 1, NULL));

retry_kb:
    puts("3. K-Value of probe B: ");
    k_value = _read_kvalue();
    if (k_value < 0) {
        printf("error(%d), try again\n", k_value);
        goto retry_kb;
    }
    eeprom_config.k_values[1] = k_value;
    printf("Got %s\n", _int_to_string(k_value, 1, NULL));

    for (int probe = 0; probe < 2; probe++) {
        printf("Switching to probe: %c (K: %s)\n", probe == 0 ? 'A' : 'B',
               _int_to_string(eeprom_config.k_values[probe], 1, NULL));
        switch_probe(probe);
        result = ezoec_set_k(&ec, eeprom_config.k_values[probe]);
        if (result < 0) {
            printf("There are issues setting the K value (error %d).\n",
                   result);
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
        printf("5%c.1: Low calibration. Put probe in low mS solution.\n",
               probe == 0 ? 'A' : 'B');
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
        printf("6%c.1: High calibration. Put probe in high uS solution.\n",
               probe == 0 ? 'A' : 'B');
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

        printf("7%c.1: Calibration done, exporting calibration values...",
               probe == 0 ? 'A' : 'B');
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
    int result = 0;
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

    char buf[20 * 4 + 1] = {0};
    printf("===========================================\n"
           "           Conductivity        Temperature \n"
           "PROBE A    %s uS                %s C       \n"
           "PROBE B    %s uS                %s C       \n",
           _int_to_string(measurement.conductivity_a, 3, buf),
           _int_to_string(measurement.temperature_a, 2, buf + 20),
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
    printf("K-Value: %s\nCalibration: ",
           _int_to_string(eeprom_config.k_values[PROBE_A], 1, NULL));
    _print_ezoec_calibration(&eeprom_config.calibration[PROBE_A]);
    puts("\n\n============== PROBE B ====================");
    printf("K-Value: %s\nCalibration: ",
           _int_to_string(eeprom_config.k_values[PROBE_B], 1, NULL));
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
        printf(
            "Usage: %s <A/B> <K_Value>\nThe K value can have up to 1 decimal\n",
            argv[0]);
        return 0;
    }

    return 0;
}

int cmd_factory_reset(int argc, char **argv) {
    (void)argc;
    (void)argv;

    config_clear();
    puts("Config cleared, use the `save` command to save the empty config");

    return 0;
}
