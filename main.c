/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>

#include "common.h"

const struct rcc_clock_scale rcc_hse_25mhz_custom = {
	.pllm = 25,
	.plln = 384,
	.pllp = 4,
	.pllq = 8,
	.pllr = 0,
	.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
	.hpre = RCC_CFGR_HPRE_NODIV,
	.ppre1 = RCC_CFGR_PPRE_DIV2,
	.ppre2 = RCC_CFGR_PPRE_NODIV,
	.voltage_scale = PWR_SCALE1,
	.flash_config = FLASH_ACR_DCEN|FLASH_ACR_ICEN|FLASH_ACR_PRFTEN|FLASH_ACR_LATENCY_2WS,
	.ahb_frequency  = 96000000,
	.apb1_frequency = 48000000,
	.apb2_frequency = 96000000,
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
void select_table(sample_table table);

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
		case 'a':
			select_table(SAMPLE_TABLE_S1);
			break;
		case 's':
			select_table(SAMPLE_TABLE_S2);
			break;
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

int main() {

	uint32_t wake;

/*
 * clocks
 */
	rcc_clock_setup_pll(&rcc_hse_25mhz_custom);
	rcc_periph_clock_enable(RCC_DMA2);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_SPI1);
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_clock_enable(RCC_USART1);
/*
 * systick
 */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(PLLO / 1000);
	systick_interrupt_enable();
	systick_counter_enable();
/*
 * gpios
 */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			GPIO0|GPIO1|GPIO4);			/* DEBUG */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE,
			GPIO5|GPIO7|GPIO8|GPIO9|GPIO11|GPIO12);
	gpio_set_af(GPIOA, GPIO_AF5, GPIO5|GPIO7);		/* SPI1 CLK/MOSI */
	gpio_set_af(GPIOA, GPIO_AF1, GPIO8|GPIO9);		/* TIM1 CH1/2 */
	gpio_set_af(GPIOA, GPIO_AF10, GPIO11|GPIO12);           /* USB_FS */

	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE,
			GPIO6|GPIO7|GPIO13|GPIO14);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD,
				GPIO_OSPEED_2MHZ, GPIO12);
	gpio_set(GPIOB, GPIO12);				/* PWMENA */
	gpio_set_af(GPIOB, GPIO_AF7, GPIO6|GPIO7);              /* USART1 TX/RX */
	gpio_set_af(GPIOB, GPIO_AF1, GPIO13|GPIO14);		/* TIM1 CH1N/2N */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE, GPIO13);		/* PC13 LED */
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

heartbeat:

	wake = systicks + 500;
	gpio_toggle(GPIOC, GPIO13);

mainloop:
	__asm("wfe");

	if (e.seen) goto mainloop;
	e.seen = true;

	switch (e.state) {

	case STATE_CLOSED:
		break;

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
		break;
	};

	if (wake > systicks)
		goto mainloop;
	else
		goto heartbeat;

	return 0;
}
