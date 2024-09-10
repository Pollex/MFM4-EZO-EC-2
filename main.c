#include "board.h"
#include "periph/cpu_gpio.h"
#include "periph/gpio.h"
#include "periph/uart.h"
#include "ringbuffer.h"
#include <shell.h>
#include <stdint.h>
#include <stdio.h>
#include <ztimer.h>

char ec_buf[256] = {0};
ringbuffer_t ec_ringbuf = RINGBUFFER_INIT(ec_buf);

static void on_ezoec_rx(void *arg, uint8_t data) {
    (void)arg;
    ringbuffer_add_one(&ec_ringbuf, data);
}

static const shell_command_t shell_commands[] = {{NULL, NULL, NULL}};

int main(void) {
    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s CPU.\n", RIOT_CPU);

    // Initialize UART
    uart_init(UART_DEV(1), 9600, on_ezoec_rx, NULL);

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
