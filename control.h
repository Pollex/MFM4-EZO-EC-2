#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

enum {
    STATUS_NOT_READY = 0,
    STATUS_BUSY = 1,
    STATUS_READY = 0x0A,
};

enum {
    TASK_UNKNOWN,
    TASK_SENSOR_INIT,
    TASK_MEASUREMENT,
    TASK_CLEAR_BOOT_MAGIC,
};

typedef struct {
    uint32_t conductivity_a;
    uint32_t conductivity_b;
    int16_t temperature_b;
    int16_t temperature_a;
} measurement_t;

#endif /* end of include guard: CONTROL_H */
