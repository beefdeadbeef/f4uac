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

static uint16_t dmabuf[2 * DMABUFSZ] __attribute__((aligned(4)));

#define __DMA DMA1
#define __DMA_STREAM DMA_CHANNEL1
#define __DMA_IRQ NVIC_DMA1_CHANNEL1_IRQ
#define __DMA_PRIO DMA_CCR_PL_VERY_HIGH
#define __DMA_MSIZE DMA_CCR_MSIZE_16BIT
#define __DMA_PSIZE DMA_CCR_PSIZE_16BIT
#define __dma_isr dma1_channel1_isr

static volatile uint32_t dma_target;
#define dma_get_target(x, y) (dma_target)

uint16_t *pframe(page_t page)
{
	return (page == dma_get_target(__DMA, __DMA_STREAM)) ?
		dmabuf: &dmabuf[DMABUFSZ];
}

static void timer_tim1_setup_ocs(enum tim_oc_id oc, enum tim_oc_id ocn)
{
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
	dma_enable_flex_mode(__DMA);
	dma_channel_reset(__DMA, __DMA_STREAM);
	dma_set_channel_request(__DMA, __DMA_STREAM, DMA_REQ_TIM1_UP);
	dma_set_priority(__DMA, __DMA_STREAM, __DMA_PRIO);
	dma_set_memory_size(__DMA, __DMA_STREAM, __DMA_MSIZE);
	dma_set_peripheral_size(__DMA, __DMA_STREAM, __DMA_PSIZE);
	dma_enable_memory_increment_mode(__DMA, __DMA_STREAM);
	dma_enable_circular_mode(__DMA, __DMA_STREAM);
	dma_set_read_from_memory(__DMA, __DMA_STREAM);
	dma_set_number_of_data(__DMA, __DMA_STREAM, 2 * DMABUFSZ);
	dma_set_peripheral_address(__DMA, __DMA_STREAM, (uint32_t)&TIM_DMAR(TIM1));
	dma_set_memory_address(__DMA, __DMA_STREAM, (uint32_t)dmabuf);
	dma_enable_half_transfer_interrupt(__DMA, __DMA_STREAM);
	dma_enable_transfer_complete_interrupt(__DMA, __DMA_STREAM);
	dma_enable_channel(__DMA, __DMA_STREAM);
	nvic_enable_irq(__DMA_IRQ);

	timer_set_period(TIM1, PWM_PERIOD);
	timer_set_prescaler(TIM1, PWM_PRESCALER - 1);
	timer_set_deadtime(TIM1, PWM_DEADTIME);
	timer_set_enabled_off_state_in_idle_mode(TIM1);
	timer_set_enabled_off_state_in_run_mode(TIM1);
	timer_disable_break(TIM1);
	timer_tim1_setup_ocs(TIM_OC1, TIM_OC1N);
	timer_tim1_setup_ocs(TIM_OC2, TIM_OC2N);
	timer_tim1_setup_ocs(TIM_OC3, TIM_OC3N);
	timer_enable_preload(TIM1);
	/* to:CCR1/2/3: len: 3  off: 0x34>>4=0xd */
	TIM_DCR(TIM1) |= 2<<8 | 0xd;
	timer_generate_event(TIM1, TIM_EGR_UG);
	timer_enable_irq(TIM1, TIM_DIER_UDE);
}

void pwm_enable(void)
{
	timer_enable_break_main_output(TIM1);
	timer_enable_counter(TIM1);
	gpio_clear(GPIOB, GPIO12);
}

static void pwm_disable(void)
{
	gpio_set(GPIOB, GPIO12);
	timer_disable_counter(TIM1);
	timer_disable_break_main_output(TIM1);
}

extern volatile ev_t e;

void __dma_isr(void)
{
	dma_target = dma_get_interrupt_flag(__DMA, __DMA_STREAM, DMA_HTIF);
	dma_clear_interrupt_flags(__DMA, __DMA_STREAM, DMA_GIF);
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
