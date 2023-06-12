/*
 *  SPDX-License-Identifier: MIT
 *  Copyright (C) 2021-2022 Sergey Bolshakov <beefdeadbeef@gmail.com>
 */

#include <stdint.h>

#if defined (__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)

#define __smulw(w, h)							\
        ({                                                              \
                int32_t out;                                            \
                __asm__ volatile ("smulwb %0, %1, %2"			\
                                  : "=r" (out)                          \
                                  : "r" (w), "r" (h));			\
		out;							\
        })

#define __smlawb(acc, w, h, add)					\
        ({                                                              \
                __asm__ volatile ("smlawb %0, %1, %2, %3"		\
                                  : "+r" (acc)				\
                                  : "r" (w), "r" (h), "r" (add));	\
        })

#define __smlawt(acc, w, h, add)					\
        ({                                                              \
                __asm__ volatile ("smlawt %0, %1, %2, %3"		\
                                  : "+r" (acc)				\
                                  : "r" (w), "r" (h), "r" (add));	\
        })

#define __ssat(val, pos, rsh)						\
        ({                                                              \
                int32_t out;                                            \
                __asm__ volatile ("ssat %0, %1, %2, asr %3"		\
                                  : "=r" (out)                          \
                                  : "I" (pos), "r" (val), "I" (rsh));	\
                out;                                                    \
        })

#endif
