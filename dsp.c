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
static int32_t framebuf[NCHANNELS * NFRAMES];
static uint8_t ringbuf[RBSIZE + 4] __attribute__((aligned(4)));
static int16_t taps[NUMTAPS16];

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
	uint16_t scale;
	const int16_t *taps;
} format;

static bool muted;
static uint16_t volidx = 6;
static void reset_zstate();

static void set_scale()
{
	uint16_t idx = muted ? VOLSTEPS - 1 : volidx;

	if (idx == format.scale) return;
	for (int i=0; i < (format.f8 ? NUMTAPS8 : NUMTAPS16); i++)
		taps[i] = (format.taps[i] * (int32_t)scale[idx]) >> 16;
	format.scale = idx;
}

void rb_setup(sample_fmt fmt, bool f8)
{
	rb.u32 = 0;

	format.f8 = f8;
	format.fmt = fmt;
	format.nframes = f8 ?
		NFRAMES >> UPSAMPLE_SHIFT_8 :
		NFRAMES >> UPSAMPLE_SHIFT_16;
	format.framesize = framesize(fmt);
	format.chunksize = format.framesize * format.nframes;
	format.taps = f8 ? hc8 : hc16;
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

/*
 *
 */
void cmute(uac_rq req, uint8_t *val)
{
	switch (req) {
	case UAC_SET_CUR:
		muted = *val;
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
	       req, chan, *val, volidx);
}

#pragma GCC push_options
#pragma GCC optimize 3

/*
 * FIR filters
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

static void upsample(int32_t *dst, const int32_t *src)
{
	static int32_t state[STATELEN];
	int32_t *samples = &state[BACKLOG(8)];
	int32_t *backlog = state;
	unsigned i, nframes = format.nframes;

	while (nframes--) {
		const uint32_t *tap = (uint32_t *)taps;

		*samples++ = *src++;
		*samples++ = *src++;

		uint32_t flip = !format.f8;
	flop:
		for (i = UPSAMPLE(8); i; i--) {
			int32_t *sample = backlog;
			int32_t suml, sumr;

			suml = sumr = 0;
			for (int k = PHASELEN(8)>>1; k; k--, tap++) {
				__smlawb(suml, *sample++, *tap, suml);
				__smlawb(sumr, *sample++, *tap, sumr);
				__smlawt(suml, *sample++, *tap, suml);
				__smlawt(sumr, *sample++, *tap, sumr);
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

#if (NS_ORDER == 5)
const int16_t abg[] = { 45, 563, 3034, 9673, 18219, -32, -11 };
#elif (NS_ORDER == 4)
const int16_t abg[] = { 514, 4453, 16843, 11826, -59, -98 };
#else
const int16_t abg[] = { 2460, 1379, 32148, -45 };
#endif
static int32_t zstate[NCHANNELS * (NS_ORDER + 1)];

static void reset_zstate()
{
	bzero(zstate, sizeof(zstate));
}

static uint16_t ns(const int32_t *src, int32_t *z)
{
	const uint32_t *g = (uint32_t *)&abg[NS_ORDER];
	const uint32_t *x = (uint32_t *)abg;
	int32_t sum;
	int16_t p;

	sum = *src - z[0];
#if (NS_ORDER == 5)
	__smlawb(z[5], sum, *x, z[5]);
	__smlawt(z[4], sum, *x++, z[4]);
	__smlawt(z[4], z[3], *g, z[5]);
	__smlawb(z[3], sum, *x, z[3]);
	z[3] += z[4];
	__smlawt(z[2], sum, *x++, z[2]);
	__smlawb(z[2], z[1], *g, z[3]);
	__smlawb(z[1], sum, *x, z[1]);
#elif (NS_ORDER == 4)
	__smlawb(z[4], sum, *x, z[4]);
	__smlawt(z[4], z[3], *g, z[4]);
	__smlawt(z[3], sum, *x++, z[3]);
	z[3] += z[4];
	__smlawb(z[2], sum, *x, z[2]);
	__smlawb(z[2], z[1], *g, z[3]);
	__smlawt(z[1], sum, *x, z[1]);
#else
	__smlawb(z[3], sum, *x, z[3]);
	__smlawt(z[2], sum, *x++, z[2]);
	__smlawb(z[2], z[1], *g, z[3]);
	__smlawb(z[1], sum, *x, z[1]);
#endif
	z[1] += z[2];
	sum += z[1] + z[0];
	z[0] = (p = __ssat(sum, PWM_WIDTH, PWM_SHIFT)) << PWM_SHIFT;

	return p + (1 << (PWM_WIDTH - 1));
}

static void sigmadelta(uint16_t *dst, const int32_t *src)
{
	for (uint16_t nframes = NFRAMES; nframes; nframes--) {
		*dst++ = ns(src++, zstate);
		*dst++ = ns(src++, &zstate[NS_ORDER + 1]);
	}
}

static void resample(uint16_t *dst, const int32_t *src)
{
	set_scale();
	upsample(framebuf, src);
	sigmadelta(dst, framebuf);
}

/*
 *
 */
static inline int32_t *reframe_s24(int32_t *dst, const uint16_t *src, uint16_t nframes)
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
		*dst++ = s.l << 8;
		*dst++ = s.r << 8;
	}

	return dst;
}

static inline int32_t *reframe_s16(int32_t *dst, const int16_t *src, uint16_t nframes)
{
	while (nframes--) {
		*dst++ = *src++ << 16;
		*dst++ = *src++ << 16;
	}
	return dst;
}

static int32_t *reframe(int32_t *dst, const void *src, uint16_t len)
{
	uint16_t nframes = len / format.framesize;

	switch (format.fmt) {
	case SAMPLE_FORMAT_S32:
		memcpy((void *)dst, src, len);
		return dst + nframes * NCHANNELS;

	case SAMPLE_FORMAT_S24:
		return reframe_s24(dst, src, nframes);

	case SAMPLE_FORMAT_S16:
		return reframe_s16(dst, src, nframes);

	case SAMPLE_FORMAT_NONE:
		break;
	}
	return dst;
}

static void resample_ringbuf(uint16_t *dst)
{
	uint16_t count, len = format.chunksize;
	uint16_t tail = 0, framelen = format.framesize;
	int32_t *p, *buf;
	rb_t r;

	r.u32 = rb.u32;

	if (rb_count(r) < len) return;

	p = buf = &framebuf[NCHANNELS * (NFRAMES - format.nframes)];

	count = rb_count_to_end(r);

	if (count) {
		count = MIN(count, len);
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
static void resample_table(uint16_t *dst)
{
	uint32_t count, slen = sizeof(stbl) / sizeof(stbl[0]);
	uint32_t nframes = format.nframes;
	static uint32_t idx;
	int32_t *p, *buf;

	p = buf = &framebuf[NCHANNELS * (NFRAMES - format.nframes)];

	while(nframes) {
		count = MIN(nframes, slen - idx);
		nframes -= count;
		while(count--) {
			*p++ = stbl[idx] << 16;
			*p++ = - stbl[idx] << 16;
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
