#include "config.h"
#include "periph/eeprom.h"
#include <string.h>

eeprom_config_t eeprom_config = {0};

int config_has_calibration(void) {
    return (eeprom_config.flags & CFG_FLAG_CALIBRATED) > 0;
}

int config_init(void) {
    eeprom_read(0, &eeprom_config, sizeof(eeprom_config));
    if (strcmp(eeprom_config.magic, CFG_MAGIC_HEADER) != 0) {
        puts("!!! NOTICE: corrupted config, resetting");
        memset(&eeprom_config, 0x00, sizeof(eeprom_config));
        memcpy(&eeprom_config.magic, CFG_MAGIC_HEADER, sizeof(CFG_MAGIC_HEADER));
    }
    if (!config_has_calibration()) {
        puts("!!! NOTICE: device has no calibration values");
    }
    return 0;
}

int config_persist(void) {
    return eeprom_write(0, &eeprom_config, sizeof(eeprom_config));
}
