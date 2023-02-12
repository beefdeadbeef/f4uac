/* -*- mode: c; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <alloca.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "common.h"
#include "dsp.h"
#include "tables.h"

/*
 *
 */
static uint8_t ringbuf[RBSIZE + 4] __attribute__((aligned(4)));

typedef union {
	struct {
		uint16_t head;
		uint16_t tail;
	};
	uint32_t u32;
} rb_t;

static rb_t rb;

static inline uint16_t rb_count(rb_t r)
{
	return (r.head - r.tail) & (RBSIZE - 1);
}

static inline uint16_t rb_space(rb_t r)
{
	return (r.tail - r.head - 1) & (RBSIZE - 1);
}

static inline uint16_t rb_count_to_end(rb_t r)
{
	uint16_t end = RBSIZE - r.tail;
	uint16_t n = (r.head + end) & (RBSIZE - 1);
	return n < end ? n : end;
}

static inline uint16_t rb_space_to_end(rb_t r)
{
	uint16_t end = RBSIZE - r.head - 1;
	uint16_t n = (r.tail + end) & (RBSIZE - 1);
	return n <= end ? n : end + 1;
}

static struct {
	bool f8;
	sample_fmt fmt;
	uint16_t nframes;
	uint16_t framesize;
	uint16_t chunksize;
	float scale;
	const float *taps;
} format;

static bool muted;
static uint16_t volidx = 6;
static void reset_zstate();

static void set_scale()
{
	uint16_t idx = muted ? VOLSTEPS - 1 : volidx;
	format.scale = scale[idx]  / (const float[]) {
		[SAMPLE_FORMAT_NONE] = 1<<0,
		[SAMPLE_FORMAT_S16] = 1<<15,
		[SAMPLE_FORMAT_S24] = 1<<23,
		[SAMPLE_FORMAT_S32] = 1<<31,
		[SAMPLE_FORMAT_F32] = 1<<0
	} [format.fmt];
}

void rb_setup(sample_fmt fmt, sample_rate rate)
{
	rb.u32 = 0;

	format.fmt = fmt;
	format.f8 = rate == SAMPLE_RATE_96000;
	format.nframes = format.f8 ?
		NFRAMES >> UPSAMPLE_SHIFT_8 :
		NFRAMES >> UPSAMPLE_SHIFT_16;
	format.framesize = framesize(fmt);
	format.chunksize = format.framesize * format.nframes;
	format.taps = format.f8 ? hc8 : hc16;
	reset_zstate();
	set_scale();
}

static void __attribute__((constructor)) rb_init(void)
{
	rb_setup(SAMPLE_FORMAT_NONE, SAMPLE_RATE_48000);
}

uint16_t rb_put(void *src, uint16_t len)
{
	rb_t r;
	uint16_t count, space;

	r.u32 = rb.u32;

	if ((space = rb_space(r)) < len) return 0;

	rb.head = (r.head + len) & (RBSIZE - 1);
	space -= len;

	count = rb_space_to_end(r);

	if (count) {
		count = (count < len) ? count : len;
		memcpy((void *)&ringbuf[r.head], src, count);
		len -= count;
		src += count;
	}

	if (len) {
		memcpy((void *)ringbuf, src, len);
	}

	return space;
}

/*
 *
 */
void cmute(uac_rq req, uint8_t *val)
{
	switch (req) {
	case UAC_SET_CUR:
		muted = *val;
		set_scale();
		break;
	case UAC_GET_CUR:
		*val = muted;
		break;
	default:
		break;
	}
}

void cvolume(uac_rq req, uint16_t chan, int16_t *val)
{
	(void) chan;
	switch (req) {
	case UAC_SET_CUR:
	{
		uint16_t i = 0;
		while (i < VOLSTEPS && db[i] > *val) i++;
		volidx = i;
		set_scale();
		break;
	}
	case UAC_SET_MIN:
	case UAC_SET_MAX:
	case UAC_SET_RES:
		break;
	case UAC_GET_CUR:
		*val =  db[volidx];
		break;
	case UAC_GET_MIN:
		*val = db[VOLSTEPS - 1];
		break;
	case UAC_GET_MAX:
		*val = 0;
		break;
	case UAC_GET_RES:
		*val = 256;	/* 1dB step */
	}
	debugf("req: %02x chan: %02x val: %d (%d)\n",
	       req, chan, *val, volume.level);
}

#pragma GCC push_options
#pragma GCC optimize 3

/*
 * FIR filters
 *
 * UPSAMPLE : NUMTAPS : PHASELEN : BACKLOG
 *        8 :      16 :        2 :       2
 *       16 :      32 :        2 :       2
 */
#define UPSAMPLE(x) (1U << UPSAMPLE_SHIFT_##x)
#define PHASELEN(x) (NUMTAPS##x >> UPSAMPLE_SHIFT_##x)
#define BACKLOG(x)  (NCHANNELS * (PHASELEN(x) - 1))
#define NSAMPLES(x) ((NCHANNELS * NFRAMES) >> UPSAMPLE_SHIFT_##x)

#if (PHASELEN(8) != PHASELEN(16)) || (BACKLOG(8) != BACKLOG(16))
#error PHASELEN and BACKLOG must match
#endif

#if (BACKLOG(16) + NSAMPLES(16)) > (BACKLOG(8) + NSAMPLES(8))
#define STATELEN (BACKLOG(16) + NSAMPLES(16))
#else
#define STATELEN (BACKLOG(8) + NSAMPLES(8))
#endif

static inline float *reframe_f32(float *dst, const float *src, uint16_t nframes)
{
	while (nframes--) {
		*dst++ = format.scale * *src++;
		*dst++ = format.scale * *src++;
	}
	return dst;
}

static inline float *reframe_s32(float *dst, const int32_t *src, uint16_t nframes)
{
	while (nframes--) {
		*dst++ = format.scale * *src++;
		*dst++ = format.scale * *src++;
	}
	return dst;
}

static inline float *reframe_s24(float *dst, const uint16_t *src, uint16_t nframes)
{
	union {
		struct __attribute__((packed)) {
			int32_t l : 24;
			int32_t r : 24;
		};
		uint16_t u[3];
	} s;

	while (nframes--) {
		s.u[0] = *src++;
		s.u[1] = *src++;
		s.u[2] = *src++;
		*dst++ = format.scale * s.l;
		*dst++ = format.scale * s.r;
	}

	return dst;
}

static inline float *reframe_s16(float *dst, const int16_t *src, uint16_t nframes)
{
	while (nframes--) {
		*dst++ = format.scale * *src++;
		*dst++ = format.scale * *src++;
	}
	return dst;
}

static float *reframe(float *dst, const void *src, uint16_t len)
{
	uint16_t nframes = len / format.framesize;

	switch (format.fmt) {
	case SAMPLE_FORMAT_F32:
		return reframe_f32(dst, src, nframes);

	case SAMPLE_FORMAT_S32:
		return reframe_s32(dst, src, nframes);

	case SAMPLE_FORMAT_S24:
		return reframe_s24(dst, src, nframes);

	case SAMPLE_FORMAT_S16:
		return reframe_s16(dst, src, nframes);

	case SAMPLE_FORMAT_NONE:
		break;
	}
	return dst;
}

static void upsample(float *dst, const float *src)
{
	static float state[STATELEN];
	float *samples = &state[BACKLOG(8)];
	float *backlog = state;
	uint16_t i, nframes = format.nframes;

	while (nframes--) {
		const float *tap = format.taps;

		*samples++ = *src++;
		*samples++ = *src++;

		uint32_t flip = !format.f8;
	flop:

#pragma GCC unroll 8
		for (i = UPSAMPLE(8); i; i--) {
		    float *sample = backlog;
		    float suml, sumr;
		    uint16_t k;

		    suml = sumr = 0.0f;
		    for (k = PHASELEN(8); k; k--) {
			suml += *sample++ * *tap;
			sumr += *sample++ * *tap;
			tap++;
		    }

		    *dst++ = suml;
		    *dst++ = sumr;
		}

		if (flip--) goto flop;

		backlog += NCHANNELS;
	}

	samples = state;

	for (i = BACKLOG(8); i; i--)
		*samples++ = *backlog++;

}

#define ORDER 4
#define QF (1U << (PWM_SHIFT - 1))
const float abg[] = { .0157f, .1359f, .514f, .3609f, .003f, .0018f };
static float zstate[NCHANNELS * (ORDER + 1)];

static void reset_zstate()
{
	bzero(zstate, sizeof(zstate));
}

static uint16_t ns(const float *src, float *z)
{
	const float *x = abg;
	const float *g = &abg[ORDER];
	float sum;
	int8_t p;

	sum = *src - z[4];
	z[0] += *x++ * sum + *g++ * z[1];
	z[1] += z[0] + *x++ * sum;
	z[2] += z[1] + *x++ * sum + *g * z[3];
	z[3] += z[2] + *x * sum;
	sum += z[3] + z[4];
	z[4] = (p = __ssat((int32_t)(sum * QF), PWM_SHIFT)) / (float)QF;

	return QF + p;
}

static void sigmadelta(uint16_t *dst, const float *src)
{
#pragma GCC unroll 4
	for (uint16_t nframes = NFRAMES; nframes; nframes--) {
		*dst++ = ns(src++, zstate);
		*dst++ = ns(src++, &zstate[ORDER + 1]);
	}
}

static void resample(uint16_t *dst, const float *src)
{
	float samples[NCHANNELS * NFRAMES];

	upsample(samples, src);
	sigmadelta(dst, samples);
}

#pragma GCC pop_options

/*
 *
 */
static void resample_ringbuf(uint16_t *dst)
{
	uint16_t count, len = format.chunksize;
	uint16_t tail = 0, framelen = format.framesize;
	float *p, *buf;
	rb_t r;

	r.u32 = rb.u32;

	if (rb_count(r) < len) return;

	p = buf = alloca(format.nframes * framesize(SAMPLE_FORMAT_F32));

	count = rb_count_to_end(r);

	if (count) {
		count = (count < len) ? count : len;
		if ((tail = count % framelen)) {
			*(uint32_t *)&ringbuf[RBSIZE] = *(uint32_t *)ringbuf;
			tail = framelen - tail;
			count += tail;
		}
		p = reframe(p, &ringbuf[r.tail], count);
		len -= count;
	}

	if (len) {
		reframe(p, ringbuf + tail, len);
	}

	rb.tail = (r.tail + format.chunksize) & (RBSIZE - 1);

	resample(dst, buf);
}

/*
 */
#define MIN(a,b) ((a) < (b) ? (a) : (b))

static void resample_table(uint16_t *dst)
{
	uint32_t count, slen = sizeof(stbl) / sizeof(stbl[0]);
	uint32_t nframes = format.nframes;
	static uint32_t idx;
	float *p, *buf;

	p = buf = alloca(format.chunksize);

	while(nframes) {
		count = MIN(nframes, slen - idx);
		nframes -= count;
		while(count--) {
			*p++ = format.scale * stbl[idx];
			*p++ = - format.scale * stbl[idx];
			idx++;
		}
		if (idx == slen) idx = 0;
	}

	resample(dst, buf);
}

extern uint16_t *pframe(frame_type frame);

void pump(frame_type frame)
{
	uint16_t *dst = pframe(frame);

	format.fmt == SAMPLE_FORMAT_NONE ?
		resample_table(dst) : resample_ringbuf(dst);
}
