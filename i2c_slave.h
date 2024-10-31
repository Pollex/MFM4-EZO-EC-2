#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int i2c_slave_init(uint8_t addr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* end of include guard: I2C_SLAVE_H */
