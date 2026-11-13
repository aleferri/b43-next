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

/* Kbuild looks for the kernel's typed allocators in its slab.h */
#ifndef B43_NEXT_HAVE_KZALLOC_OBJ
#define __b43n_gfp(a, gfp, ...)	gfp
#define b43n_gfp(...)		__b43n_gfp(, ##__VA_ARGS__, GFP_KERNEL)
#define kmalloc_obj(obj, ...)	kmalloc(sizeof(obj), b43n_gfp(__VA_ARGS__))
#define kzalloc_obj(obj, ...)	kzalloc(sizeof(obj), b43n_gfp(__VA_ARGS__))
#define kzalloc_objs(obj, n, ...) \
	kcalloc(n, sizeof(obj), b43n_gfp(__VA_ARGS__))
#define kzalloc_flex(obj, fam, n, ...) \
	kzalloc(struct_size_t(typeof(obj), fam, n), b43n_gfp(__VA_ARGS__))
#endif

#endif /* B43_NEXT_COMPAT_H_ */
