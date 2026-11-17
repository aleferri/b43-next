/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_PHY_LCN_H_
#define B43_PHY_LCN_H_

#include "phy_common.h"


#define B43_PHY_LCN_AFE_CTL1			B43_PHY_OFDM(0x03B)
#define B43_PHY_LCN_AFE_CTL2			B43_PHY_OFDM(0x03C)
#define B43_PHY_LCN_RF_CTL1			B43_PHY_OFDM(0x04C)
#define B43_PHY_LCN_RF_CTL2			B43_PHY_OFDM(0x04D)
#define B43_PHY_LCN_TABLE_ADDR			B43_PHY_OFDM(0x055) /* Table address */
#define B43_PHY_LCN_TABLE_DATALO		B43_PHY_OFDM(0x056) /* Table data low */
#define B43_PHY_LCN_TABLE_DATAHI		B43_PHY_OFDM(0x057) /* Table data high */
#define B43_PHY_LCN_RF_CTL3			B43_PHY_OFDM(0x0B0)
#define B43_PHY_LCN_RF_CTL4			B43_PHY_OFDM(0x0B1)
#define B43_PHY_LCN_RF_CTL5			B43_PHY_OFDM(0x0B7)
#define B43_PHY_LCN_RF_CTL6			B43_PHY_OFDM(0x0F9)
#define B43_PHY_LCN_RF_CTL7			B43_PHY_OFDM(0x0FA)


#define B43_LCN_TX_PWR_CTRL_MASK		0xe000
#define B43_LCN_TX_PWR_CTRL_OFF			0x0000
#define B43_LCN_TX_PWR_CTRL_HW			0xe000
/* Where the hardware control starts from, as wl */
#define B43_LCN_TX_PWR_CTRL_START_IDX		180
#define B43_LCN_TX_PWR_CTRL_START_NPT		0
/* and again from the index, with brcmsmac's packets, when the target changes */
#define B43_LCN_TX_PWR_CTRL_RESET_NPT		1

/* The offsets from the target of the 4 CCK, 8 OFDM and 8 MCS rates */
#define B43_LCN_TX_PWR_CTRL_RATE_OFFSET		0x340
#define B43_LCN_TX_PWR_RATES			20

struct b43_phy_lcn {
	bool hw_pwr_ctl;
	bool hw_pwr_ctl_capable;
	bool temp_pwr_ctl_capable;
	u8 tx_pwr_curr_idx;
	s8 tx_pwr_idx_override;

	/* The last estimate of the TX power control: index, packets, count */
	u16 tssi_idx;
	u8 tssi_npt;
	u16 tssi_tx_cnt;

	/* The target, 0 until the first recalculation, and the rates' offsets */
	u8 tx_pwr_target;
	s8 tx_pwr_offset[B43_LCN_TX_PWR_RATES];

	/* The init's calibration, which a channel switch loads again */
	bool cal_done;
	s16 txiqcc[2];
	u16 txdidq;
	u16 loft[4];
	u16 rxiqcomp[2];
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_lcn;

#endif /* B43_PHY_LCN_H_ */
