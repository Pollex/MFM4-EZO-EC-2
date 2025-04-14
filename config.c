#include "config.h"
#include "periph/eeprom.h"
#include "sensors.h"
#include <stdint.h>
#include <string.h>

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
    if (!config_has_calibration(PROBE_A)) {
        puts("!!! NOTICE: Probe A is not calibrated");
    }
    if (!config_has_calibration(PROBE_B)) {
        puts("!!! NOTICE: Probe B is not calibrated");
    }
    return 0;
}

int config_clear(void) {
    memset(&eeprom_config, 0x00, sizeof(eeprom_config));
    memcpy(&eeprom_config.magic, CFG_MAGIC_HEADER, sizeof(CFG_MAGIC_HEADER));
    return 0;
}

int config_persist(void) {
    return eeprom_write(0, &eeprom_config, sizeof(eeprom_config));
}
