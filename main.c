/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/crs.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include "common.h"

const struct rcc_clock_scale rcc_hse_custom = {
	.hse_xtpre = RCC_CFGR_PLLXTPRE_HSE_CLK_PREDIV,
	.pll_mul = RCC_CFGR_PLLRANGE_HIGH | RCC_CFGR_PLLMUL_PLL_CLK_MUL48,
	.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
	.hpre = RCC_CFGR_HPRE_NODIV,
	.ppre1 = RCC_CFGR_PPRE_DIV2,
	.ppre2 = RCC_CFGR_PPRE_DIV2,
	.ahb_frequency  = 192000000,
	.apb1_frequency = 96000000,
	.apb2_frequency = 96000000
};

volatile uint32_t systicks;
volatile ev_t e = {
	.seen = true,
	.state = STATE_CLOSED
};

void pwm();
void pwm_enable();
void usbd(void);
void pump(frame_type frame);

void sys_tick_handler(void)
{
        systicks++;
}

void usart1_isr(void)
{
	uint8_t data;

	if (((USART_CR1(USART1) & USART_CR1_RXNEIE) != 0) &&
	    ((USART_SR(USART1) & USART_SR_RXNE) != 0)) {
		data = usart_recv(USART1);
		usart_send_blocking(USART1, data);

		switch (data) {
		case 'q':
			e.state = STATE_DRAIN;
			break;
		case 'w':
			e.state = STATE_FILL;
			e.seen = false;
			break;
		default:
			return;
		}
        }
}

static void poll()
{
	gpio_toggle(GPIOC, GPIO13);
}

int main() {

	uint32_t wake;

/*
 * clocks
 */
	rcc_clock_setup_pll(&rcc_hse_custom);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRS);
	rcc_periph_clock_enable(RCC_DMA1);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_USART1);
	rcc_set_hsi_div(RCC_CFGR3_HSIDIV_NODIV);
	rcc_set_hsi_sclk(RCC_CFGR5_HSI_SCLK_HSIDIV);
	rcc_set_usb_clock_source(RCC_HSI);
	rcc_periph_clock_enable(RCC_USB);
	rcc_usb_alt_pma_enable();
	rcc_usb_alt_isr_enable();
	crs_autotrim_usb_enable();
/*
 * systick
 */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(rcc_ahb_frequency / 1000);
	systick_interrupt_enable();
	systick_counter_enable();
/*
 * gpios
 */
	gpio_set_mux(AFIO_GMUX_TIM1_A12_B12);	/* CH1/2:A8/9 CHN1/2 B13/14 */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO0|GPIO1);	/* DEBUG */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO8|GPIO9|GPIO11|GPIO12);

	gpio_set_mux(AFIO_GMUX_USART1_B6);			/* TX:RX B[6:7] */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO6|GPIO7|GPIO13|GPIO14);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);	/* PWMEN */

	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);	/* PC13 LED */

	gpio_set(GPIOB, GPIO12);
/*
 * usart
 */
	usart_set_baudrate(USART1, 921600);
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_STOPBITS_1);
	usart_set_mode(USART1, USART_MODE_TX_RX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

	nvic_enable_irq(NVIC_USART1_IRQ);
	usart_enable_rx_interrupt(USART1);
	usart_enable(USART1);
/*
 *
 */
	pwm();
	usbd();

mainloop:
	wake = systicks + 500;

sleep:
	__asm("wfe");

	if (e.seen) goto poll;
	e.seen = true;

	switch (e.state) {

	case STATE_FILL:
		pump(BUSY_FRAME);
		pump(FREE_FRAME);
		e.state = STATE_RUNNING;
		pwm_enable();
		break;

	case STATE_RUNNING:
		pump(FREE_FRAME);
		break;

	case STATE_DRAIN:
		e.state = STATE_CLOSED;

	case STATE_CLOSED:
		break;
	};

poll:
	if (wake > systicks)
		goto sleep;

	poll();
	goto mainloop;

	return 0;
}
