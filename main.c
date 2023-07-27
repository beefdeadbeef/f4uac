/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/crs.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "common.h"

const struct rcc_clock_scale rcc_hse_custom[] = {
	{ /* 61=>47656.25 */
		.hse_xtpre = RCC_CFGR_PLLXTPRE_HSE_CLK_PREDIV,
		.pll_mul = RCC_CFGR_PLLRANGE_HIGH | RCC_CFGR_PLLMUL_PLL_CLK_MUL61,
		.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
		.hpre = RCC_CFGR_HPRE_NODIV,
		.ppre1 = RCC_CFGR_PPRE_DIV2,
		.ppre2 = RCC_CFGR_PPRE_DIV2,
		.ahb_frequency  = 244000000,
		.apb1_frequency = 122000000,
		.apb2_frequency = 122000000
	},
	{ /* 56=>43750 57=>44531.25 */
		.hse_xtpre = RCC_CFGR_PLLXTPRE_HSE_CLK_PREDIV,
		.pll_mul = RCC_CFGR_PLLRANGE_HIGH | RCC_CFGR_PLLMUL_PLL_CLK_MUL56,
		.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
		.hpre = RCC_CFGR_HPRE_NODIV,
		.ppre1 = RCC_CFGR_PPRE_DIV2,
		.ppre2 = RCC_CFGR_PPRE_DIV2,
		.ahb_frequency  = 224000000,
		.apb1_frequency = 112000000,
		.apb2_frequency = 112000000
	}
};

static const struct rcc_clock_scale *clock = rcc_hse_custom;

volatile uint32_t systicks;
volatile ev_t e = {
	.seen = true,
	.state = STATE_CLOSED
};

void pwm();
void pwm_enable();
void usbd(void);
void pump(page_t);

void sys_tick_handler(void)
{
        systicks++;
}

void pll_setup(sample_rate rate)
{
	const struct rcc_clock_scale *clk;

	debugf("rate=%d\n", rate);

	switch (rate) {
	case SAMPLE_RATE_44100:
	case SAMPLE_RATE_88200:
		clk = &rcc_hse_custom[1];
		break;
	case SAMPLE_RATE_48000:
	case SAMPLE_RATE_96000:
		clk = rcc_hse_custom;
	}

	if (clk == clock) return;

	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSICLK);

	rcc_osc_off(RCC_PLL);
	while(rcc_is_osc_ready(RCC_PLL));
	rcc_clock_setup_pll(clk);
	clock = clk;
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
	rcc_clock_setup_pll(clock);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRS);
	rcc_periph_clock_enable(RCC_DMA1);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_TIM1);
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
	gpio_set_mux(AFIO_GMUX_TIM1_A12_B12);	/* CH1-3:A[8:10] CHN1-3 B[13:15] */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO0|GPIO1);	/* DEBUG */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO8|GPIO9|GPIO10|GPIO11|GPIO12);

	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO13|GPIO14|GPIO15);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);	/* PWMEN */

	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);	/* PC13 LED */

	gpio_set(GPIOB, GPIO12);
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
		pump(BUSY_PAGE);
		pump(FREE_PAGE);
		e.state = STATE_RUNNING;
		pwm_enable();
		break;

	case STATE_RUNNING:
		pump(FREE_PAGE);
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
