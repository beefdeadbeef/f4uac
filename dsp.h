/*
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <stdint.h>

#if defined (__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)

#define __ssat(val, sat)                                                \
        ({                                                              \
                int32_t out;                                            \
                __asm__ volatile ("ssat %0, %1, %2"                     \
                                  : "=r" (out)                          \
                                  : "I" (sat), "r" (val)                \
                                  : "cc");                              \
                out;                                                    \
        })

#endif
