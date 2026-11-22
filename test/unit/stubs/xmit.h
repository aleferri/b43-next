/* SPDX-License-Identifier: GPL-2.0 */
/* Stub for b43/xmit.h: the PLCP rate codes, which wrap.c defines. */
#ifndef _STUB_XMIT_H
#define _STUB_XMIT_H
#include "b43.h"

u8 b43_plcp_get_ratecode_cck(const u8 bitrate);
u8 b43_plcp_get_ratecode_ofdm(const u8 bitrate);
#endif
