#ifndef CONFIG_H
#define CONFIG_H

#include "ezoec.h"
#include <stdint.h>

#define CFG_FLAG_CALIBRATED (1 << 0)
#define CFG_MAGIC_HEADER "MFM01"

typedef struct {
    char magic[6];
    uint8_t flags;
    ezoec_calibration_t calibration[2];
    uint8_t k_values[2];
} eeprom_config_t;
extern eeprom_config_t eeprom_config;


#ifdef __cplusplus
extern "C" {
#endif

int config_init(void);
int config_persist(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* end of include guard: CONFIG_H */
