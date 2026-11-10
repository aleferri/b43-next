/*
 * kernel_shim.h — userspace shim of the kernel symbols used by
 * bcmn_sprom_extract_r11(), which the Makefile cuts out of bcma-next/sprom.c
 * with its SPOFF/SPEX macros: the kernel-side names for fixed-width integer
 * types, the byte-order helper, BUILD_BUG_ON, ARRAY_SIZE.
 */

#ifndef KERNEL_SHIM_H
#define KERNEL_SHIM_H

#define _DEFAULT_SOURCE  /* expose htobe16() from <endian.h> */

#include <stdint.h>
#include <endian.h>

typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef uint32_t u32;

/* The kernel uses __be16 for the BE-stored portion of il0mac inside
 * struct ssbn_sprom. We mirror only the storage shape, not the
 * sparse/__bitwise annotation. */
typedef uint16_t __be16;

/* bcma-next/sprom.c uses cpu_to_be16() to byteswap each u16 word
 * of the MAC address into the il0mac u8[6] slot. Userspace htobe16()
 * has identical semantics. */
#define cpu_to_be16(x) htobe16(x)

/* ARRAY_SIZE: bcma-next/sprom.c relies on the kernel-wide
 * definition. The compile-time-constant version below is sufficient
 * for the call sites we exercise (BUILD_BUG_ON on r11_pwr_info_offset
 * vs core_pwr_info, plus the loop bound). */
#define __aligned(x) __attribute__((aligned(x)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* BUILD_BUG_ON in the kernel triggers a compile error if the
 * expression is non-zero. _Static_assert has the same semantics for
 * compile-time-constant expressions, which is what the parser uses
 * it for here (sizing invariants between r11_pwr_info_offset and
 * core_pwr_info). */
#define BUILD_BUG_ON(cond) _Static_assert(!(cond), #cond)

#endif /* KERNEL_SHIM_H */
