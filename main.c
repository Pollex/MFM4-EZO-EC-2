#include "board.h"
#include "ds18.h"
#include "modules/ezoec/include/ezoec.h"
#include "periph/cpu_gpio.h"
#include "periph/gpio.h"
#include "periph/uart.h"
#include "ringbuffer.h"
#include "shell.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ztimer.h>

int cmd_ec_read(int argc, char **argv);
int cmd_ec_rb(int argc, char **argv);
int cmd_ds_read(int argc, char **argv);

static const shell_command_t shell_commands[] = {
    {"ec_read", "", cmd_ec_read},
    {"ec_rb", "", cmd_ec_rb},
    {"ds_read", "", cmd_ds_read},
    {NULL, NULL, NULL},
};

ezoec_t ec = {0};
ezoec_params_t ec_params = {
    .baud_rate = 115200,
    .uart = UART_DEV(1),
    .k_value = 1,
};

ds18_t t1 = {0};
ds18_params_t t1_params = {
    .pin = DQ_A_PIN,
    .out_mode = GPIO_OD_PU,
};
ds18_t t2 = {0};
ds18_params_t t2_params = {
    .pin = DQ_B_PIN,
    .out_mode = GPIO_OD_PU,
};

int main(void) {
    printf("You are running RIOT on a(n) %s board.\n", RIOT_BOARD);
    printf("This board features a(n) %s CPU.\n", RIOT_CPU);

    ezoec_init(&ec, &ec_params);
    ds18_init(&t1, &t1_params);
    ds18_init(&t2, &t2_params);

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

int cmd_ds_read(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (argc == 0) {
        puts("Usage: ds_read <A|B>");
        return -1;
    }

    ds18_t *t;
    if (argv[1][0] == 'A')
        t = &t1;
    else if (argv[1][0] == 'B')
        t = &t2;
    else {
        puts("Usage: ds_read <A|B>");
        return -1;
    }

    int16_t temp = 0;
    int result = ds18_get_temperature(t, &temp);
    if (result < 0) {
        printf("error triggering ds18b20: %d\n", result);
        return result;
    }

    printf("got: %d\n", temp);

    return 0;
}
