#ifndef EZOEC_H
#define EZOEC_H

#include "ringbuffer.h"
#include <stdint.h>

#define RX_MAX_LINE_LEN 42
#define RX_BUFFER_SIZE RX_MAX_LINE_LEN*3

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart;
    uint8_t k_value;
    unsigned int baud_rate;
} ezoec_params_t;

typedef struct {
    ezoec_params_t params;
    char rx_buffer[RX_BUFFER_SIZE];
    ringbuffer_t rx_ringbuffer;
} ezoec_t;

int ezoec_init(ezoec_t *ec, const ezoec_params_t *params);
int ezoec_read(ezoec_t *ec, int temperature, uint32_t *out);
int ezoec_set_baud(ezoec_t *ec, unsigned int baud);
int ezoec_is_calibrated(ezoec_t *ec);
int ezoec_cal_dry(ezoec_t *ec);
int ezoec_cal_low(ezoec_t *ec, uint32_t uS);
int ezoec_cal_high(ezoec_t *ec, uint32_t uS);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* end of include guard: EZOEC_H */
