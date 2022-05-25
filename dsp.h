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

#else

__attribute__((always_inline))
static inline int32_t __ssat(int32_t val, uint32_t sat)
{
        if ((sat >= 1U) && (sat <= 32U)) {
                const int32_t max = (int32_t)((1U << (sat - 1U)) - 1U);
                const int32_t min = -1 - max ;
                if (val > max) {
                        return max;
                } else if (val < min) {
                        return min;
                }
        }
        return val;
}

#endif
