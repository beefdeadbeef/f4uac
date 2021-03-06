/*  -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <stdint.h>
#include <stdbool.h>
#include "debug.h"

/*
 * our main clock
 */
#define PLLO 96000000UL

/*
 * we're stereo ffs
 */
#define NCHANNELS       2

/*
 * number of audio frames after upsampling, must be 2^(4+N)
 */
#define NFRAMES_SHIFT   10
#define NFRAMES	        (1 << NFRAMES_SHIFT)

/*
 * pwm width
 */
#define PWM_SHIFT 	7
#define PWM_PERIOD 	(1 << PWM_SHIFT)

/*
 * circular buffer size, must be 2^N
 */
#define RINGBUF_SHIFT	12
#define RBSIZE		(1 << RINGBUF_SHIFT)

/*
 * async usb audio feedback
 * frames # per 1ms in Q10.14 format
 */
#define FEEDBACK_SHIFT 14
#define FEEDBACK (48 << FEEDBACK_SHIFT)
#define FEEDBACK_MIN (42 << FEEDBACK_SHIFT)

/*
 * how often we'll send it
 */
#define SOF_RATE 4

/*
 * how feedback value relates to circular buffer position:
 * delta in 0 .. (RBSIZE << SOF_RATE) to +- 1/8 FEEDBACK =>
 * +- (6 << FEEDBACK_SHIFT) / (1 << (RINGBUF_SHIFT + SOF_RATE) =>
 * 0 .. 3 * (1 << (FEEDBACK_SHIFT + 2 - RINGBUF_SHIFT - SOF_RATE)) =>
 * > (1 << (FEEDBACK_SHIFT + 3 - SOF_RATE - RINGBUF_SHIFT))
 */
#if (FEEDBACK_SHIFT + 3 - SOF_RATE) > RINGBUF_SHIFT
#define DELTA_SHIFT(x) ((x) << (FEEDBACK_SHIFT + 3 - SOF_RATE - RINGBUF_SHIFT))
#else
#define DELTA_SHIFT(x) ((x) >> (RINGBUF_SHIFT + SOF_RATE - 3 - FEEDBACK_SHIFT))
#endif

/*
 *
 */
typedef enum {
	BUSY_FRAME,
	FREE_FRAME
} frame_type;

/*
 *
 */
typedef enum {
	SAMPLE_TABLE_S1,
	SAMPLE_TABLE_S2,
	SAMPLE_TABLE_END
} sample_table;

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
	SAMPLE_RATE_48000 = 48000,
	SAMPLE_RATE_96000 = 96000
} sample_rate;

/*
 * frame size, bytes
 */
static inline uint16_t framesize(sample_fmt fmt)
{
	return (const uint8_t []) {
		4 * NCHANNELS,	/* SAMPLE_FORMAT_NONE */
		2 * NCHANNELS,	/* SAMPLE_FORMAT_S16 */
		3 * NCHANNELS,	/* SAMPLE_FORMAT_S24 */
		4 * NCHANNELS,	/* SAMPLE_FORMAT_S32 */
		4 * NCHANNELS	/* SAMPLE_FORMAT_F32 */
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
 * Audio Class-Specific Request Codes
 */
typedef enum  {
	UAC_SET_CUR = 1,
	UAC_SET_MIN,
	UAC_SET_MAX,
	UAC_SET_RES,
	UAC_GET_CUR = 0x81,
	UAC_GET_MIN,
	UAC_GET_MAX,
	UAC_GET_RES
} uac_rq;
