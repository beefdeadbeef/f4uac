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
#include "tables.h"
#include "font.h"

extern volatile ev_t e;
extern volatile cs_t cstate;

extern void speaker();
extern void uac_notify(uint8_t);
#define DISP_X		128
#define DISP_Y		64
#define DISP_PAGE_NUM	(DISP_Y / 8)
#define DISP_PAGE_SIZE	(DISP_X)
#define SICONSZ		24	/* 24x24 px */
#define LICONSZ		40	/* 40x40 px */

#define DISPNUM		2
#define REFRESH_HZ	30
#define REFRESH_DIV_PRE	1024
#define REFRESH_DIV	(REFRESH_DIV_PRE * REFRESH_HZ * DISPNUM * DISP_PAGE_NUM)

static uint8_t dispbuf[DISP_PAGE_SIZE] __attribute__((aligned(4)));

/*
 *
 */
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

static void disp_init()
{
	gpio_clear(GPIOB, GPIO6);			/* RST */
	for (unsigned i=0; i<128; i++);
	gpio_set(GPIOB, GPIO6);
	gpio_clear(GPIOB, GPIO8);			/* D/C */
	for (unsigned i=0; i < sizeof(sh1106_init); i++)
		spi_send(SPI4, sh1106_init[i]);
	while (!(SPI_SR(SPI4) & SPI_SR_TXE));
	while (SPI_SR(SPI4) & SPI_SR_BSY);
	gpio_set(GPIOB, GPIO8);				/* D/C */
}

static void disp_select_page(uint8_t i)
{
	gpio_clear(GPIOB, GPIO8);			/* D/C */
	spi_send(SPI4, 0xb0 | (i&7));
	spi_send(SPI4, 0x02);
	spi_send(SPI4, 0x10);
	while (!(SPI_SR(SPI4) & SPI_SR_TXE));
	while (SPI_SR(SPI4) & SPI_SR_BSY);
	gpio_set(GPIOB, GPIO8);				/* D/C */
}
/*
 *
 */
static const icon iconrow[] = {
	[spmuted]	= icon_headphones_box,
	[boost]		= icon_subwoofer,
	[muted]		= icon_volume_mute,
	[sine]		= icon_sine_wave,
	[usb]		= icon_usb
};

#define NICONS (sizeof(iconrow)/sizeof(iconrow[0]))

#define BAR_START	4
#define BAR_LEN		(DISP_PAGE_SIZE - 2 * BAR_START)

static unsigned f_to_barlen(float f)
{
	return MIN((unsigned)(BAR_LEN * f), BAR_LEN - 1);
}

static void disp_draw_bar(uint8_t *dst, uint8_t c, float f)
{
	unsigned len = f_to_barlen(f) / 2;
	uint16_t *p = (uint16_t *)dst;
	while (len--) *p++ = (uint16_t)c << 8;
}

static void disp_draw_icon(uint8_t *dst, icon ico, uint16_t page)
{
	const char *src = icons[ico].p + page;
	unsigned iconsz = icons[ico].h;
	for (unsigned i=0; i<iconsz; i++) {
		*dst++ = *src;
		src += iconsz / 8;
	}
}

static void disp_draw_char(uint8_t *dst, uint16_t c, uint16_t page)
{
	const uint8_t *src = font_bits + font_height * c + font_width * (1 - page);
	memcpy(dst, src, font_width);
}

static void disp_draw_string(uint8_t *dst, const char *s, uint16_t page)
{
	while (*s) {
		disp_draw_char(dst, *s++, page);
		dst += font_width;
	}
}

static const char * const vol_strings[] = {
	" 0 ", "-1 ", "-2 ", "-3 ", "-4 ", "-5 ", "-6 ", "-7 ",
	"-8 ", "-9 ", "-10", "-11", "-12", "-13", "-14", "-15",
	"-16", "-17", "-18", "-19", "-20", "-21", "-22", "-23",
	"-24", "-25", "-26", "-27", "-28", "-29", "-30", "-31",
	"-32", "-33", "-34", "-35", "-36", "-37", "-38", "-39",
	"-40", "-41", "-42", "-43", "-44", "-45", "-46", "-47",
	"-48", "-49", "-50", "-51", "-52", "-53", "-54", "-55",
	"-56", "-57", "-58", "-59", "-60"
};

static const char * const fmt_strings[] = {
	[SAMPLE_FORMAT_NONE]	= "-:-",
	[SAMPLE_FORMAT_S16]	= "S16",
	[SAMPLE_FORMAT_S24]	= "S24",
	[SAMPLE_FORMAT_S32]	= "S32",
	[SAMPLE_FORMAT_F32]	= "F32"
};

static const char * rate_strings(sample_rate rate)
{
	const struct {
		sample_rate rate;
		const char *s;
	} strings[] = {
		{ .rate = SAMPLE_RATE_44100, .s = "44k" },
		{ .rate = SAMPLE_RATE_48000, .s = "48k" },
		{ .rate = SAMPLE_RATE_88200, .s = "88k" },
		{ .rate = SAMPLE_RATE_96000, .s = "96k" },
		{ .rate = SAMPLE_RATE_NONE,  .s = "-:-" }
	}, *rs = strings;

	while (rs->rate && rs->rate != rate) rs++;
	return rs->s;
}

static void disp_fill_page(unsigned page)
{
	disp_select_page(page);

	bzero(dispbuf, sizeof(dispbuf));

	switch (page) {
	case 0 ... 2:
		for (unsigned i=0; i<NICONS; i++)
			if (cstate.on[i])
				disp_draw_icon(dispbuf + i * (SICONSZ + 2),
					       iconrow[i], page);
		break;
	case 3 ... 7:
		disp_draw_icon(dispbuf + LICONSZ,
			       e.state == STATE_RUNNING ? icon_play : icon_pause,
			       page - 3);
		if (page > 5) {
			disp_draw_string(dispbuf + 4, "dB:", page - 6);
			disp_draw_string(dispbuf + 100,
					 rate_strings(cstate.rate), page - 6);
		}
		else if (page > 3) {
			disp_draw_string(dispbuf + 4,
					 vol_strings[cstate.attn], page - 4);
			disp_draw_string(dispbuf + 100,
					 fmt_strings[cstate.format], page - 4);
		}
		break;
	case 9:
		disp_draw_bar(dispbuf + BAR_START, 0xff, 4 * cstate.rms[1]);
		dispbuf[BAR_START + f_to_barlen(4 * cstate.peak[1])] = 0x55;
		break;
	case 10:
		disp_draw_bar(dispbuf + BAR_START, 0x7f, 4 * cstate.rms[1]);
		dispbuf[BAR_START + f_to_barlen(4 * cstate.peak[1])] = 0x55;
		break;
	case 11:
		disp_draw_bar(dispbuf + BAR_START, 0x70, scale[cstate.attn]);
		break;
	case 12:
		disp_draw_bar(dispbuf + BAR_START, 0x0e, scale[cstate.attn]);
		break;
	case 13:
		disp_draw_bar(dispbuf + BAR_START, 0xfe, 4 * cstate.rms[0]);
		dispbuf[BAR_START + f_to_barlen(4 * cstate.peak[0])] = 0xaa;
		break;
	case 14:
		disp_draw_bar(dispbuf + BAR_START, 0xff, 4 * cstate.rms[0]);
		dispbuf[BAR_START + f_to_barlen(4 * cstate.peak[0])] = 0xaa;
	default:
		break;
	}

	switch (page) {
	case 0:
	case 8:
		for (unsigned i=0; i<DISP_PAGE_SIZE; i++)
			dispbuf[i] |= 0x01;
		break;
	case 7:
	case 15:
		for (unsigned i=0; i<DISP_PAGE_SIZE; i++)
			dispbuf[i] |= 0x80;
	default:
		break;
	}

	dispbuf[0] = dispbuf[DISP_PAGE_SIZE - 1] = 0xff;
}

#define DBCNT 12
#define NBTNS 2
static void disp_poll_buttons(unsigned now)
{
	static uint8_t counter[NBTNS] = { DBCNT, DBCNT };
	static unsigned last, bits = (1<<NBTNS) - 1;
	unsigned i, mask, ready, toggled = bits ^ now;

	for (i=0, ready=0, mask=1; i<NBTNS; i++, mask<<=1) {
		if (toggled & mask) {
			counter[i] = DBCNT;
		} else {
			if (counter[i] == 0) {
				ready |= mask;
			} else {
				counter[i]--;
			}
		}
	}

	bits ^= toggled;
	toggled = last ^ ready;
	last = ready;
	ready &= toggled;

	if (!ready) return;

	if ((i = (2 & ready))) {
		bool sp = i & bits;
		if (cstate.on[spmuted] != sp) {
			cstate.on[spmuted] = sp;
			cstate.on[boost] = !sp;
			uac_notify(UAC_FU_SPEAKER_ID);
		}
		speaker();
	}
}

void dma2_channel1_isr()
{
	dma_clear_interrupt_flags(DMA2, DMA_CHANNEL1, DMA_TCIF);
	spi_disable_tx_dma(SPI4);
	dma_disable_channel(DMA2, DMA_CHANNEL1);
}

void tim4_isr()
{
	timer_clear_flag(TIM4, TIM_SR_UIF);
	disp_poll_buttons(gpio_get(GPIOA, GPIO2|GPIO3) >> 2);
	disp_fill_page(timer_get_counter(TIM3));
	dma_set_memory_address(DMA2, DMA_CHANNEL1, (uint32_t)dispbuf);
	dma_set_number_of_data(DMA2, DMA_CHANNEL1, sizeof(dispbuf));
	dma_enable_channel(DMA2, DMA_CHANNEL1);
	spi_enable_tx_dma(SPI4);
}

void disp()
{
	rcc_periph_reset_pulse(RST_SPI4);
	spi_init_master(SPI4,
                        SPI_CR1_BAUDRATE_FPCLK_DIV_64,
                        SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
                        SPI_CR1_CPHA_CLK_TRANSITION_1,
                        SPI_CR1_DFF_8BIT,
                        SPI_CR1_MSBFIRST);

	spi_set_bidirectional_transmit_only_mode(SPI4);
	spi_enable_software_slave_management(SPI4);
	spi_set_nss_high(SPI4);
	spi_enable(SPI4);

	disp_init();

	dma_enable_flex_mode(DMA2);
	dma_channel_reset(DMA2, DMA_CHANNEL1);
	dma_set_channel_request(DMA2, DMA_CHANNEL1, DMA_REQ_SPI4_TX);
	dma_set_priority(DMA2, DMA_CHANNEL1, DMA_CCR_PL_LOW);
	dma_set_memory_size(DMA2, DMA_CHANNEL1, DMA_CCR_MSIZE_8BIT);
	dma_set_peripheral_size(DMA2, DMA_CHANNEL1, DMA_CCR_PSIZE_8BIT);
	dma_enable_memory_increment_mode(DMA2, DMA_CHANNEL1);
	dma_set_read_from_memory(DMA2, DMA_CHANNEL1);
	dma_set_peripheral_address(DMA2, DMA_CHANNEL1, (uint32_t)&SPI_DR(SPI4));
	dma_enable_transfer_complete_interrupt(DMA2, DMA_CHANNEL1);
	nvic_enable_irq(NVIC_DMA2_CHANNEL1_IRQ);

	nvic_enable_irq(NVIC_TIM4_IRQ);
	timer_enable_irq(TIM4, TIM_DIER_UIE);
	timer_set_prescaler(TIM4, REFRESH_DIV_PRE - 1);
	timer_set_period(TIM4, rcc_ahb_frequency / REFRESH_DIV);
	timer_set_master_mode(TIM4, TIM_CR2_MMS_COMPARE_OC1REF);
	timer_set_oc_value(TIM4, TIM_OC1, 3);
	timer_set_oc_mode(TIM4, TIM_OC1, TIM_OCM_PWM1);
	timer_enable_oc_output(TIM4, TIM_OC1);
	timer_enable_preload(TIM4);

	timer_slave_set_mode(TIM3, TIM_SMCR_SMS_ECM1);
	timer_slave_set_trigger(TIM3, TIM_SMCR_TS_ITR3);	/* TIM4 */
	timer_set_period(TIM3, DISPNUM * DISP_PAGE_NUM - 1);
	timer_set_oc_mode(TIM3, TIM_OC1, TIM_OCM_PWM1);
	timer_set_oc_mode(TIM3, TIM_OC2, TIM_OCM_PWM1);
	timer_set_oc_polarity_low(TIM3, TIM_OC1);
	timer_set_oc_value(TIM3, TIM_OC1, DISP_PAGE_NUM);
	timer_set_oc_value(TIM3, TIM_OC2, DISP_PAGE_NUM);
	timer_enable_oc_output(TIM3, TIM_OC1);
	timer_enable_oc_output(TIM3, TIM_OC2);
	timer_enable_preload(TIM3);

	timer_enable_counter(TIM3);
	timer_enable_counter(TIM4);
}
