#ifndef EZOEC_H
#define EZOEC_H

#include "mutex.h"
#include "tsrb.h"
#include <stdint.h>
#include <sys/types.h>

#define RX_MAX_LINE_LEN 42
#define RX_BUFFER_SIZE 128
#define EZOEC_CALIBRATION_LINE_LENGTH 12
#define EZOEC_CALIBRATION_MAX_LINES 10

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int uart;
    unsigned int baud_rate;
} ezoec_params_t;

typedef struct {
    ezoec_params_t params;
    uint8_t rx_buffer[RX_BUFFER_SIZE];
    tsrb_t rx_ringbuffer;
    pid_t rx_thread;
    mutex_t readline_lock;
} ezoec_t;

typedef struct {
    char line[EZOEC_CALIBRATION_MAX_LINES][EZOEC_CALIBRATION_LINE_LENGTH];
} ezoec_calibration_t;

int ezoec_init(ezoec_t *ec, const ezoec_params_t *params);
int ezoec_measure(ezoec_t *ec, uint32_t *out_nS);
int ezoec_set_baud(ezoec_t *ec, unsigned int baud);
int ezoec_factory(ezoec_t *ec);
int ezoec_set_k(ezoec_t *ec, uint8_t k_value);
int ezoec_is_calibrated(ezoec_t *ec);
int ezoec_cal_dry(ezoec_t *ec);
int ezoec_cal_low(ezoec_t *ec, uint32_t uS);
int ezoec_cal_high(ezoec_t *ec, uint32_t uS);
int ezoec_cal_import(ezoec_t *ec, ezoec_calibration_t *cal);
int ezoec_cal_export(ezoec_t *ec, ezoec_calibration_t *cal);

// Exposed private functions for "powerusers"
int ezoec_writeline(ezoec_t *ec, const char *format, ...);
int ezoec_readline(ezoec_t *ec, char *buf, uint8_t buf_len, uint32_t timeout);
int ezoec_assert_ok(ezoec_t *ec);
int ezoec_cmd(ezoec_t *ec, uint32_t timeout, char *out, const char *format,
              ...);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* end of include guard: EZOEC_H */
