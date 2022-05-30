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

static const struct {
	const float * start;
	const float * end;
} tables [SAMPLE_TABLE_END] = {
	{ .start = s1_tbl, .end = s1_tbl + sizeof(s1_tbl)/sizeof(s1_tbl[0]) },
	{ .start = s2_tbl, .end = s2_tbl + sizeof(s2_tbl)/sizeof(s2_tbl[0]) }
};

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
} format;

static void reset_zstate();

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

	reset_zstate();
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
static volatile uint16_t volidx;

void cvolume(uac_rq req, uint16_t chan, int16_t *val)
{
	(void) chan;
	switch (req) {
	case UAC_SET_CUR:
	{
		uint16_t i = 0;
		while (i < VOLSTEPS && vl[i++] > *val);
		volidx = i == VOLSTEPS ? VOLSTEPS - 1 : i;
		break;
	}
	case UAC_SET_MIN:
	case UAC_SET_MAX:
	case UAC_SET_RES:
		break;
	case UAC_GET_CUR:
		*val =  vl[volidx];
		break;
	case UAC_GET_MIN:
		*val = vl[VOLSTEPS - 1];
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
#define NUMTAPS(x)  (NUMTAPS##x)
#define UPSAMPLE(x) (1U << UPSAMPLE_SHIFT_##x)
#define PHASELEN(x) (NUMTAPS(x) >> UPSAMPLE_SHIFT_##x)
#define NSAMPLES(x) ((NCHANNELS * NFRAMES) >> UPSAMPLE_SHIFT_##x)
#define BACKLOG(x)  (NCHANNELS * (PHASELEN(x) - 1))

#if (PHASELEN(8) != PHASELEN(16)) || (BACKLOG(8) != BACKLOG(16))
#error PHASELEN and BACKLOG must match
#endif

#if (BACKLOG(16) + NSAMPLES(16)) > (BACKLOG(8) + NSAMPLES(8))
#define STATELEN (BACKLOG(16) + NSAMPLES(16))
#else
#define STATELEN (BACKLOG(8) + NSAMPLES(8))
#endif

#define FX16 (float)(1U<<15)
#define FX24 (float)(1U<<23)
#define FX32 (float)(1U<<31)

static inline float *reframe_s32(float *dst, const int32_t *src, uint16_t nframes)
{
	while (nframes--) {
		*dst++ = *src++ / FX32;
		*dst++ = *src++ / FX32;
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

	uint16_t tail;

	nframes -= (tail = nframes % 4);

#pragma GCC unroll 4
	while (nframes--) {
		s.u[0] = *src++;
		s.u[1] = *src++;
		s.u[2] = *src++;
		*dst++ = s.l / FX24;
		*dst++ = s.r / FX24;
	}

	while (tail--) {
		s.u[0] = *src++;
		s.u[1] = *src++;
		s.u[2] = *src++;
		*dst++ = s.l / FX24;
		*dst++ = s.r / FX24;
	}

	return dst;
}

static inline float *reframe_s16(float *dst, const int16_t *src, uint16_t nframes)
{
	while (nframes--) {
		*dst++ = *src++ / FX16;
		*dst++ = *src++ / FX16;
	}
	return dst;
}

static float *reframe(float *dst, const void *src, uint16_t len)
{
	uint16_t nframes = len / format.framesize;

	switch (format.fmt) {
	case SAMPLE_FORMAT_F32:
		memcpy((void *)dst, src, len);
		return dst + NCHANNELS * nframes;

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
	const float *taps;
	static float state[STATELEN];
	float *samples = &state[BACKLOG(8)];
	float *backlog = state;
	uint16_t i, nframes;

	if (format.f8) {
		nframes = NFRAMES >> UPSAMPLE_SHIFT_8;
		taps = hc8 + NUMTAPS(8) * volidx;
	} else {
		nframes = NFRAMES >> UPSAMPLE_SHIFT_16;
		taps = hc16 + NUMTAPS(16) * volidx;
	}

	while (nframes--) {
		const float *tap = taps;

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

static inline uint32_t ns(const float *src, float *z)
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

static void sigmadelta(uint32_t *dst, const float *src)
{
	for (uint16_t nframes = NFRAMES; nframes; nframes--) {
		*dst++ = ns(src++, zstate);
		*dst++ = ns(src++, &zstate[ORDER + 1]);
	}
}

static uint16_t resample(uint32_t *dst, const float *src)
{
	float samples[NCHANNELS * NFRAMES];

	upsample(samples, src);
	sigmadelta(dst, samples);

	return NCHANNELS * NFRAMES * 4;
}

#pragma GCC pop_options

/*
 *
 */
#ifndef KICKSTART

static uint16_t resample_ringbuf(void *dst)
{
	uint16_t count, len = format.chunksize;
	uint16_t tail = 0, framelen = format.framesize;
	float *p, *buf;
	rb_t r;

	r.u32 = rb.u32;

	if (rb_count(r) < len) return 0;

	p = buf = alloca(format.nframes * sizeof(float) * NCHANNELS);

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

	return resample(dst, buf);
}

#endif

/*
 */

static const float *table_start = s1_tbl;
static const float *table_end = s1_tbl + sizeof(s1_tbl)/sizeof(s1_tbl[0]);
static const float *table = s1_tbl;

static uint16_t resample_table(void *dst)
{
	uint16_t len;

	if (!table_start) return 0;
	if (table == table_end) table = table_start;
	len = resample(dst, (void *)table);
	table += NCHANNELS * NFRAMES >> UPSAMPLE_SHIFT_16;
	return len;
}

#ifndef KICKSTART

void select_table(sample_table tbl)
{
	table = table_start = tables[tbl].start;
	table_end = tables[tbl].end;
}

extern void *pframe(frame_type frame);

void pump(frame_type frame)
{
	void *dst = pframe(frame);

	format.fmt == SAMPLE_FORMAT_NONE ?
		resample_table(dst) : resample_ringbuf(dst);
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define NPASS 6 * 16
#define BUFSZ (NCHANNELS * NFRAMES * (NPASS + sizeof(uint32_t)))

int main(int ac, char **av)
{
	uint8_t *buf;
	uint32_t *buf32;
	uint16_t len;
	int i, fd, pass = NPASS;
	off_t sum = 0;

	table = table_start = s1_tbl;
	table_end = s1_tbl + sizeof(s1_tbl)/sizeof(s1_tbl[0]);

	fd = open("tool.u8", O_RDWR|O_CREAT|O_TRUNC, 0644);
	lseek(fd, BUFSZ - 1, SEEK_SET);
	write(fd, "", 1);

	buf = mmap(NULL, BUFSZ, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	do {
		sum += len = resample_table(buf) >> 2;
		debugf("len=%d sum=%ld of %ld\n", len, sum, BUFSZ);

		for (i=0, buf32=(uint32_t *)buf; i<len; i++)
			*buf++ = *buf32++ &0xff;

	} while (--pass);

	ftruncate(fd, sum);
}

#endif
