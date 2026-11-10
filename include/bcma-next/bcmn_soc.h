/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCMN_SOC_H_
#define LINUX_BCMN_SOC_H_

#include <bcma-next/bcmn.h>

struct bcmn_soc {
	struct bcmn_bus bus;
	struct device *dev;
};

int __init bcmn_host_soc_register(struct bcmn_soc *soc);
int __init bcmn_host_soc_init(struct bcmn_soc *soc);

int bcmn_bus_register(struct bcmn_bus *bus);

#endif /* LINUX_BCMN_SOC_H_ */
