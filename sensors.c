#include "sensors.h"
#include "board.h"
#include "config.h"
#include "ds18.h"
#include "ezoec.h"
#include "periph/gpio.h"
#include "ztimer.h"
#include <stdint.h>

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

int sensors_trigger_temperatures(void) {
    int result = ds18_trigger(&t1);
    if (result < 0) {
        return result;
    }
    result = ds18_trigger(&t2);
    if (result < 0) {
        return result;
    }
    return 0;
}
int sensors_get_temperatures(int16_t *probeA, int16_t *probeB) {
    int result = ds18_read(&t1, probeA);
    if (result < 0) {
        return result;
    }
    result = ds18_read(&t2, probeB);
    if (result < 0) {
        return result;
    }
    return 0;
}

int sensors_get_conductivity(probe_t probe, uint32_t *out) {
    // Set probe K
    int result = ezoec_set_k(&ec, eeprom_config.k_values[probe]);
    if (result < 0) {
        return result;
    }
    // Load calibration into ezoec
    result = ezoec_cal_import(&ec, &eeprom_config.calibration[probe]);
    if (result < 0) {
        return result;
    }
    ztimer_sleep(ZTIMER_MSEC, 1000);
    result = ezoec_measure(&ec, out);
    if (result < 0) {
        return result;
    }
    return 0;
}
