/* -*- mode: c; mode: folding; tab-width: 8 -*-
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <alloca.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "common.h"
#include "dsp.h"

static const struct {
	const int16_t * start;
	const int16_t * end;
} tables [SAMPLE_TABLE_END] = {
	{ .start = s1_tbl, .end = s1_tbl_end },
	{ .start = s2_tbl, .end = s2_tbl_end },
	{ .start = s3_tbl, .end = s3_tbl_end }
};

/*
 */
static uint8_t ringbuf[RBSIZE] __attribute__((aligned(4)));
typedef union {
	struct {
		uint16_t head;
		uint16_t tail;
	};
	uint32_t u32;
} rb_t;

static rb_t rb;

static sample_fmt format;
static uint16_t chunksize;

void rb_setup(sample_fmt fmt)
{
	rb.u32 = 0;
	format = fmt;
	chunksize = (const uint16_t []) {
	    2 * NSAMPLES,		/* SAMPLE_FORMAT_NONE */
	    2 * NSAMPLES,		/* SAMPLE_FORMAT_S16 */
	    3 * NSAMPLES,		/* SAMPLE_FORMAT_S24 */
	    4 * NSAMPLES,		/* SAMPLE_FORMAT_S32 */
	    4 * NSAMPLES		/* SAMPLE_FORMAT_F32 */
	} [fmt];
}

uint16_t rb_put(void *src, uint16_t len)
{
	rb_t r;
	uint16_t count, space;

	r.u32 = rb.u32;

	if ((space = CIRC_SPACE(r.head, r.tail, RBSIZE)) < len) return 0;

	rb.head = (r.head + len) & (RBSIZE - 1);
	space -= len;

	count = CIRC_SPACE_TO_END(r.head, r.tail, RBSIZE);

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
 * FIR filters
 */
#include "tables.h"
#define NUMTAPS (sizeof(hc)/sizeof(hc[0]))
#define PHASELENGTH (NUMTAPS / UPSAMPLE)
#define VOLSTEPS (sizeof(vl)/sizeof(vl[0]))

#define FX8  (float)(1U<<7)
#define FX16 (float)(1U<<15)
#define FX24 (float)(1U<<23)
#define FX32 (float)(1U<<31)

static volatile struct {
	uint8_t level;
	bool seen;
} volume;

void cvolume(uac_rq req, uint16_t chan, int16_t *val)
{
	(void) chan;
	switch (req) {
	case UAC_SET_CUR:
		volume.level = - (*val >> 8);
		volume.seen = false;
		break;
	case UAC_SET_MIN:
	case UAC_SET_MAX:
	case UAC_SET_RES:
		break;
	case UAC_GET_CUR:
		*val =  - (volume.level << 8);
		break;
	case UAC_GET_MIN:
		*val = - ((int16_t)VOLSTEPS * 256);
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

/*
 *
 */
__attribute__((optimize (3)))
static void upsample(float *dst, const int16_t *src)
{
	static float taps[NUMTAPS];
	static float state[2 * (PHASELENGTH - 1) + NSAMPLES];
	float *samples = &state[2 * (PHASELENGTH - 1)];
	float *backlog = state;
	uint16_t i, nsamples = NSAMPLES;
	const int32_t *s32 = (int32_t *)src;

	if (!volume.seen) {
		volume.seen = true;
		if (volume.level)
			for (i=0; i < NUMTAPS; i++)
				taps[i] = hc[i] * vl[volume.level];
		else
			memcpy(taps, hc, sizeof(hc));
	}

	while (nsamples) {
		switch (format) {
		case SAMPLE_FORMAT_F32:
			*samples++ = *(float *)s32++;
			*samples++ = *(float *)s32++;
			break;
		case SAMPLE_FORMAT_S32:
			*samples++ = *s32++ / FX32;
			*samples++ = *s32++ / FX32;
			break;
		case SAMPLE_FORMAT_S24:
		{
			union {
				struct __attribute__((packed)) {
					int32_t l : 24;
					int32_t r : 24;
				};
				uint16_t u[3];
			} s;

			s.u[0] = *src++;
			s.u[1] = *src++;
			s.u[2] = *src++;
			*samples++ = s.l / FX24;
			*samples++ = s.r / FX24;
		}
			break;
		case SAMPLE_FORMAT_S16:
		case SAMPLE_FORMAT_NONE: /* table run, s16 */
			*samples++ = *src++ / FX16;
			*samples++ = *src++ / FX16;
		}

		for (i = UPSAMPLE; i; i--) {
			const float *tap = taps + i - 1;
			float *sample = backlog;
			float suml, sumr;
			uint16_t k;

			for (suml = sumr = 0.0f, k = PHASELENGTH; k; k--) {
				suml += *sample++ * *tap;
				sumr += *sample++ * *tap;
				tap += UPSAMPLE;
			}

			*dst++ = suml;
			*dst++ = sumr;
		}

		backlog += 2;
		nsamples -= 2;
	}

	for (i = 2 * (PHASELENGTH - 1), samples = state; i; i--)
		*samples++ = *backlog++;
}

#define ORDER 4
const float abg[] = { .0157f, .1359f, .514f, .3609f, .003f, .0018f };

__attribute__((optimize (3)))
static uint32_t ns(const float *src, float *z)
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
	z[4] = (p = __ssat((int32_t)(sum * FX8), 8)) / FX8;

	return (1U<<7) + p;
}

__attribute__((optimize (3)))
static void sigmadelta(uint32_t *dst, const float *src)
{
	static float z[2 * (ORDER + 1)];

	for (uint16_t nsamples = NFRAMES; nsamples; nsamples -= 2) {
		*dst++ = ns(src++, z);
		*dst++ = ns(src++, &z[ORDER + 1]);
	}
}

/*
 *
 */
__attribute__((optimize (3)))
static uint16_t resample(uint32_t *dst, const int16_t *src)
{
	float samples[NFRAMES];

	upsample(samples, src);
	sigmadelta(dst, samples);

	return NFRAMES * 4;
}

#ifndef KICKSTART

static uint16_t resample_ringbuf(void *dst)
{
	uint16_t count, len = chunksize;
	uint8_t *buf;
	rb_t r;

	r.u32 = rb.u32;

	if (CIRC_CNT(r.head, r.tail, RBSIZE) < len) return 0;

	buf = alloca(len);
	count = CIRC_CNT_TO_END(r.head, r.tail, RBSIZE);

	if (count) {
		count = (count < len) ? count : len;
		memcpy(buf, &ringbuf[r.tail], count);
		len -= count;
	}

	if (len) {
		memcpy(buf + count, ringbuf, len);
	}

	rb.tail = (r.tail + chunksize) & (RBSIZE - 1);

	return resample(dst, (void *)buf);
}

#endif

/*
 */

static const int16_t *table_start = s2_tbl;
static const int16_t *table_end = s2_tbl_end;
static const int16_t *table = s2_tbl;

static uint16_t resample_table(void *dst)
{
	uint16_t len;

	if (!table_start) return 0;
	if (table == table_end) table = table_start;
	len = resample(dst, table);
	table += NSAMPLES;
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

	format == SAMPLE_FORMAT_NONE ?
		resample_table(dst) : resample_ringbuf(dst);
}

#else
/* {{{ */

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define NPASS 6 * 16
#define BUFSZ (NFRAMES * (NPASS + sizeof(uint32_t)))

int main(int ac, char **av)
{
	uint8_t *buf;
	uint32_t *buf32;
	uint16_t len;
	int i, fd, pass = NPASS;
	off_t sum = 0;

	debugf("table s1: %p-%p size[w]: %ld\n"
	       "table s2: %p-%p size[w]: %ld\n"
	       "table s3: %p-%p size[w]: %ld\n",
	       s1_tbl, s1_tbl_end, TABLESIZE(s1),
	       s2_tbl, s2_tbl_end, TABLESIZE(s2),
	       s3_tbl, s3_tbl_end, TABLESIZE(s3));

	table = table_start = s2_tbl;
	table_end = s2_tbl_end;

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

/* }}} */
#endif
