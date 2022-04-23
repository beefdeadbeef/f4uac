/*
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

__attribute__((always_inline))
static inline int32_t ssat(int64_t val)
{
        return  __ssat((int32_t)(val >> 32), 31) << 1;
}

__asm__ (
	".pushsection .rodata,\"a\"\n"
	".global s1_tbl\n"
	" s1_tbl:\n"
	".incbin \"s1.s16\"\n"
	".global s1_tbl_end\n"
	"s1_tbl_end:\n"
	".global s2_tbl\n"
	" s2_tbl:\n"
	".incbin \"s2.s16\"\n"
	".global s2_tbl_end\n"
	"s2_tbl_end:\n"
	".global s3_tbl\n"
	"s3_tbl:\n"
	".incbin \"s3.s16\"\n"
	".global s3_tbl_end\n"
	"s3_tbl_end:\n"
	".popsection\n"
	);

extern const int16_t s1_tbl[];
extern const int16_t s1_tbl_end[];

extern const int16_t s2_tbl[];
extern const int16_t s2_tbl_end[];

extern const int16_t s3_tbl[];
extern const int16_t s3_tbl_end[];

#define TABLESIZE(x) (x##_tbl_end - x##_tbl)
