/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_FW_H_
#define B43_FW_H_

struct b43_wldev;

/* Loads the firmware files of a core into dev->fw, and frees them. */
int b43_load_firmware(struct b43_wldev *dev);
void b43_release_firmware(struct b43_wldev *dev);

/* Core init: the microcode, the initial values and those of the band. */
int b43_upload_microcode(struct b43_wldev *dev);
int b43_upload_initvals(struct b43_wldev *dev);
int b43_upload_initvals_band(struct b43_wldev *dev);

#endif /* B43_FW_H_ */
