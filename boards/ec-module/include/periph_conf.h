#ifndef PERIPH_CONF_H
#define PERIPH_CONF_H

#include "board.h"
#include "clk_conf.h"
#include "periph/cpu_uart.h"
#include "periph_cpu.h"
#include "stm32l010x6.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name   Timer configuration
 * @{
 */
static const timer_conf_t timer_config[] = {{.dev = TIM2,
                                             .max = 0x0000ffff,
                                             .rcc_mask = RCC_APB1ENR_TIM2EN,
                                             .bus = APB1,
                                             .irqn = TIM2_IRQn}};

#define TIMER_0_ISR isr_tim2

#define TIMER_NUMOF ARRAY_SIZE(timer_config)
/** @} */

/**
 * @name    UART configuration
 * @{
 */
static const uart_conf_t uart_config[] = {
    {
        .dev = USART2,
        .rcc_mask = RCC_APB1ENR_USART2EN,
        .rx_pin = GPIO_PIN(PORT_A, 10),
        .tx_pin = GPIO_PIN(PORT_A, 9),
        .rx_af = GPIO_AF4,
        .tx_af = GPIO_AF4,
        .bus = APB1,
        .irqn = USART2_IRQn,
        .type = STM32_USART,
        .clk_src = 0, /* Use APB clock */
    },
    {
        .dev = LPUART1,
        .rcc_mask = RCC_APB1ENR_LPUART1EN,
        .rx_pin = LPUART_RX_PIN,
        .tx_pin = LPUART_TX_PIN,
        .rx_af = GPIO_AF6,
        .tx_af = GPIO_AF6,
        .bus = APB1,
        .irqn = LPUART1_IRQn,
        .type = STM32_LPUART,
        .clk_src = 0, /* Use APB clock */
    },
};

#define UART_0_ISR (isr_usart2)
#define UART_1_ISR (isr_lpuart1)

#define UART_NUMOF ARRAY_SIZE(uart_config)
/** @} */

/**
 * @name    I2C configuration
 * @{
 */
static const i2c_conf_t i2c_config[] = {
    {
        .dev = I2C1,
        .mode = I2C_MODE_SLAVE,
        .slave_addr = 0x11,
        .speed = I2C_SPEED_NORMAL,
        .scl_pin = GPIO_PIN(PORT_B, 6),
        .sda_pin = GPIO_PIN(PORT_B, 7),
        .scl_af = GPIO_AF1,
        .sda_af = GPIO_AF1,
        .bus = APB1,
        .rcc_mask = RCC_APB1ENR_I2C1EN,
        .irqn = I2C1_IRQn,
    },
};

#define I2C_0_ISR (isr_i2c1)

#define I2C_NUMOF ARRAY_SIZE(i2c_config)
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_CONF_H */
/** @} */
