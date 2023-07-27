/*  -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <stdint.h>
#include <stdbool.h>
#include "debug.h"

/*
 * guard against unsupported mcu family
 */
#if !defined(AT32F40X)
#error "unsupported MCU family"
#endif

/*
 * number of audio frames after upsampling, must be 2^(4+N)
 */
#define NFRAMES		(1 << 9)

/*
 * pwm width
 */
#define PWM_WIDTH 	7
#define PWM_PERIOD	(1 << PWM_WIDTH)
#define PWM_PRESCALER	5

/*
 * noise shaper order
 */
#define NS_ORDER	4

/*
 * circular buffer size, must be 2^N
 */
#define RINGBUF_SHIFT	12
#define RBSIZE		(1 << RINGBUF_SHIFT)

/*
 * async usb audio feedback
 * frames # per 1ms in Q10.14 format
 */
#define FEEDBACK_SHIFT	14
#define FEEDBACK	(48 << FEEDBACK_SHIFT)
#define FEEDBACK_MIN	(42 << FEEDBACK_SHIFT)

/*
 * how often we'll send it
 */
#define SOF_SHIFT	4

/*
 * how feedback value relates to circular buffer position:
 * delta in 0 .. (RBSIZE << SOF_SHIFT) to +- 1/8 FEEDBACK =>
 * +- (6 << FEEDBACK_SHIFT) / (1 << (RINGBUF_SHIFT + SOF_SHIFT) =>
 * 0 .. 3 * (1 << (FEEDBACK_SHIFT + 2 - RINGBUF_SHIFT - SOF_SHIFT)) =>
 * > (1 << (FEEDBACK_SHIFT + 3 - SOF_SHIFT - RINGBUF_SHIFT))
 */
#if (FEEDBACK_SHIFT + 3 - SOF_SHIFT) > RINGBUF_SHIFT
#define DELTA_SHIFT(x) ((x) << (FEEDBACK_SHIFT + 3 - SOF_SHIFT - RINGBUF_SHIFT))
#else
#define DELTA_SHIFT(x) ((x) >> (RINGBUF_SHIFT + SOF_SHIFT - 3 - FEEDBACK_SHIFT))
#endif

/*
 * audio frame
 */
typedef struct {
	float l;
	float r;
	float c;
} frame_t;

#define NCHANNELS       (sizeof(frame_t)/sizeof(float))

/*
 * double buffered dma halves
 */
typedef enum {
	BUSY_PAGE,
	FREE_PAGE
} page_t;

/*
 * sync'd with usb descriptor alt settings order
 */
typedef enum {
	SAMPLE_FORMAT_NONE,
	SAMPLE_FORMAT_S16,
	SAMPLE_FORMAT_S24,
	SAMPLE_FORMAT_S32,
	SAMPLE_FORMAT_F32
} sample_fmt;

typedef enum {
	SAMPLE_RATE_44100 = 44100,
	SAMPLE_RATE_48000 = 48000,
	SAMPLE_RATE_88200 = 88200,
	SAMPLE_RATE_96000 = 96000
} sample_rate;

/*
 * input frame size, bytes
 */
static inline uint16_t framesize(sample_fmt fmt)
{
	return (const uint16_t []) {
		2 * 2,	/* SAMPLE_FORMAT_NONE */
		2 * 2,	/* SAMPLE_FORMAT_S16 */
		2 * 3,	/* SAMPLE_FORMAT_S24 */
		2 * 4,	/* SAMPLE_FORMAT_S32 */
		2 * 4	/* SAMPLE_FORMAT_F32 */
	} [fmt];
}

/*
 *
 */
typedef struct {
	bool seen;
	enum {
		STATE_CLOSED,
		STATE_FILL,
		STATE_RUNNING,
		STATE_DRAIN
	} state;
} ev_t;

/*
 *
 */
enum { muted, spmuted, boost, sine, usb, sw_num };
typedef struct {
	bool on[sw_num];
	uint16_t attn;
	float rms[2];
} cs_t;

/*
 *
 */
#define MIN(a,b) ((a) < (b) ? (a) : (b))
