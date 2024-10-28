#include "board.h"
#include "ds18.h"
#include "modules/ezoec/include/ezoec.h"
#include "periph/cpu_gpio.h"
#include "periph/eeprom.h"
#include "periph/gpio.h"
#include "periph/uart.h"
#include "ringbuffer.h"
#include "shell.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ztimer.h>

#define USE_PER_PROBE_CALIBRATION 0

int cmd_ec_read(int argc, char **argv);
int cmd_ec_stable(int argc, char **argv);
int cmd_data_persist(int argc, char **argv);
int cmd_data_import(int argc, char **argv);
int cmd_data_export(int argc, char **argv);
int cmd_data_k_value(int argc, char **argv);
int cmd_ec_export(int argc, char **argv);
int cmd_ec_import(int argc, char **argv);
int cmd_ec_cmd(int argc, char **argv);
int cmd_ec_rb(int argc, char **argv);
int cmd_ec_is_calibrated(int argc, char **argv);
int cmd_ds_read(int argc, char **argv);
int cmd_ec_calibrate(int argc, char **argv);
int cmd_switch_probe(int argc, char **argv);

static const shell_command_t shell_commands[] = {
    {"switch_probe", "", cmd_switch_probe},
    {"ec_calibrate", "", cmd_ec_calibrate},
    {"ec_read", "", cmd_ec_read},
    {"ec_stable", "", cmd_ec_stable},
    {"ds_read", "", cmd_ds_read},
    {"data_persist", "", cmd_data_persist},
    {"data_export", "", cmd_data_export},
    {"data_import", "", cmd_data_import},
    {"data_k_value", "", cmd_data_k_value},
    // Less interesting commands
    {"ec_export", "", cmd_ec_export},
    {"ec_import", "", cmd_ec_import},
    {"ec_is_cal", "", cmd_ec_is_calibrated},
    {"ec_rb", "", cmd_ec_rb},
    {"ec_cmd", "", cmd_ec_cmd},
    {NULL, NULL, NULL},
};

ezoec_t ec = {0};
ezoec_params_t ec_params = {
    .baud_rate = 115200,
    .uart = UART_DEV(1),
    .k_value = 10,
};

ds18_t t1 = {0};
ds18_params_t t1_params = {
    .pin = DQ_A_PIN,
    .out_mode = GPIO_OD_PU,
};
ds18_t t2 = {0};
ds18_params_t t2_params = {
    .pin = DQ_B_PIN,
    .out_mode = GPIO_OD_PU,
};

struct {
    char magic[4];
    ezoec_calibration_t calibration[2];
    uint8_t k_values[2];
} eeprom_data;

#define MAGIC_HEADER "MFM"

int main(void) {
    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s CPU.\n", RIOT_CPU);

    gpio_init(PRB_SEL_PIN, GPIO_OUT);

    eeprom_read(0, &eeprom_data, sizeof(eeprom_data));
    if (strcmp(eeprom_data.magic, MAGIC_HEADER) != 0) {
        puts("!!! NOTICE: this device has no calibration stored");
    }

    int result = ezoec_init(&ec, &ec_params);
    if (result < 0) {
        printf("EZOEC Initialization error: %d\n", result);
    }
    result = ds18_init(&t1, &t1_params);
    if (result < 0) {
        printf("DS18B20 A Initialization error: %d\n", result);
    }
    result = ds18_init(&t2, &t2_params);
    if (result < 0) {
        printf("DS18B20 B Initialization error: %d\n", result);
    }

    char line_buf[SHELL_DEFAULT_BUFSIZE * 2];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE * 2);

    return 0;
}

int switch_probe(uint8_t index) {
    gpio_write(PRB_SEL_PIN, index);
    return 0;
}

//
// Shell commands
//
int cmd_ds_read(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (argc < 1) {
        puts("Usage: ds_read <A|B>");
        return -1;
    }

    ds18_t *t;
    if (argv[1][0] == 'A')
        t = &t1;
    else if (argv[1][0] == 'B')
        t = &t2;
    else {
        puts("Usage: ds_read <A|B>");
        return -1;
    }

    int16_t temp = 0;
    int result = ds18_get_temperature(t, &temp);
    if (result < 0) {
        printf("error triggering ds18b20: %d\n", result);
        return result;
    }

    printf("got: %d\n", temp);

    return 0;
}

int cmd_ec_read(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (argc < 1) {
        puts("Usage: ds_read <A|B>");
        return -1;
    }

    char prb = argv[1][0];
    if (prb != 'A' && prb != 'B') {
        printf("Invalid probe, must be uppercase A or B, got: %c\n", prb);
        return -1;
    }
    int probeIndex = prb == 'A' ? 0 : 1;

    int result = switch_probe(probeIndex);
    if (result < 0) {
        printf("Error switching probe: %d\n", result);
        return result;
    }

#if USE_PER_PROBE_CALIBRATION
    // Import settings if probe differs from last
    static int last_probe_index = -1;
    if (last_probe_index != probeIndex) {
        puts("Importing probe settings");
        result = ezoec_set_k(&ec, eeprom_data.k_values[probeIndex]);
        if (result < 0) {
            printf("Error importing probe calibration: %d\n", result);
            return result;
        }
        result = ezoec_cal_import(&ec, &eeprom_data.calibration[probeIndex]);
        if (result < 0) {
            printf("Error importing probe calibration: %d\n", result);
            return result;
        }
    }
#endif

    uint32_t out = 0;
    result = ezoec_measure(&ec, &out);
    if (result < 0) {
        printf("Error taking measurement: %d\n", result);
        return result;
    }

    printf("Read: %ld\n", out);
    return 0;
}

int cmd_ec_rb(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char buf[RX_BUFFER_SIZE] = {0};
    int len = ringbuffer_get(&ec.rx_ringbuffer, buf, RX_BUFFER_SIZE);
    printf("Got %d in rb: %s\n", len, buf);
    return 0;
}
//

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

int cmd_ec_is_calibrated(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int result = ezoec_is_calibrated(&ec);
    if (result < 0) {
        printf("Error: %d\n", result);
        return result;
    }
    printf("Calibrated with %d points\n", result);
    return 0;
}

int cmd_ec_export(int argc, char **argv) {
    (void)argc;
    (void)argv;

    ezoec_calibration_t calibration = {0};
    int result = ezoec_cal_export(&ec, &calibration);
    if (result < 0) {
        printf("Error: %d\n", result);
        return result;
    }

    printf("Exported %d lines\n", EZOEC_CALIBRATION_MAX_LINES);
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES; line++) {
        fwrite(calibration.line[line], EZOEC_CALIBRATION_LINE_LENGTH, 1,
               stdout);
    }
    putc('\n', stdout);

    return 0;
}

int cmd_ec_import(int argc, char **argv) {
    if (argc < 2) {
        puts(
            "Import a calibration string obtained from 'ec_export' into ezoec");
        printf("Usage: %s <calibration string>\n", argv[0]);
        return -1;
    }
    if (strlen(argv[1]) !=
        EZOEC_CALIBRATION_MAX_LINES * EZOEC_CALIBRATION_LINE_LENGTH) {
        printf("Error: expected calibration string of %d characters\n",
               EZOEC_CALIBRATION_LINE_LENGTH * EZOEC_CALIBRATION_MAX_LINES);
        return -1;
    }

    ezoec_calibration_t calibration = {0};
    char *calibrationString = argv[1];
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES; line++) {
        printf("Importing: %.12s\n", calibrationString);
        memcpy(&calibration, calibrationString, EZOEC_CALIBRATION_LINE_LENGTH);
        calibrationString += EZOEC_CALIBRATION_LINE_LENGTH;
    }

    int result = ezoec_cal_import(&ec, &calibration);
    if (result < 0) {
        printf("Error importing calibration into EZOEC: %d\n", result);
        return result;
    }

    return 0;
};

#define STABLE_READING_SAMPLES 12
int wait_for_stable_readings(uint32_t timeout, uint32_t tolerance) {
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
        total_readings++;

        // Only check std with X samples
        if (total_readings <= STABLE_READING_SAMPLES) {
            printf("Collecting samples: %2d/%2d \t-->\t%ld\n", total_readings,
                   STABLE_READING_SAMPLES,
                   readings[(total_readings - 1) % STABLE_READING_SAMPLES]);
            if (total_readings < STABLE_READING_SAMPLES) {
                continue;
            }
        } else {
            printf("Collected sample: %2d \t-->\t%ld\n", total_readings,
                   readings[(total_readings - 1) % STABLE_READING_SAMPLES]);
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

        printf("Variance²: %6ld\nMax variance allowed: %6ld\n", variance,
               tolerance);

        // Validate
        if (variance < tolerance * tolerance) {
            return 0; // Return 0 on stable readings
        }
    }

    return 0; // This line is unreachable due to the loop
}

#define CAL_TOLERANCE_DRY_uS 100
#define CAL_TOLERANCE_LOW_uS 1000
#define CAL_TOLERANCE_HIGH_uS 1000
int cmd_ec_calibrate(int argc, char **argv) {
    if (argc < 3) {
        puts("This calibration command requires two parameters. The low and "
             "high concentration, in microSiemens, that the probe will be "
             "calibrated agaist.");
        puts("Example that calibrates probe A with Atlas-Scientific "
             "calibration kit values 12.880uS and 80.000uS:");
        printf("\t%s A 12880 80000\n", argv[0]);
        printf("Usage: %s <probe A/B> <low> <high>\n", argv[0]);
        return -1;
    }

    char prb = argv[1][0];
    if (prb != 'A' && prb != 'B') {
        printf("Invalid probe, must be uppercase A or B, got: %c\n", prb);
        return -1;
    }
    int probeIndex = prb == 'A' ? 0 : 1;
    int low = atoi(argv[2]);
    int high = atoi(argv[3]);
    if (low < 0 || high < 0) {
        puts("Invalid low or high given, value cant be negative");
        return -1;
    }

    puts("\n\n================================");
    puts("=======Calibration==============");
    puts("================================");
    printf("1. Switching to probe %c.\n", prb);
    switch_probe(probeIndex);
    puts("1. Starting with a dry calibration, the probe should be dry and not "
         "in a fluid.");
    puts("1. Press enter to start...");
    while (getchar() != '\x0A')
        ;
    puts("1. Waiting for stabilized readings...");
    int result = wait_for_stable_readings(30000, CAL_TOLERANCE_DRY_uS);
    if (result < 0) {
        printf("An error occured during stabilization: %d\n", result);
        return -1;
    }

    result = ezoec_cal_dry(&ec);
    if (result < 0) {
        printf("An error occured during dry calibration: %d\n", result);
        return -1;
    }

    puts("\n====== LOW Calibration =========");
    printf("2. Put the probe in the %d uS solution\n", low);
    puts("2. Press enter to do the low calibration");
    while (getchar() != '\x0A')
        ;
    puts("2. Waiting for stabilized readings...");
    result = wait_for_stable_readings(30000, CAL_TOLERANCE_LOW_uS);
    if (result < 0) {
        printf("An error occured during stabilization: %d\n", result);
        return -1;
    }
    result = ezoec_cal_low(&ec, low);
    if (result < 0) {
        printf("An error occured during low calibration: %d\n", result);
        return -1;
    }

    puts("\n====== HIGH Calibration ========");
    printf("3. Put the probe in the %d uS solution\n", high);
    puts("Waiting...");
    while (getchar() != '\x0A')
        ;
    puts("3. Waiting for stabilized readings...");
    result = wait_for_stable_readings(30000, CAL_TOLERANCE_HIGH_uS);
    if (result < 0) {
        printf("An error occured during stabilization: %d\n", result);
        return -1;
    }
    result = ezoec_cal_high(&ec, high);
    if (result < 0) {
        printf("An error occured during high calibration: %d\n", result);
        return -1;
    }

    puts("\n================================");
    puts("4. Saving calibration values to temporary memory....");
    result = ezoec_cal_export(&ec, &eeprom_data.calibration[probeIndex]);
    if (result < 0) {
        printf("An error occured during export for save: %d\n", result);
        return -1;
    }
    // Save eeprom

    printf("Calibration succesful for probe %c\n", prb);
    puts("After calibration of BOTH probes, run the 'persist_data' command");
    return 0;
}

int cmd_data_k_value(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <probe A/B> <k_value>\n", argv[0]);
        return -1;
    }

    char prb = argv[1][0];
    if (prb != 'A' && prb != 'B') {
        printf("Invalid probe, must be uppercase A or B, got: %c\n", prb);
        return -1;
    }
    int probeIndex = prb == 'A' ? 0 : 1;

    int k = atoi(argv[2]);
    if (k < 0 || k > 255) {
        puts("K value must be between 0.1 and 25.5");
        return -1;
    }

    eeprom_data.k_values[probeIndex] = k;
    return 0;
}

int cmd_data_export(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("Exporting all persisted data");

    puts("\n=========PROBE A=========");
    printf("K Value: %d\n", eeprom_data.k_values[0]);
    puts("Calibration string:");
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES; line++) {
        fwrite(eeprom_data.calibration[0].line[line],
               EZOEC_CALIBRATION_LINE_LENGTH, 1, stdout);
    }
    putc('\n', stdout);

    puts("\n=========PROBE B=========");
    printf("K Value: %d\n", eeprom_data.k_values[1]);
    puts("Calibration string:");
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES; line++) {
        fwrite(eeprom_data.calibration[1].line[line],
               EZOEC_CALIBRATION_LINE_LENGTH, 1, stdout);
    }
    putc('\n', stdout);
    return 0;
}

int cmd_data_import(int argc, char **argv) {
    if (argc < 3) {
        puts("Import a calibration string obtained from 'ec_export' for a "
             "probe");
        printf("Usage: %s <probe A/B> <calibration string>\n", argv[0]);
        return -1;
    }
    char prb = argv[1][0];
    if (prb != 'A' && prb != 'B') {
        printf("Invalid probe, must be uppercase A or B, got: %c\n", prb);
        return -1;
    }
    int probeIndex = prb == 'A' ? 0 : 1;
    if (strlen(argv[2]) !=
        EZOEC_CALIBRATION_MAX_LINES * EZOEC_CALIBRATION_LINE_LENGTH) {
        printf("Error: expected calibration string of %d characters\n",
               EZOEC_CALIBRATION_LINE_LENGTH * EZOEC_CALIBRATION_MAX_LINES);
        return -1;
    }

    printf("Importing for probe %c\n", prb);
    char *calibrationString = argv[2];
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES; line++) {
        printf("Importing: %.12s\n", calibrationString);
        memcpy(eeprom_data.calibration[probeIndex].line[line],
               calibrationString, EZOEC_CALIBRATION_LINE_LENGTH);
        calibrationString += EZOEC_CALIBRATION_LINE_LENGTH;
    }

    return 0;
}

int cmd_data_persist(int argc, char **argv) {
    (void)argc;
    (void)argv;

    memcpy(eeprom_data.magic, MAGIC_HEADER, sizeof(MAGIC_HEADER));

    int result = eeprom_write(0, &eeprom_data, sizeof(eeprom_data));
    if (result < 0) {
        printf("An error occured during saving: %d\n", result);
        return -1;
    }
    puts("Calibration stored");
    return 0;
}

int cmd_switch_probe(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <probe A/B>>\n", argv[0]);
        return -1;
    }
    char prb = argv[1][0];
    if (prb != 'A' && prb != 'B') {
        printf("Invalid probe, must be uppercase A or B, got: %c\n", prb);
        return -1;
    }
    int probeIndex = prb == 'A' ? 0 : 1;

    printf("Switching to probe %c (%d)\n", prb, probeIndex);
    switch_probe(probeIndex);
    return 0;
}

int cmd_ec_stable(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <probe A/B> <tolerance>\n", argv[0]);
        return -1;
    }
    char prb = argv[1][0];
    if (prb != 'A' && prb != 'B') {
        printf("Invalid probe, must be uppercase A or B, got: %c\n", prb);
        return -1;
    }
    int probeIndex = prb == 'A' ? 0 : 1;

    printf("Switching to probe %c (%d)\n", prb, probeIndex);
    switch_probe(probeIndex);

    int tolerance = atoi(argv[2]);
    int result = wait_for_stable_readings(10000, tolerance);
    if (result < 0) {
        printf("An error occured during stabilization: %d\n", result);
        return -1;
    }

    uint32_t out = 0;
    result = ezoec_measure(&ec, &out);
    if (result < 0) {
        printf("Error taking measurement: %d\n", result);
        return result;
    }

    printf("Read: %ld\n", out);
    return 0;
}
