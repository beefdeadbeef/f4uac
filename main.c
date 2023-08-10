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

volatile cs_t cstate = {
	.on[muted] = true,
	.on[spmuted] = true,
	.attn = 6,
	.rate = SAMPLE_RATE_48000
};

void disp();
void pump(page_t);
void pwm();
void pwm_enable();
void uac_notify();
void usbd(void);

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
	default:
		clk = rcc_hse_custom;
	}

	if (clk == clock) return;

	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSICLK);

	rcc_osc_off(RCC_PLL);
	while(rcc_is_osc_ready(RCC_PLL));
	rcc_clock_setup_pll(clk);
	clock = clk;
}

void speaker()
{
	if (e.state == STATE_RUNNING && !cstate.on[spmuted]) {
		gpio_clear(GPIOB, GPIO12);
		if (cstate.on[boost])
			gpio_clear(GPIOA, GPIO15);
		else
			gpio_set(GPIOA, GPIO15);
	} else {
		gpio_set(GPIOA, GPIO15);
		gpio_set(GPIOB, GPIO12);
	}
}

void exti3_isr(void)
{
	bool sp;

	exti_reset_request(EXTI3);
	/* jsense active high */
	sp = gpio_get(GPIOA, GPIO3) != 0;
	if (cstate.on[spmuted] != sp) {
		cstate.on[spmuted] = sp;
		cstate.on[boost] = !sp;
		uac_notify();
	}
	speaker();
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
	rcc_periph_clock_enable(RCC_DMA2);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_SPI4);
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_TIM3);
	rcc_periph_clock_enable(RCC_TIM4);
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
	gpio_set_mux(AFIO_GMUX_SWJ_NO_JTAG);
	gpio_set_mux(AFIO_GMUX_TIM3_B4);
	gpio_set_mux(AFIO_GMUX_SPI4_B6);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
		      GPIO_CNF_INPUT_FLOAT,
		      GPIO3);				/* JSENSE */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO8|GPIO9|GPIO10|		/* TIM1 CH[1:3] */
		      GPIO11|GPIO12);			/* USBFS */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL,
		      GPIO15);				/* PWMEN1 */

	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL,
		      GPIO6|				/* DISP_RST */
		      GPIO8|				/* DISP_DC */
		      GPIO12);				/* PWMEN */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO4|GPIO5|			/* TIM3 CH[1:2] */
		      GPIO7|				/* SPI4 CLK */
		      GPIO9|				/* SPI4 MOSI */
		      GPIO13|GPIO14|GPIO15);		/* TIM1 CH[1:3]N */

	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL,
		      GPIO13);				/* PC13 LED */

	gpio_set(GPIOA, GPIO0|GPIO1|GPIO2|GPIO3|GPIO15);
	gpio_set(GPIOB, GPIO6|GPIO8|GPIO12);

	cstate.on[spmuted] = (gpio_get(GPIOA, GPIO3) != 0);
	cstate.on[boost] = !cstate.on[spmuted];
/*
 *	jack sense
 */
	exti_select_source(EXTI3, GPIOA);
	exti_set_trigger(EXTI3, EXTI_TRIGGER_BOTH);
	exti_enable_request(EXTI3);
	nvic_enable_irq(NVIC_EXTI3_IRQ);
/*
 *
 */
	disp();
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
