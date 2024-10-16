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

#define ASSERT_OK_TIMEOUT 500
#define TX_MAX_LINE_LEN 42
#define MAX_DECIMALS 3
#define DEV UART_DEV(ec->params.uart)

void _write(ezoec_t *ec, const char *format, ...);
void _clear_rb(ezoec_t *ec);
int assert_ok(ezoec_t *ec);
char *_intd(uint8_t k, uint8_t precision);
int set_k(ezoec_t *ec);
int rx_read_line(ezoec_t *ec, char *buf, uint8_t buf_len, uint32_t timeout);

static void on_ezoec_receive(void *arg, uint8_t data) {
    ezoec_t *ec = (ezoec_t *)arg;
    ringbuffer_add_one(&ec->rx_ringbuffer, data);
}

int ezoec_init(ezoec_t *ec, const ezoec_params_t *params) {
    ec->params = *params;

    int result = uart_init(DEV, ec->params.baud_rate, on_ezoec_receive, ec);
    if (result < 0) {
        DEBUG("[%s]: Could not init uart at %d: %d\n", __func__,
              ec->params.baud_rate, result);
        return result;
    }

    ringbuffer_init(&ec->rx_ringbuffer, ec->rx_buffer, sizeof(ec->rx_buffer));

    // Disable continuous mode
    _clear_rb(ec);
    _write(ec, "C,0\r");
    result = assert_ok(ec);
    if (result < 0) {
        DEBUG("[%s]: Could not disable continuous mode: %d\n", __func__,
              result);
        return result;
    }

    // Set probe value
    result = set_k(ec);
    if (result < 0) {
        DEBUG("[%s]: Could not set k-value: %d\n", __func__, result);
        return result;
    }

    DEBUG("[%s]: Init success\n", __func__);
    return 0;
}

int ezoec_set_baud(ezoec_t *ec, unsigned int baud) {
    _clear_rb(ec);
    _write(ec, "Baud,%d\r", baud);
    return assert_ok(ec);
}

int ezoec_read(ezoec_t *ec, int temperature, uint32_t *out) {
    _clear_rb(ec);
    _write(ec, "RT,%s\r", _intd(temperature, 1));

    // Read measurements result line
    enum { buf_len = 16 };
    char buf[buf_len + 1] = {0};
    int rx_len = rx_read_line(ec, buf, buf_len, 2500);
    // Return directly if there's an error
    if (rx_len < 0)
        return rx_len;

    int result = assert_ok(ec);
    if (result < 0)
        return result;

    // Convert to int
    char *ptr_start = buf;
    char *ptr_end = ptr_start + rx_len;
    char *ptr = ptr_start;

    //
    uint8_t decimals = 0;
    while (ptr < ptr_end) {
        if (*ptr == '.') {
            decimals = ptr_end - ptr - 1;
            ptr++;
            continue;
        }

        *out *= 10;
        *out += (*ptr - 0x30);
        ptr++;
    }
    // Correct for potential missing decimals
    for (int x = MAX_DECIMALS - decimals; x > 0; x--) {
        *out *= 10;
    }

    return result;
}

//

int rx_read_line(ezoec_t *ec, char *buf, uint8_t buf_len, uint32_t timeout) {
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

int assert_ok(ezoec_t *ec) {
    enum { buf_len = 10 };
    char buf[buf_len + 1] = {0};

    int result = rx_read_line(ec, buf, buf_len, ASSERT_OK_TIMEOUT);
    if (result < 0) {
        DEBUG("[%s]: Error reading line: %d\n", __func__, result);
        return result;
    }

    if (strcmp(buf, "*OK") != 0) {
        DEBUG("[%s]: Non OK: '%s'\n", __func__, buf);
        return -1;
    }

    DEBUG("[%s]: OK\n", __func__);
    return 0;
}

int set_k(ezoec_t *ec) {
    uint8_t k = ec->params.k_value;
    DEBUG("[%s]: Setting K: %d\n", __func__, k);
    _clear_rb(ec);
    _write(ec, "K,%s\r", _intd(k, 1));
    return assert_ok(ec);
}

char *_intd(uint8_t k, uint8_t precision) {
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

void _write(ezoec_t *ec, const char *format, ...) {
    char buffer[TX_MAX_LINE_LEN] = {0};
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len < 0 || len >= TX_MAX_LINE_LEN) {
        DEBUG("[%s]: Error formatting command or command too long\n", __func__);
        return;
    }

    uart_write(DEV, (uint8_t *)buffer, len);

#if ENABLE_DEBUG
    // Debug output
    for (int i = 0; i < len; i++) {
        putchar(buffer[i] == '\r' ? '\n' : buffer[i]);
    }
#endif
}

void _clear_rb(ezoec_t *ec) { ec->rx_ringbuffer.avail = 0; }
