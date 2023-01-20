/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

#include "common.h"

#define PWM_DEADTIME 4
#define DMABUFSZ (NCHANNELS * NFRAMES)

static uint32_t frames[2 * DMABUFSZ];

void *pframe(frame_type frame)
{
	return (frame == dma_get_target(DMA2, DMA_STREAM5)) ?
		frames: &frames[DMABUFSZ];
}

static void timer_tim1_setup_ocs(enum tim_oc_id oc, enum tim_oc_id ocn)
{
	timer_disable_oc_output(TIM1, oc);
	timer_disable_oc_output(TIM1, ocn);

	timer_disable_oc_clear(TIM1, oc);
	timer_enable_oc_preload(TIM1, oc);
	timer_set_oc_slow_mode(TIM1, oc);
	timer_set_oc_mode(TIM1, oc, TIM_OCM_PWM1);

	timer_set_oc_polarity_high(TIM1, oc);
	timer_set_oc_idle_state_set(TIM1, oc);

	timer_set_oc_polarity_high(TIM1, ocn);
	timer_set_oc_idle_state_set(TIM1, ocn);

	timer_enable_oc_output(TIM1, oc);
	timer_enable_oc_output(TIM1, ocn);
}

void pwm()
{
	dma_stream_reset(DMA2, DMA_STREAM5);
	dma_channel_select(DMA2, DMA_STREAM5, DMA_SxCR_CHSEL_6);
	dma_set_priority(DMA2, DMA_STREAM5, DMA_SxCR_PL_HIGH);
	dma_set_memory_size(DMA2, DMA_STREAM5, DMA_SxCR_MSIZE_32BIT);
	dma_set_peripheral_size(DMA2, DMA_STREAM5, DMA_SxCR_PSIZE_32BIT);
	dma_enable_memory_increment_mode(DMA2, DMA_STREAM5);
	dma_enable_double_buffer_mode(DMA2, DMA_STREAM5);
	dma_set_transfer_mode(DMA2, DMA_STREAM5, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
	dma_set_number_of_data(DMA2, DMA_STREAM5, DMABUFSZ);
	dma_set_peripheral_address(DMA2, DMA_STREAM5, (uint32_t)&TIM_DMAR(TIM1));
	dma_set_memory_address(DMA2, DMA_STREAM5, (uint32_t)frames);
	dma_set_memory_address_1(DMA2, DMA_STREAM5, (uint32_t)&frames[DMABUFSZ]);
	dma_enable_transfer_complete_interrupt(DMA2, DMA_STREAM5);
	dma_enable_stream(DMA2, DMA_STREAM5);
	nvic_enable_irq(NVIC_DMA2_STREAM5_IRQ);
	timer_set_period(TIM1, PWM_PERIOD);
	timer_set_deadtime(TIM1, PWM_DEADTIME);
	timer_set_enabled_off_state_in_idle_mode(TIM1);
	timer_set_enabled_off_state_in_run_mode(TIM1);
	timer_disable_break(TIM1);
	timer_set_break_polarity_high(TIM1);
	timer_disable_break_automatic_output(TIM1);
	timer_set_break_lock(TIM1, TIM_BDTR_LOCK_OFF);
	timer_tim1_setup_ocs(TIM_OC1, TIM_OC1N);
	timer_tim1_setup_ocs(TIM_OC2, TIM_OC2N);
	timer_enable_preload(TIM1);
	/* to:CCR1/2: len: 2  off: 0x34>>4=0xd */
	TIM_DCR(TIM1) |= 1<<8 | 0xd;
	timer_generate_event(TIM1, TIM_EGR_UG);
	timer_enable_irq(TIM1, TIM_DIER_UDE);
	timer_enable_break_main_output(TIM1);
}

void pwm_enable(void)
{
	timer_enable_counter(TIM1);
	gpio_clear(GPIOB, GPIO12);
}

static void pwm_disable(void)
{
	gpio_set(GPIOB, GPIO12);
	timer_disable_counter(TIM1);
}

extern volatile ev_t e;

void dma2_stream5_isr(void)
{
	if (!dma_get_interrupt_flag(DMA2, DMA_STREAM5, DMA_TCIF)) return;
	dma_clear_interrupt_flags(DMA2, DMA_STREAM5, DMA_TCIF);

	switch (e.state) {
	case STATE_RUNNING:
		e.seen = false;
		break;
	case STATE_DRAIN:
		pwm_disable();
		e.seen = false;
	default:
		break;
	};
}
