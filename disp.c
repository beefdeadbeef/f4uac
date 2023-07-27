/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2023 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include <string.h>
#include "common.h"
#include "icons.h"

extern volatile ev_t e;
extern volatile cs_t cstate;

#define DISP_X		128
#define DISP_Y		64
#define DISP_PAGE_NUM	(DISP_Y / 8)
#define DISP_PAGE_SIZE	(DISP_X)
#define ICONLEN		128	/* 32x32 px */

#define DISPNUM		2
#define REFRESH_HZ	30
#define REFRESH_DIV_PRE	1024
#define REFRESH_DIV	(REFRESH_DIV_PRE * REFRESH_HZ * DISPNUM * DISP_PAGE_NUM)

static const icon iconrow[] = {
	[muted]		= icon_volume_mute,
	[spmuted]	= icon_headphones_box,
	[boost]		= icon_subwoofer,
	[usb]		= icon_usb
};

#define NICONS (sizeof(iconrow)/sizeof(iconrow[0]))

static const uint8_t sh1106_init[] = {
	0xae,                   /* set display off */
	0xd5, 0x80,             /* set osc frequency */
	0xa8, 0x3f,             /* set MUX ratio */
	0xd3, 0,                /* set display offset */
	0x40| 0,                /* set display start line */
	0xa0| 0,                /* set segment remap (1) */
	0xc8,                   /* set reversed COM output scan direction */
	0xda, 0x12,             /* set COM pins hw configuration */
	0xd9, 0xf1,             /* set pre-charge period */
	0xdb, 0x40,             /* set VCOMH deselect level */
	0xaf	                /* set display on */
};

static uint8_t dispbuf[DISP_PAGE_SIZE] __attribute__((aligned(4)));

/*
 *
 */
static void disp_init()
{
	gpio_clear(GPIOA, GPIO6);			/* RST */
	for (unsigned i=0; i<128; i++);
	gpio_set(GPIOA, GPIO6);
	gpio_clear(GPIOA, GPIO4);			/* D/C */
	for (unsigned i=0; i < sizeof(sh1106_init); i++)
		spi_send(SPI1, sh1106_init[i]);
	while (!(SPI_SR(SPI1) & SPI_SR_TXE));
	while (SPI_SR(SPI1) & SPI_SR_BSY);
	gpio_set(GPIOA, GPIO4);				/* D/C */
}

static void disp_select_page(uint8_t i)
{
	gpio_clear(GPIOA, GPIO4);			/* D/C */
	spi_send(SPI1, 0xb0 | (i&7));
	spi_send(SPI1, 0x02);
	spi_send(SPI1, 0x10);
	while (!(SPI_SR(SPI1) & SPI_SR_TXE));
	while (SPI_SR(SPI1) & SPI_SR_BSY);
	gpio_set(GPIOA, GPIO4);				/* D/C */
}

void dma2_channel1_isr()
{
	dma_clear_interrupt_flags(DMA2, DMA_CHANNEL1, DMA_TCIF);
	spi_disable_tx_dma(SPI1);
	dma_disable_channel(DMA2, DMA_CHANNEL1);
}

void tim1_up_isr()
{
	unsigned page = timer_get_counter(TIM9);
	timer_clear_flag(TIM10, TIM_SR_UIF);
	disp_select_page(page);
	bzero(dispbuf, sizeof(dispbuf));
	switch (page) {
	case 0 ... 3:
	{
		uint8_t * dst = dispbuf;
		for (unsigned i=0; i<NICONS; i++) {
			const char *src = icons[iconrow[i]].p + page;
			if (cstate.on[i]) {
				for (unsigned j=0; j<(ICONLEN/4); j++) {
					*dst++ = *src;
					src += 4;
				}
			} else {
				dst += ICONLEN/4;
			}
		}
		break;
	}
	case 4:
	{
		uint8_t c = 0xfe;
		unsigned u = 4 * DISP_PAGE_SIZE * cstate.rms[1];
		for (unsigned i=0; i<MIN(DISP_PAGE_SIZE, u); i+=2)
			dispbuf[i] = c;
		break;
	}
	case 5:
	{
		uint8_t c = 0x60;
		unsigned u = DISP_PAGE_SIZE - 2 * cstate.attn;
		for (unsigned i=0; i<u; i+=2)
			dispbuf[i] = c;
		break;
	}
	case 6:
	{
		uint8_t c = 0x06;
		unsigned u = DISP_PAGE_SIZE - 2 * cstate.attn;
		for (unsigned i=0; i<u; i+=2)
			dispbuf[i] = c;
		break;
	}
	case 7:
	{
		uint8_t c = 0x7f;
		unsigned u = 4 * DISP_PAGE_SIZE * cstate.rms[0];
		for (unsigned i=0; i<MIN(DISP_PAGE_SIZE, u); i+=2)
			dispbuf[i] = c;
		break;
	}
	default:
		break;
	}
	dma_set_memory_address(DMA2, DMA_CHANNEL1, (uint32_t)dispbuf);
	dma_set_number_of_data(DMA2, DMA_CHANNEL1, sizeof(dispbuf));
	dma_enable_channel(DMA2, DMA_CHANNEL1);
	spi_enable_tx_dma(SPI1);
}

void disp()
{
	rcc_periph_reset_pulse(RST_SPI1);
	spi_init_master(SPI1,
                        SPI_CR1_BAUDRATE_FPCLK_DIV_64,
                        SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
                        SPI_CR1_CPHA_CLK_TRANSITION_1,
                        SPI_CR1_DFF_8BIT,
                        SPI_CR1_MSBFIRST);

	spi_set_bidirectional_transmit_only_mode(SPI1);
	spi_enable_software_slave_management(SPI1);
	spi_set_nss_high(SPI1);
	spi_enable(SPI1);

	disp_init();

	dma_enable_flex_mode(DMA2);
	dma_channel_reset(DMA2, DMA_CHANNEL1);
	dma_set_channel_request(DMA2, DMA_CHANNEL1, DMA_REQ_SPI1_TX);
	dma_set_priority(DMA2, DMA_CHANNEL1, DMA_CCR_PL_LOW);
	dma_set_memory_size(DMA2, DMA_CHANNEL1, DMA_CCR_MSIZE_8BIT);
	dma_set_peripheral_size(DMA2, DMA_CHANNEL1, DMA_CCR_PSIZE_8BIT);
	dma_enable_memory_increment_mode(DMA2, DMA_CHANNEL1);
	dma_set_read_from_memory(DMA2, DMA_CHANNEL1);
	dma_set_peripheral_address(DMA2, DMA_CHANNEL1, (uint32_t)&SPI1_DR);
	dma_enable_transfer_complete_interrupt(DMA2, DMA_CHANNEL1);
	nvic_enable_irq(NVIC_DMA2_CHANNEL1_IRQ);

	nvic_enable_irq(NVIC_TIM1_UP_IRQ);
	timer_enable_irq(TIM10, TIM_DIER_UIE);
	timer_set_prescaler(TIM10, REFRESH_DIV_PRE - 1);
	timer_set_period(TIM10, rcc_ahb_frequency / REFRESH_DIV);
	timer_set_oc_value(TIM10, TIM_OC1, 3);
	timer_set_oc_mode(TIM10, TIM_OC1, TIM_OCM_PWM1);
	timer_enable_oc_output(TIM10, TIM_OC1);
	timer_enable_preload(TIM10);

	timer_slave_set_mode(TIM9, TIM_SMCR_SMS_ECM1);
	timer_slave_set_trigger(TIM9, TIM_SMCR_TS_ITR2);	/* TIM10_OC */
	timer_set_period(TIM9, DISPNUM * DISP_PAGE_NUM - 1);
	timer_set_oc_mode(TIM9, TIM_OC1, TIM_OCM_PWM1);
	timer_set_oc_mode(TIM9, TIM_OC2, TIM_OCM_PWM1);
	timer_set_oc_polarity_low(TIM9, TIM_OC1);
	timer_set_oc_value(TIM9, TIM_OC1, DISP_PAGE_NUM);
	timer_set_oc_value(TIM9, TIM_OC2, DISP_PAGE_NUM);
	timer_enable_oc_output(TIM9, TIM_OC1);
	timer_enable_oc_output(TIM9, TIM_OC2);
	timer_enable_preload(TIM9);

	timer_enable_counter(TIM9);
	timer_enable_counter(TIM10);
}
