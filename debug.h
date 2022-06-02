/*
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#ifdef DEBUG

#include <inttypes.h>
#include <stdio.h>

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
#define debugf(fmt, args...)

#endif
