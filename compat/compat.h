/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Included before every source of the module: the one place where it
 * adapts to the kernel it is built against.
 */
#ifndef B43_NEXT_COMPAT_H_
#define B43_NEXT_COMPAT_H_

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
#error "b43-next follows the kernel's API from Linux 6.18"
#endif

/* The typed allocators came with 7.0, and with 6.18.y stable updates */
#include <linux/slab.h>
#ifndef kzalloc_obj
#define kzalloc_obj(obj)	kzalloc(sizeof(obj), GFP_KERNEL)
#endif

#endif /* B43_NEXT_COMPAT_H_ */
