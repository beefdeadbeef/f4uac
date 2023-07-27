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

extern volatile cs_t cstate;

/*
 *
 */
static frame_t framebuf[NFRAMES];
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
	bool doublerate;
	sample_fmt fmt;
	uint16_t nframes;
	uint16_t framesize;
	uint16_t chunksize;
	float scale;
	const float *taps;
} format;

static void reset_zstate();

void set_scale()
{
	uint16_t idx = cstate.on[muted] ? VOLSTEPS - 1 : cstate.attn;
	format.scale = scale[idx]  / (const float[]) {
		[SAMPLE_FORMAT_NONE] = 1<<0,
		[SAMPLE_FORMAT_S16] = 1<<15,
		[SAMPLE_FORMAT_S24] = 1<<23,
		[SAMPLE_FORMAT_S32] = 1<<31,
		[SAMPLE_FORMAT_F32] = 1<<0
	} [format.fmt];
}

void rb_setup(sample_fmt fmt, bool dr)
{
	rb.u32 = 0;

	format.doublerate = dr;
	format.fmt = fmt;
	format.nframes = dr ?
		NFRAMES >> UPSAMPLE_SHIFT_DR :
		NFRAMES >> UPSAMPLE_SHIFT_SR;
	format.framesize = framesize(fmt);
	format.chunksize = format.framesize * format.nframes;
	format.taps = dr ? hc_dr : hc_sr;
	reset_zstate();
	set_scale();
}

static void __attribute__((constructor)) rb_init(void)
{
	rb_setup(SAMPLE_FORMAT_NONE, false);
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
		count = MIN(count, len);
		memcpy((void *)&ringbuf[r.head], src, count);
		len -= count;
		src += count;
	}

	if (len) {
		memcpy((void *)ringbuf, src, len);
	}

	return space;
}

#pragma GCC push_options
#pragma GCC optimize 3

/*
 * TF2 biquads
 */
const float lowpass[] = {
	.00006080142895919634f,
	.00012160285791839268f,
	.00006080142895919634f,
	1.9711486088434251f,
	-.971391814559262f,
	/**/
	.00006131519781136579f,
	.00012263039562273158f,
	.00006131519781136579f,
	1.9878047101153298f,
	-.9880499709065751f,
};

const float highpass[] = {
	.9856351058506718f,
	-1.9712702117013436f,
	.9856351058506718f,
	1.9711486088434251f,
	-.971391814559262f,
	/**/
	.9939636702554763f,
	-1.9879273405109525f,
	.9939636702554763f,
	1.9878047101153298f,
	-.9880499709065751f,
};

static float qqstate[4 * NCHANNELS];

static inline float qq(float x, float *z, const float *ab)
{
	float y;

	y = x * ab[0] + z[0];
	z[0] = x * ab[1] + y * ab[3]+ z[1];
	z[1] = x * ab[2] + y * ab[4];

	return y;
}

static void filter(frame_t *frame)
{
	unsigned nframes = format.nframes;
	float l, r, x;

	while(nframes--) {
		l = frame->l;
		r = frame->r;

		x = qq(l, &qqstate[0], &highpass[0]);
		x = qq(x, &qqstate[2], &highpass[5]);
		frame->l = x;

		x = qq(r, &qqstate[4], &highpass[0]);
		x = qq(x, &qqstate[6], &highpass[5]);
		frame->r = x;

		x = .5f * (l + r);
		x = qq(x, &qqstate[8], &lowpass[0]);
		x = qq(x, &qqstate[10], &lowpass[5]);
		frame->c = x;

		frame++;
	}
}

/*
 * FIR filters
 */
#define UPSAMPLE(x) (1U << UPSAMPLE_SHIFT_##x)
#define PHASELEN(x) (NUMTAPS_##x >> UPSAMPLE_SHIFT_##x)
#define BACKLOG(x)  (PHASELEN(x) - 1)
#define NSAMPLES(x) (NFRAMES >> UPSAMPLE_SHIFT_##x)

#if (PHASELEN(DR) != PHASELEN(SR)) || (BACKLOG(DR) != BACKLOG(SR))
#error PHASELEN and BACKLOG must match
#endif

#if (BACKLOG(SR) + NSAMPLES(SR)) > (BACKLOG(DR) + NSAMPLES(DR))
#define STATELEN (BACKLOG(SR) + NSAMPLES(SR))
#else
#define STATELEN (BACKLOG(DR) + NSAMPLES(DR))
#endif

static void upsample(frame_t *dst, const frame_t *src)
{
	static frame_t state[STATELEN];
	frame_t *samples = &state[BACKLOG(DR)];
	frame_t *backlog = state;
	unsigned i, nframes = format.nframes;

	while (nframes--) {
		const float *tap = format.taps;
		uint32_t flip = !format.doublerate;

		*samples++ = *src++;
	flop:

#pragma GCC unroll 8
		for (i = UPSAMPLE(DR); i; i--) {
			frame_t *sample = backlog;
			frame_t sum;

			sum.l = sum.r = sum.c = 0.0f;
			for (int k = PHASELEN(DR); k; k--, tap++) {
				sum.l += sample->l * *tap;
				sum.r += sample->r * *tap;
				sum.c += sample->c * *tap;
				sample++;
			}

			*dst++ = sum;
		}

		if (flip--) goto flop;

		backlog++;
	}

	samples = state;

	for (i = BACKLOG(DR); i; i--)
		*samples++ = *backlog++;

}

#define QF (1U << (PWM_WIDTH - 1))
#if (NS_ORDER == 5)
const float abg[] = { .0028f, .0344f, .1852f, .5904f, 1.1120f, -.002f, -.0007f };
#elif (NS_ORDER == 4)
const float abg[] = { .0157f, .1359f, .514f, .3609f, -.0018, -.003f };
#else
const float abg[] = { .0751f, .0421f, .9811, -.0014f };
#endif
static float zstate[NCHANNELS * (NS_ORDER + 1)];

static void reset_zstate()
{
	bzero(zstate, sizeof(zstate));
}

static uint16_t ns(float src, float *z)
{
	const float *x = abg;
	const float *g = &abg[NS_ORDER];
	float sum;
	int8_t p;

	sum = src - z[0];
#if (NS_ORDER == 5)
	z[5] += *x++ * sum;
	z[4] += z[5] + *x++ * sum + g[1] * z[3];
	z[3] += z[4] + *x++ * sum;
#elif (NS_ORDER == 4)
	z[4] += *x++ * sum + g[1] * z[3];
	z[3] += z[4] + *x++ * sum;
#else
	z[3] += *x++ * sum;
#endif
	z[2] += z[3] + *x++ * sum + g[0] * z[1];
	z[1] += z[2] + *x * sum;
	sum += z[1] + z[0];
	z[0] = (p = __ssat((int32_t)(sum * QF), PWM_WIDTH)) / (float)QF;

	return QF + p;
}

static void sigmadelta(uint16_t *dst, const frame_t *src)
{
#pragma GCC unroll 4
	for (uint16_t nframes = NFRAMES; nframes; nframes--, src++) {
		*dst++ = ns(src->l, zstate);
		*dst++ = ns(src->r, &zstate[NS_ORDER + 1]);
		*dst++ = ns(src->c, &zstate[(NS_ORDER + 1)<<1]);
	}
}

static void resample(uint16_t *dst, frame_t *src)
{
	if (cstate.on[boost]) filter(src);
	upsample(framebuf, src);
	sigmadelta(dst, framebuf);
}

/*
 *
 */
static inline void reframe_f32(frame_t *dst, const float *src, uint16_t nframes)
{
	while (nframes--) {
		dst->l = format.scale * *src++;
		dst->r = format.scale * *src++;
		dst++;
	}
}

static inline void reframe_s32(frame_t *dst, const int32_t *src, uint16_t nframes)
{
	while (nframes--) {
		dst->l = format.scale * *src++;
		dst->r = format.scale * *src++;
		dst++;
	}
}

static inline void reframe_s24(frame_t *dst, const uint16_t *src, uint16_t nframes)
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
		dst->l = format.scale * s.l;
		dst->r = format.scale * s.r;
		dst++;
	}
}

static inline void reframe_s16(frame_t *dst, const int16_t *src, uint16_t nframes)
{
	while (nframes--) {
		dst->l = format.scale * *src++;
		dst->r = format.scale * *src++;
		dst++;
	}
}

/*
 * reframes len bytes, containing nframes full frames
 */
static uint16_t reframe(frame_t *dst, const void *src, uint16_t len)
{
	uint16_t nframes = len / format.framesize;

	switch (format.fmt) {
	case SAMPLE_FORMAT_F32:
		reframe_f32(dst, src, nframes);
		break;

	case SAMPLE_FORMAT_S32:
		reframe_s32(dst, src, nframes);
		break;

	case SAMPLE_FORMAT_S24:
		reframe_s24(dst, src, nframes);
		break;

	case SAMPLE_FORMAT_S16:
		reframe_s16(dst, src, nframes);
		break;

	case SAMPLE_FORMAT_NONE:
		break;
	}

	return nframes;
}

/*
 *
 */
extern uint16_t *pframe(page_t page);

void pump(page_t page)
{
	uint16_t count, len = format.chunksize;
	uint16_t tail = 0, framelen = format.framesize;
	uint16_t *dst = pframe(page);
	frame_t *p, *buf;
	rb_t r;

	r.u32 = rb.u32;

	if (rb_count(r) < len) return;

	p = buf = &framebuf[NFRAMES - format.nframes];

	count = rb_count_to_end(r);

	if (count) {
		count = MIN(count, len);
		if ((tail = count % framelen)) {
			*(uint32_t *)&ringbuf[RBSIZE] = *(uint32_t *)ringbuf;
			tail = framelen - tail;
			count += tail;
		}
		p += reframe(p, &ringbuf[r.tail], count);
		len -= count;
	}

	if (len) {
		reframe(p, ringbuf + tail, len);
	}

	rb.tail = (r.tail + format.chunksize) & (RBSIZE - 1);

	resample(dst, buf);
}
