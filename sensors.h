#ifndef SENSORS_H
#define SENSORS_H

#include "ds18.h"
#include "ezoec.h"
#include "periph/uart.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern ezoec_t ec;
static const ezoec_params_t ec_params = {
    .baud_rate = 115200,
    .uart = UART_DEV(1),
};

extern ds18_t t1;
static const ds18_params_t t1_params = {
    .pin = DQ_A_PIN,
    .out_mode = GPIO_OD_PU,
};
extern ds18_t t2;
static const ds18_params_t t2_params = {
    .pin = DQ_B_PIN,
    .out_mode = GPIO_OD_PU,
};

typedef enum {
    PROBE_A,
    PROBE_B,
} probe_t;

int sensors_init(void);
void sensors_enable(void);
void sensors_disable(void);
int sensors_trigger_temperature(probe_t probe);
int sensors_get_temperature(probe_t probe, int16_t *out);
int sensors_get_conductivity(probe_t probe, uint32_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* end of include guard: SENSORS_H */
