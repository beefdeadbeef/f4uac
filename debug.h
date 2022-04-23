/*
 */

#ifdef DEBUG

#include <inttypes.h>
#include <stdio.h>

#ifndef __linux__

#include "trace.h"

extern volatile uint32_t systicks;

#define trace(port, val)                        \
    do {                                        \
        trace_send_blocking32(port, systicks);  \
        trace_send_blocking32(port, val);       \
    } while(0)

#define debugf(fmt, args...)                                            \
    do {                                                                \
        printf("[%08lx] %s(): " fmt, systicks, __func__, ##args);       \
    } while (0)

#else

#define trace(port, val)

#define debugf(fmt, args...)                                        \
    do {                                                            \
        fprintf(stderr, "%s(): " fmt, __func__, ##args);            \
    } while (0)
#endif

#else

#define trace(port, val)
#define debugf(fmt, args...)

#endif
