#include "modules/ezoec/include/ezoec.h"
#include "periph/uart.h"
#include "ringbuffer.h"
#include "shell.h"
#include <stdint.h>
#include <stdio.h>
#include <ztimer.h>

int cmd_ec_read(int argc, char **argv);
int cmd_ec_rb(int argc, char **argv);

static const shell_command_t shell_commands[] = {
    {"ec_read", "", cmd_ec_read},
    {"ec_rb", "", cmd_ec_rb},
    {NULL, NULL, NULL},
};

ezoec_t ec = {0};
ezoec_params_t ec_params = {
    .baud_rate = 115200,
    .uart = UART_DEV(1),
    .k_value = 1,
};

int main(void) {
    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s CPU.\n", RIOT_CPU);

    ezoec_init(&ec, &ec_params);

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}

//
int cmd_ec_read(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t out = 0;
    int result = ezoec_read(&ec, 250, &out);
    if (result < 0)
        printf("Error: %d\n", result);
    else
        printf("Read: %ld\n", out);
    return 0;
}

int cmd_ec_rb(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char buf[RX_BUFFER_SIZE] = {0};
    int len = ringbuffer_get(&ec.rx_ringbuffer, buf, RX_BUFFER_SIZE);
    printf("Got %d in rb: %s\n", len, buf);
    return 0;
}
