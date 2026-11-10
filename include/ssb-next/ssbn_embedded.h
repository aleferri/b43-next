/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_SSBN_EMBEDDED_H_
#define LINUX_SSBN_EMBEDDED_H_

#include <linux/types.h>
#include <ssb-next/ssbn.h>


extern int ssbn_watchdog_timer_set(struct ssbn_bus *bus, u32 ticks);

/* Generic GPIO API */
u32 ssbn_gpio_in(struct ssbn_bus *bus, u32 mask);
u32 ssbn_gpio_out(struct ssbn_bus *bus, u32 mask, u32 value);
u32 ssbn_gpio_outen(struct ssbn_bus *bus, u32 mask, u32 value);
u32 ssbn_gpio_control(struct ssbn_bus *bus, u32 mask, u32 value);
u32 ssbn_gpio_intmask(struct ssbn_bus *bus, u32 mask, u32 value);
u32 ssbn_gpio_polarity(struct ssbn_bus *bus, u32 mask, u32 value);

#endif /* LINUX_SSBN_EMBEDDED_H_ */
