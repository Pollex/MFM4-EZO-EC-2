#include "include/ezoec.h"
#include "periph/uart.h"
#include "ringbuffer.h"
#include "ztimer.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ENABLE_DEBUG 0
#include "debug.h"

#define ASSERT_OK_TIMEOUT 2500
#define TX_MAX_LINE_LEN 42
#define MAX_DECIMALS 3
#define DEV UART_DEV(ec->params.uart)

void _clear_ringbuffer(ezoec_t *ec);
char *_int_to_string(uint8_t k, uint8_t precision);

enum {
    STATUS_OK,
    STATUS_ERROR,
    STATUS_OVERVOLT,
    STATUS_UNDERVOLT,
    STATUS_RESET,
    STATUS_READY,
    STATUS_SLEEP,
    STATUS_WAKE,
    STATUS_DONE,
} status_t;

#define MAX_STATUS_LEN 6
static int _read_status(ezoec_t *ec, uint32_t timeout) {
    while (!ringbuffer_empty(&ec->rx_ringbuffer) &&
           ringbuffer_peek_one(&ec->rx_ringbuffer) != '*') {
        ringbuffer_get_one(&ec->rx_ringbuffer);
    }
    char status[MAX_STATUS_LEN] = {0};
    int len = ezoec_readline(ec, status, MAX_STATUS_LEN, timeout);
    if (len < 0) {
        return len;
    }
    DEBUG("[%s]: %s\n", __func__, status);
    if (strcmp(status, "*OK") == 0)
        return STATUS_OK;
    if (strcmp(status, "*ER") == 0)
        return STATUS_ERROR;
    if (strcmp(status, "*OV") == 0)
        return STATUS_OVERVOLT;
    if (strcmp(status, "*UV") == 0)
        return STATUS_UNDERVOLT;
    if (strcmp(status, "*RS") == 0)
        return STATUS_RESET;
    if (strcmp(status, "*RE") == 0)
        return STATUS_READY;
    if (strcmp(status, "*SL") == 0)
        return STATUS_SLEEP;
    if (strcmp(status, "*WA") == 0)
        return STATUS_WAKE;
    if (strcmp(status, "*DONE") == 0)
        return STATUS_DONE;
    return -1;
}

static void on_ezoec_receive(void *arg, uint8_t data) {
    ezoec_t *ec = (ezoec_t *)arg;
    ringbuffer_add_one(&ec->rx_ringbuffer, data);
}

int ezoec_init(ezoec_t *ec, const ezoec_params_t *params) {
    ec->params = *params;

    ringbuffer_init(&ec->rx_ringbuffer, ec->rx_buffer, sizeof(ec->rx_buffer));

    int result = uart_init(DEV, ec->params.baud_rate, on_ezoec_receive, ec);
    if (result < 0) {
        DEBUG("[%s]: Could not init uart at %d: %d\n", __func__,
              ec->params.baud_rate, result);
        return result;
    }
    uart_write(DEV, (const uint8_t *)"\r", 1);
    ezoec_assert_ok(ec);

    char version[15] = {0};
    result = ezoec_cmd(ec, 500, version, "i");
    if (result < 0) {
        DEBUG("[%s]: Could not get version: %d\n", __func__, result);
        return result;
    }
    if (result < 5) {
        DEBUG("[%s]: Not enough rx to be version info: %d\n", __func__, result);
        return result;
    }
    if (version[3] != 'E' || version[4] != 'C') {
        DEBUG("[%s]: does not appear to be EC device: %s\n", __func__, version);
        return result;
    }

    DEBUG("[%s]: Init success\n", __func__);
    return 0;
}

int ezoec_set_baud(ezoec_t *ec, unsigned int baud) {
    return ezoec_cmd(ec, 0, NULL, "Baud,%d", baud);
}

int ezoec_factory(ezoec_t *ec) {
    uart_write(DEV, (const uint8_t *)"Factory\r", sizeof("Factory\r"));

    int result = 0;
    // Wait for reset
    result = _read_status(ec, 2000);
    if (result < 0)
        return result;
    if (result != STATUS_OK)
        return -1;
    result = _read_status(ec, 2000);
    if (result < 0)
        return result;
    if (result != STATUS_RESET)
        return -1;
    result = _read_status(ec, 2000);
    if (result < 0)
        return result;
    if (result != STATUS_READY)
        return -1;

    DEBUG("[%s]: EZO ready\n", __func__);
    return 0;
}

int ezoec_set_k(ezoec_t *ec, uint8_t k_value) {
    return ezoec_cmd(ec, 0, NULL, "K,%s", _int_to_string(k_value, 1));
}

int ezoec_measure(ezoec_t *ec, uint32_t *out_nS) {
    char rx[RX_MAX_LINE_LEN] = {0};
    int rx_len = ezoec_cmd(ec, 2000, rx, "R");
    if (rx_len < 0) {
        return rx_len;
    }

    // Convert to int
    char *ptr_start = rx;
    char *ptr_end = ptr_start + rx_len;
    char *ptr = ptr_start;

    //
    *out_nS = 0;
    uint8_t decimals = 0;
    while (ptr < ptr_end) {
        if (*ptr == '.') {
            decimals = ptr_end - ptr - 1;
            ptr++;
            continue;
        }

        *out_nS *= 10;
        *out_nS += (*ptr - 0x30);
        ptr++;
    }
    // Correct for potential missing decimals
    for (int x = MAX_DECIMALS - decimals; x > 0; x--) {
        *out_nS *= 10;
    }

    return 0;
}

int ezoec_is_calibrated(ezoec_t *ec) {
    char rx[RX_MAX_LINE_LEN] = {0};
    int result = ezoec_cmd(ec, 100, rx, "Cal,?");
    if (result < 0) {
        return result;
    }
    return rx[5] - 0x30;
}

int ezoec_cal_dry(ezoec_t *ec) { return ezoec_cmd(ec, 0, NULL, "Cal,dry"); }

int ezoec_cal_low(ezoec_t *ec, uint32_t uS) {
    return ezoec_cmd(ec, 0, NULL, "Cal,low,%d", uS);
}

int ezoec_cal_high(ezoec_t *ec, uint32_t uS) {
    return ezoec_cmd(ec, 0, NULL, "Cal,high,%d", uS);
}

int ezoec_cal_import(ezoec_t *ec, ezoec_calibration_t *cal) {
    int result = 0;
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES; line++) {
        result = ezoec_cmd(ec, 0, NULL, "Import,%.12s", cal->line[line]);
        if (result < 0) {
            DEBUG("[%s]: Error importing: %d\n", __func__, result);
            return result;
        }
    }

    // Wait for reset
    char rxBuffer[13] = {0};
    result = ezoec_readline(ec, rxBuffer, 12, 2000);
    if (result < 0) {
        DEBUG("[%s]: Error waiting for reset: %d\n", __func__, result);
        return result;
    }
    if (strcmp(rxBuffer, "*RS") != 0) {
        DEBUG("[%s]: Expected EZO reset\n", __func__);
        return 0;
    }
    DEBUG("[%s]: EZO resetting\n", __func__);

    result = ezoec_readline(ec, rxBuffer, 12, 2000);
    if (result < 0) {
        DEBUG("[%s]: Error waiting for ready: %d\n", __func__, result);
        return result;
    }
    if (strcmp(rxBuffer, "*RE") != 0) {
        DEBUG("[%s]: Expected EZO ready\n", __func__);
        return 0;
    }
    DEBUG("[%s]: EZO ready\n", __func__);

    return 0;
}

int ezoec_cal_export(ezoec_t *ec, ezoec_calibration_t *cal) {
    int result = 0;
    char rxBuffer[EZOEC_CALIBRATION_LINE_LENGTH + 1] = {0};
    for (int line = 0; line < EZOEC_CALIBRATION_MAX_LINES + 1; line++) {
        result = ezoec_cmd(ec, 1000, rxBuffer, "Export");
        if (result < 0) {
            DEBUG("[%s]: Error reading export: %d\n", __func__, result);
            return result;
        }
        rxBuffer[result] = 0;

        DEBUG("[%s]: Read %d bytes\n", __func__, result);
        if (strcmp(rxBuffer, "*DONE") == 0) {
            DEBUG("[%s]: Done with export\n", __func__);
            return line;
        }

        memcpy(cal->line[line], rxBuffer, result);
    }
    return -1;
}

//
int ezoec_writeline(ezoec_t *ec, const char *format, ...) {
    static char txbuf[TX_MAX_LINE_LEN] = {0};

    va_list args;
    va_start(args, format);
    int len = vsnprintf(txbuf, sizeof(txbuf) - 1, format, args);
    va_end(args);
    txbuf[len++] = '\r';
    _clear_ringbuffer(ec);
    uart_write(DEV, (uint8_t *)txbuf, len);

    return 0;
}

int ezoec_cmd(ezoec_t *ec, uint32_t timeout, char *out, const char *format,
              ...) {
    static char txbuf[TX_MAX_LINE_LEN] = {0};

    va_list args;
    va_start(args, format);
    int len = vsnprintf(txbuf, sizeof(txbuf) - 1, format, args);
    va_end(args);
    txbuf[len++] = '\r';

    _clear_ringbuffer(ec);
    uart_write(DEV, (uint8_t *)txbuf, len);
#if ENABLE_DEBUG
    fwrite(txbuf, len, 1, stdout);
    putc('\n', stdout);
#endif

    // Only if out is given will we read a response
    int rx_len = 0;
    if (out != NULL) {
        rx_len = ezoec_readline(ec, out, RX_MAX_LINE_LEN, timeout);
        if (rx_len < 0)
            return rx_len;
    }

    int result = ezoec_assert_ok(ec);
    if (result < 0)
        return result;

    return rx_len;
}

int ezoec_readline(ezoec_t *ec, char *buf, uint8_t buf_len, uint32_t timeout) {
    ringbuffer_t *rb = &ec->rx_ringbuffer;
    char *ptr = buf;

    ztimer_acquire(ZTIMER_MSEC);
    uint32_t start = ztimer_now(ZTIMER_MSEC);

    int result = 0;
    for (;;) {
        if ((ztimer_now(ZTIMER_MSEC) - start) > timeout) {
            result = -ETIMEDOUT;
            break;
        }

        int data = ringbuffer_get_one(rb);
        if (data < 0) {
            continue;
        }

        if (data == '\r') {
            result = ptr - buf;
#if ENABLE_DEBUG
            fwrite("<<< ", 4, 1, stdout);
            fwrite(buf, result, 1, stdout);
            putc('\n', stdout);
#endif
            break;
        }

        *ptr++ = data;
        if (ptr == buf + buf_len) {
            result = -ENOBUFS;
            break;
        }
    }

    ztimer_release(ZTIMER_MSEC);
    return result;
}

int ezoec_assert_ok(ezoec_t *ec) {
    int result = 0;
read_status:
    result = _read_status(ec, ASSERT_OK_TIMEOUT);
    if (result < 0) {
        DEBUG("[%s]: Error reading status: %d\n", __func__, result);
        return result;
    }
    DEBUG("[%s]: Status: %d\n", __func__, result);
    switch (result) {
    case STATUS_OK:
        return 0;
    case STATUS_ERROR:
        return -1;
    case STATUS_OVERVOLT:
    case STATUS_UNDERVOLT:
    case STATUS_RESET:
    case STATUS_READY:
    case STATUS_SLEEP:
    case STATUS_WAKE:
        goto read_status;
    default:
        return -2;
    }
    return 0;
}

char *_int_to_string(uint8_t k, uint8_t precision) {
    enum { buf_len = 10 };
    static char buf[buf_len + 1] = {0};
    char *ptr = &buf[buf_len - 1];

    int decimals = 0;
    while (k > 0) {
        char decimal = 0x30 + (k % 10);
        k /= 10;

        *ptr-- = decimal;
        if (++decimals == precision) {
            *ptr-- = '.';
            if (k == 0)
                *ptr-- = '0';
        }
    }
    return ptr + 1;
}

void _clear_ringbuffer(ezoec_t *ec) {
    ec->rx_ringbuffer.start = 0;
    ec->rx_ringbuffer.avail = 0;
}
