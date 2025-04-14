#include "sensors.h"
#include "board.h"
#include "config.h"
#include "ds18.h"
#include "ezoec.h"
#include "periph/gpio.h"
#include "ztimer.h"
#include <stdint.h>
#include <stdio.h>

ezoec_t ec = {0};
ds18_t t1 = {0};
ds18_t t2 = {0};

int sensors_init(void) {
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
    return 0;
}

void sensors_enable(void) { gpio_set(BOOST_EN_PIN); }
void sensors_disable(void) { gpio_clear(BOOST_EN_PIN); }

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
        printf("Warning: probe %c has no K value set\n",
               probe == PROBE_A ? 'A' : 'B');
    }

    // Load calibration into ezoec
    if (config_has_calibration(probe)) {
        result = ezoec_cal_import(&ec, &eeprom_config.calibration[probe]);
        if (result < 0) {
            return result;
        }
        ztimer_sleep(ZTIMER_MSEC, 1000);
        ezoec_measure(&ec, out);
    } else {
        printf("Warning: probe %c has no calibration\n",
               probe == PROBE_A ? 'A' : 'B');
    }

    result = ezoec_measure(&ec, out);
    if (result < 0) {
        return result;
    }
    return 0;
}
