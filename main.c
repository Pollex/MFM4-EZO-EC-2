#include "board.h"
#include "config.h"
#include "control.h"
#include "i2c_slave.h"
#include "msg.h"
#include "periph/cpu_gpio.h"
#include "periph/gpio.h"
#include "sched.h"
#include "sensors.h"
#include "shell.h"
#include "shell_commands.h"
#include "stm32l010x6.h"
#include "thread.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <ztimer.h>

measurement_t measurement = {0};
uint8_t do_measurement = 0;
uint8_t measurement_status = STATUS_NOT_READY;
uint8_t do_initialize = 0;
uint8_t initialize_status = STATUS_NOT_READY;
kernel_pid_t main_thread_pid = 0;

static const shell_command_t shell_commands[] = {
    {"provision", "Full provisioning sequence for EC Module board",
     cmd_provision},
    {"measurement", "Performs a full measurement", cmd_do_measurement},
    {"ec_cmd", "Debugging: send command to EZOEC module", cmd_ec_cmd},
    {NULL, NULL, NULL},
};

void slot_id_init(void) {
    gpio_init(MOD_ID1_PIN, GPIO_IN);
    gpio_init(MOD_ID2_PIN, GPIO_IN);
    gpio_init(MOD_ID3_PIN, GPIO_IN);
}

int slot_id(void) {
    int id = 0;
    if (gpio_read(MOD_ID1_PIN) > 0)
        id += 1;
    if (gpio_read(MOD_ID2_PIN) > 0)
        id += 2;
    if (gpio_read(MOD_ID3_PIN) > 0)
        id += 4;
    return id;
}

#define SHELL_MAGIC 0x24C0FFEE
int main(void) {
    // Reset by button occured
    // Check if we need to enter shell mode
    if ((RCC->CSR & RCC_CSR_PINRSTF_Msk) > 0) {
        puts("Ext rst");
        if (RTC->BKP0R == SHELL_MAGIC) {
            puts("Magic");
            // clear magic to bkp0r
            PWR->CR |= PWR_CR_DBP;
            RTC->BKP0R = 0;
            goto enter_shell;
        } else {
            puts("Nomagic");
            // write magic to bkp0r
            PWR->CR |= PWR_CR_DBP;
            RTC->BKP0R = SHELL_MAGIC;
            ztimer_sleep(ZTIMER_MSEC, 500);
            RTC->BKP0R = 0;
        }
    }

    main_thread_pid = thread_getpid();

    gpio_init(PRB_SEL_PIN, GPIO_OUT);
    gpio_init(BOOST_EN_PIN, GPIO_OUT);
    slot_id_init();
    config_init();
    uint8_t id = slot_id();
    if (id == 0)
        id = 1;
    i2c_slave_init(0x10 + slot_id());

    msg_t msg = {0};
    for (;;) {
        if (do_initialize) {
            puts("Should init now...");
            do_initialize = 0;
        } else if (do_measurement) {
            puts("Should measure now...");
            measurement_status = STATUS_BUSY;

            sensors_enable();
            int result = sensors_init();
            if (result < 0) {
                printf("shit init");
            }
            result = sensors_trigger_temperatures();
            if (result < 0) {
                printf("shit trigger");
            }
            result =
                sensors_get_conductivity(PROBE_A, &measurement.conductivity_a);
            if (result < 0) {
                printf("shit conduc A");
            }
            result =
                sensors_get_conductivity(PROBE_B, &measurement.conductivity_b);
            if (result < 0) {
                printf("shit conduc B");
            }
            result = sensors_get_temperatures(&measurement.temperature_a,
                                              &measurement.temperature_b);
            if (result < 0) {
                printf("shit get temps");
            }
            sensors_disable();

            measurement_status = STATUS_READY;
            do_measurement = 0;
        } else {
            msg_receive(&msg);
        }
    }
    return 0;

enter_shell:
    sensors_init();
    char line_buf[SHELL_DEFAULT_BUFSIZE * 2];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE * 2);

    return 0;
}
