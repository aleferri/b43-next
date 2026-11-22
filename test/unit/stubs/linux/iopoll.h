/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stub linux/iopoll.h: the kernel's atomic poll loop, op for op, so that a
 * poll reads the register as many times here as on the hardware. The delays
 * are wrap.c's, which take no time.
 */
#ifndef _STUB_LINUX_IOPOLL_H
#define _STUB_LINUX_IOPOLL_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>

#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC	1000L
#endif

#define poll_timeout_us_atomic(op, cond, delay_us, timeout_us, \
			       delay_before_op) \
({ \
	u64 __timeout_us = (timeout_us); \
	s64 __left_ns = __timeout_us * NSEC_PER_USEC; \
	unsigned long __delay_us = (delay_us); \
	u64 __delay_ns = __delay_us * NSEC_PER_USEC; \
	int ___ret; \
	if ((delay_before_op) && __delay_us) { \
		udelay(__delay_us); \
		if (__timeout_us) \
			__left_ns -= __delay_ns; \
	} \
	for (;;) { \
		bool __expired = __timeout_us && __left_ns < 0; \
		op; \
		if (cond) { \
			___ret = 0; \
			break; \
		} \
		if (__expired) { \
			___ret = -ETIMEDOUT; \
			break; \
		} \
		if (__delay_us) { \
			udelay(__delay_us); \
			if (__timeout_us) \
				__left_ns -= __delay_ns; \
		} \
		if (__timeout_us) \
			__left_ns--; \
	} \
	___ret; \
})

#define read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us, \
				 delay_before_read, args...) \
	poll_timeout_us_atomic((val) = op(args), cond, delay_us, timeout_us, \
			       delay_before_read)

#endif
