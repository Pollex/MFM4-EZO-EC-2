#ifndef CONTROL_H
#define CONTROL_H

#include "sched.h"
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
};

typedef struct {
    uint32_t conductivity_a;
    uint32_t conductivity_b;
    int16_t temperature_b;
    int16_t temperature_a;
} measurement_t;

extern measurement_t measurement;
extern uint8_t do_measurement;
extern uint8_t measurement_status;
extern uint8_t do_initialize;
extern uint8_t initialize_status;
extern kernel_pid_t main_thread_pid;

#endif /* end of include guard: CONTROL_H */
