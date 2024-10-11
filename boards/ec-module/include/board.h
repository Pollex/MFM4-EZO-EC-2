#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif


#define CONFIG_ZTIMER_USEC_WIDTH (16)
#define CONFIG_ZTIMER_USEC_ADJUST_SET 20
#define CONFIG_ZTIMER_USEC_ADJUST_SLEEP 27

#define LPUART_TX_PIN GPIO_PIN(PORT_A, 2)
#define LPUART_RX_PIN GPIO_PIN(PORT_A, 3)

#define PRB_SEL_1_PIN GPIO_PIN(PORT_A, 6)
#define DQ_A_PIN GPIO_PIN(PORT_A, 5)
#define DQ_B_PIN GPIO_PIN(PORT_A, 7)

#define BOOST_EN_PIN GPIO_PIN(PORT_B, 12)

#define MOD_ID1_PIN GPIO_PIN(PORT_B, 9)
#define MOD_ID2_PIN GPIO_PIN(PORT_B, 5)
#define MOD_ID3_PIN GPIO_PIN(PORT_B, 8)

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
/** @} */
