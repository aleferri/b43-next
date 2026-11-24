// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  Loading the firmware files and uploading them to the core

  Moved out of main.c; the code and its copyright are main.c's:

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2009 Michael Buesch <m@bues.ch>
  Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (c) 2010-2011 Rafał Miłecki <zajec5@gmail.com>
  Copyright (c) 2026 Alessio Ferri <alessio.ferri.3012@gmail.com>
  Copyright (c) 2026 Andreas Wendleder <andreas.wendleder@proton.me>
  Copyright (c) 2026 Ville Takio <ville+git@takio.fi>

*/

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#include "b43.h"
#include "main.h"
#include "fw.h"

static char modparam_fwpostfix[16];
module_param_string(fwpostfix, modparam_fwpostfix, 16, 0444);
MODULE_PARM_DESC(fwpostfix, "Postfix for the .fw files to load.");

/* Context data for fetching firmware. */
struct b43_request_fw_context {
	/* The device we are requesting the fw for. */
	struct b43_wldev *dev;
	/* a pointer to the firmware object */
	const struct firmware *blob;
	/* The type of firmware to request. */
	enum b43_firmware_file_type req_type;
	/* Error messages for each firmware type. */
	char errors[B43_NR_FWTYPES][128];
	/* Temporary buffer for storing the firmware name. */
	char fwname[64];
	/* A fatal error occurred while requesting. Firmware request
	 * can not continue, as any other request will also fail. */
	int fatal_failure;
};

/* Read the firmware capabilities bitmask (Opensource firmware only) */
static u16 b43_fwcapa_read(struct b43_wldev *dev)
{
	B43_WARN_ON(!dev->fw.opensource);
	return b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_FWCAPA);
}

static void b43_do_release_fw(struct b43_firmware_file *fw)
{
	release_firmware(fw->data);
	fw->data = NULL;
	fw->filename = NULL;
}

void b43_release_firmware(struct b43_wldev *dev)
{
	complete(&dev->fw_load_complete);
	b43_do_release_fw(&dev->fw.ucode);
	b43_do_release_fw(&dev->fw.pcm);
	b43_do_release_fw(&dev->fw.initvals);
	b43_do_release_fw(&dev->fw.initvals_band);
}

static void b43_print_fw_helptext(struct b43_wl *wl, bool error)
{
	const char text[] =
		"You must go to " \
		"https://wireless.docs.kernel.org/en/latest/en/users/drivers/b43/developers.html#list-of-firmware " \
		"and download the correct firmware for this driver version. " \
		"Please carefully read all instructions on this website.\n";

	if (error)
		b43err(wl, text);
	else
		b43warn(wl, text);
}

static void b43_fw_cb(const struct firmware *firmware, void *context)
{
	struct b43_request_fw_context *ctx = context;

	ctx->blob = firmware;
	complete(&ctx->dev->fw_load_complete);
}

static int b43_do_request_fw(struct b43_request_fw_context *ctx,
			     const char *name,
			     struct b43_firmware_file *fw, bool async)
{
	struct b43_fw_header *hdr;
	u32 size;
	int err;

	if (!name) {
		/* Don't fetch anything. Free possibly cached firmware. */
		/* FIXME: We should probably keep it anyway, to save some headache
		 * on suspend/resume with multiband devices. */
		b43_do_release_fw(fw);
		return 0;
	}
	if (fw->filename) {
		if ((fw->type == ctx->req_type) &&
		    (strcmp(fw->filename, name) == 0))
			return 0; /* Already have this fw. */
		/* Free the cached firmware first. */
		/* FIXME: We should probably do this later after we successfully
		 * got the new fw. This could reduce headache with multiband devices.
		 * We could also redesign this to cache the firmware for all possible
		 * bands all the time. */
		b43_do_release_fw(fw);
	}

	switch (ctx->req_type) {
	case B43_FWTYPE_PROPRIETARY:
		snprintf(ctx->fwname, sizeof(ctx->fwname),
			 "b43%s/%s.fw",
			 modparam_fwpostfix, name);
		break;
	case B43_FWTYPE_OPENSOURCE:
		snprintf(ctx->fwname, sizeof(ctx->fwname),
			 "b43-open%s/%s.fw",
			 modparam_fwpostfix, name);
		break;
	default:
		B43_WARN_ON(1);
		return -ENOSYS;
	}
	if (async) {
		/* do this part asynchronously */
		init_completion(&ctx->dev->fw_load_complete);
		err = request_firmware_nowait(THIS_MODULE, 1, ctx->fwname,
					      ctx->dev->dev->dev, GFP_KERNEL,
					      ctx, b43_fw_cb);
		if (err < 0) {
			pr_err("Unable to load firmware\n");
			return err;
		}
		wait_for_completion(&ctx->dev->fw_load_complete);
		if (ctx->blob)
			goto fw_ready;
	/* On some ARM systems, the async request will fail, but the next sync
	 * request works. For this reason, we fall through here
	 */
	}
	err = request_firmware(&ctx->blob, ctx->fwname,
			       ctx->dev->dev->dev);
	if (err == -ENOENT) {
		snprintf(ctx->errors[ctx->req_type],
			 sizeof(ctx->errors[ctx->req_type]),
			 "Firmware file \"%s\" not found\n",
			 ctx->fwname);
		return err;
	} else if (err) {
		snprintf(ctx->errors[ctx->req_type],
			 sizeof(ctx->errors[ctx->req_type]),
			 "Firmware file \"%s\" request failed (err=%d)\n",
			 ctx->fwname, err);
		return err;
	}
fw_ready:
	if (ctx->blob->size < sizeof(struct b43_fw_header))
		goto err_format;
	hdr = (struct b43_fw_header *)(ctx->blob->data);
	switch (hdr->type) {
	case B43_FW_TYPE_UCODE:
	case B43_FW_TYPE_PCM:
		size = be32_to_cpu(hdr->size);
		if (size != ctx->blob->size - sizeof(struct b43_fw_header))
			goto err_format;
		fallthrough;
	case B43_FW_TYPE_IV:
		if (hdr->ver != 1)
			goto err_format;
		break;
	default:
		goto err_format;
	}

	fw->data = ctx->blob;
	fw->filename = name;
	fw->type = ctx->req_type;

	return 0;

err_format:
	snprintf(ctx->errors[ctx->req_type],
		 sizeof(ctx->errors[ctx->req_type]),
		 "Firmware file \"%s\" format error.\n", ctx->fwname);
	release_firmware(ctx->blob);

	return -EPROTO;
}

/* https://bcm-v4.sipsolutions.net/802.11/Init/Firmware */
static int b43_try_request_fw(struct b43_request_fw_context *ctx)
{
	struct b43_wldev *dev = ctx->dev;
	struct b43_firmware *fw = &ctx->dev->fw;
	struct b43_phy *phy = &dev->phy;
	const u8 rev = ctx->dev->dev->core_rev;
	const char *filename;
	int err;

	/* Get microcode */
	fw->family = b43_fw_family_find(rev, phy->type);
	if (!fw->family)
		goto err_no_ucode;
	err = b43_do_request_fw(ctx, fw->family->ucode, &fw->ucode, true);
	if (err)
		goto err_load;

	/* Get PCM code */
	if ((rev >= 5) && (rev <= 10))
		filename = "pcm5";
	else if (rev >= 11)
		filename = NULL;
	else
		goto err_no_pcm;
	fw->pcm_request_failed = false;
	err = b43_do_request_fw(ctx, filename, &fw->pcm, false);
	if (err == -ENOENT) {
		/* We did not find a PCM file? Not fatal, but
		 * core rev <= 10 must do without hwcrypto then. */
		fw->pcm_request_failed = true;
	} else if (err)
		goto err_load;

	/* Get initvals */
	filename = NULL;
	switch (dev->phy.type) {
	case B43_PHYTYPE_G:
		if (rev == 13)
			filename = "b0g0initvals13";
		else if (rev >= 5 && rev <= 10)
			filename = "b0g0initvals5";
		break;
	case B43_PHYTYPE_N:
		if (rev == 30)
			filename = "n16initvals30";
		else if (rev == 28 || rev == 25)
			filename = "n0initvals25";
		else if (rev == 24)
			filename = "n0initvals24";
		else if (rev == 23)
			filename = "n0initvals16";
		else if (rev == 22)
			filename = "n0initvals22";
		else if (rev >= 16 && rev <= 18)
			filename = "n0initvals16";
		else if (rev >= 11 && rev <= 12)
			filename = "n0initvals11";
		break;
	case B43_PHYTYPE_LP:
		if (rev >= 16 && rev <= 18)
			filename = "lp0initvals16";
		else if (rev == 15)
			filename = "lp0initvals15";
		else if (rev == 14)
			filename = "lp0initvals14";
		else if (rev == 13)
			filename = "lp0initvals13";
		break;
	case B43_PHYTYPE_HT:
		if (rev == 29)
			filename = "ht0initvals29";
		else if (rev == 26)
			filename = "ht0initvals26";
		break;
	case B43_PHYTYPE_LCN:
		if (rev == 24)
			filename = "lcn0initvals24";
		break;
	case B43_PHYTYPE_LCN40:
		if (rev == 33)
			filename = "lcn400initvals33";
		break;
	case B43_PHYTYPE_AC:
		if (rev == 42)
			filename = "ac1initvals42";
		else if (rev == 40)
			filename = "ac0initvals40";
		break;
	}
	if (!filename)
		goto err_no_initvals;
	err = b43_do_request_fw(ctx, filename, &fw->initvals, false);
	if (err)
		goto err_load;

	/* Get bandswitch initvals */
	filename = NULL;
	switch (dev->phy.type) {
	case B43_PHYTYPE_G:
		if (rev == 13)
			filename = "b0g0bsinitvals13";
		else if (rev >= 5 && rev <= 10)
			filename = "b0g0bsinitvals5";
		break;
	case B43_PHYTYPE_N:
		if (rev == 30)
			filename = "n16bsinitvals30";
		else if (rev == 28 || rev == 25)
			filename = "n0bsinitvals25";
		else if (rev == 24)
			filename = "n0bsinitvals24";
		else if (rev == 23)
			filename = "n0bsinitvals16";
		else if (rev == 22)
			filename = "n0bsinitvals22";
		else if (rev >= 16 && rev <= 18)
			filename = "n0bsinitvals16";
		else if (rev >= 11 && rev <= 12)
			filename = "n0bsinitvals11";
		break;
	case B43_PHYTYPE_LP:
		if (rev >= 16 && rev <= 18)
			filename = "lp0bsinitvals16";
		else if (rev == 15)
			filename = "lp0bsinitvals15";
		else if (rev == 14)
			filename = "lp0bsinitvals14";
		else if (rev == 13)
			filename = "lp0bsinitvals13";
		break;
	case B43_PHYTYPE_HT:
		if (rev == 29)
			filename = "ht0bsinitvals29";
		else if (rev == 26)
			filename = "ht0bsinitvals26";
		break;
	case B43_PHYTYPE_LCN:
		if (rev == 24)
			filename = "lcn0bsinitvals24";
		break;
	case B43_PHYTYPE_LCN40:
		if (rev == 33)
			filename = "lcn400bsinitvals33";
		break;
	case B43_PHYTYPE_AC:
		if (rev == 42)
			filename = "ac1bsinitvals42";
		else if (rev == 40)
			filename = "ac0bsinitvals40";
		break;
	}
	if (!filename)
		goto err_no_initvals;
	err = b43_do_request_fw(ctx, filename, &fw->initvals_band, false);
	if (err)
		goto err_load;

	fw->opensource = (ctx->req_type == B43_FWTYPE_OPENSOURCE);

	return 0;

err_no_ucode:
	err = ctx->fatal_failure = -EOPNOTSUPP;
	b43err(dev->wl, "The driver does not know which firmware (ucode) "
	       "is required for your device (wl-core rev %u)\n", rev);
	goto error;

err_no_pcm:
	err = ctx->fatal_failure = -EOPNOTSUPP;
	b43err(dev->wl, "The driver does not know which firmware (PCM) "
	       "is required for your device (wl-core rev %u)\n", rev);
	goto error;

err_no_initvals:
	err = ctx->fatal_failure = -EOPNOTSUPP;
	b43err(dev->wl, "The driver does not know which firmware (initvals) "
	       "is required for your device (wl-core rev %u)\n", rev);
	goto error;

err_load:
	/* We failed to load this firmware image. The error message
	 * already is in ctx->errors. Return and let our caller decide
	 * what to do. */
	goto error;

error:
	b43_release_firmware(dev);
	return err;
}

int b43_load_firmware(struct b43_wldev *dev)
{
	struct b43_request_fw_context *ctx;
	unsigned int i;
	int err;
	const char *errmsg;

	ctx = kzalloc_obj(*ctx);
	if (!ctx)
		return -ENOMEM;
	ctx->dev = dev;

	ctx->req_type = B43_FWTYPE_PROPRIETARY;
	err = b43_try_request_fw(ctx);
	if (!err)
		goto out; /* Successfully loaded it. */
	/* Was fw version known? */
	if (ctx->fatal_failure)
		goto out;

	/* proprietary fw not found, try open source */
	ctx->req_type = B43_FWTYPE_OPENSOURCE;
	err = b43_try_request_fw(ctx);
	if (!err)
		goto out; /* Successfully loaded it. */
	if(ctx->fatal_failure)
		goto out;

	/* Could not find a usable firmware. Print the errors. */
	for (i = 0; i < B43_NR_FWTYPES; i++) {
		errmsg = ctx->errors[i];
		if (strlen(errmsg))
			b43err(dev->wl, "%s", errmsg);
	}
	b43_print_fw_helptext(dev->wl, 1);

out:
	kfree(ctx);
	return err;
}

int b43_upload_microcode(struct b43_wldev *dev)
{
	struct wiphy *wiphy = dev->wl->hw->wiphy;
	const size_t hdr_len = sizeof(struct b43_fw_header);
	const __be32 *data;
	unsigned int i, len;
	u16 fwrev, fwpatch, fwdate, fwtime;
	u32 tmp, macctl;
	int err = 0;

	/* Jump the microcode PSM to offset 0 */
	macctl = b43_read32(dev, B43_MMIO_MACCTL);
	B43_WARN_ON(macctl & B43_MACCTL_PSM_RUN);
	macctl |= B43_MACCTL_PSM_JMP0;
	b43_macctl_write(dev, macctl);
	/* Zero out all microcode PSM registers and shared memory. */
	for (i = 0; i < 64; i++)
		b43_shm_write16(dev, B43_SHM_SCRATCH, i, 0);
	for (i = 0; i < 4096; i += 2)
		b43_shm_write16(dev, B43_SHM_SHARED, i, 0);

	/* Upload Microcode. */
	data = (__be32 *) (dev->fw.ucode.data->data + hdr_len);
	len = (dev->fw.ucode.data->size - hdr_len) / sizeof(__be32);
	b43_shm_control_word(dev, B43_SHM_UCODE | B43_SHM_AUTOINC_W, 0x0000);
	for (i = 0; i < len; i++) {
		b43_write32(dev, B43_MMIO_SHM_DATA, be32_to_cpu(data[i]));
		udelay(10);
	}

	if (dev->fw.pcm.data) {
		/* Upload PCM data. */
		data = (__be32 *) (dev->fw.pcm.data->data + hdr_len);
		len = (dev->fw.pcm.data->size - hdr_len) / sizeof(__be32);
		b43_shm_control_word(dev, B43_SHM_HW, 0x01EA);
		b43_write32(dev, B43_MMIO_SHM_DATA, 0x00004000);
		/* No need for autoinc bit in SHM_HW */
		b43_shm_control_word(dev, B43_SHM_HW, 0x01EB);
		for (i = 0; i < len; i++) {
			b43_write32(dev, B43_MMIO_SHM_DATA, be32_to_cpu(data[i]));
			udelay(10);
		}
	}

	b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, B43_IRQ_ALL);

	/* Start the microcode PSM */
	b43_macctl_maskset(dev, ~B43_MACCTL_PSM_JMP0, B43_MACCTL_PSM_RUN);

	/* Wait for the microcode to load and respond */
	i = 0;
	while (1) {
		tmp = b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
		if (tmp == B43_IRQ_MAC_SUSPENDED)
			break;
		i++;
		if (i >= 20) {
			b43err(dev->wl, "Microcode not responding\n");
			b43_print_fw_helptext(dev->wl, 1);
			err = -ENODEV;
			goto error;
		}
		msleep(50);
	}
	b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);	/* dummy read */

	/* Get and check the revisions. */
	fwrev = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODEREV);
	fwpatch = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODEPATCH);
	fwdate = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODEDATE);
	fwtime = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODETIME);

	if (fwrev <= 0x128) {
		b43err(dev->wl, "YOUR FIRMWARE IS TOO OLD. Firmware from "
		       "binary drivers older than version 4.x is unsupported. "
		       "You must upgrade your firmware files.\n");
		b43_print_fw_helptext(dev->wl, 1);
		err = -EOPNOTSUPP;
		goto error;
	}
	dev->fw.rev = fwrev;
	dev->fw.patch = fwpatch;
	dev->fw.layout = *b43_fw_layout_get(dev->fw.family, fwrev);
	if (dev->fw.layout.unknown)
		b43warn(dev->wl, "Firmware %s %u unknown: headers assumed, "
			"no AP, mesh or IBSS, no MAC config block, "
			"no CTS around the calibration\n",
			dev->fw.family->ucode, dev->fw.rev);
	WARN_ON(dev->fw.opensource != (fwdate == 0xFFFF));

	dev->qos_enabled = dev->wl->hw->queues > 1;
	/* Default to firmware/hardware crypto acceleration. */
	dev->hwcrypto_enabled = true;

	if (dev->fw.opensource) {
		u16 fwcapa;

		/* Patchlevel info is encoded in the "time" field. */
		dev->fw.patch = fwtime;
		b43info(dev->wl, "Loading OpenSource firmware version %u.%u\n",
			dev->fw.rev, dev->fw.patch);

		fwcapa = b43_fwcapa_read(dev);
		if (!(fwcapa & B43_FWCAPA_HWCRYPTO) || dev->fw.pcm_request_failed) {
			b43info(dev->wl, "Hardware crypto acceleration not supported by firmware\n");
			/* Disable hardware crypto and fall back to software crypto. */
			dev->hwcrypto_enabled = false;
		}
		/* adding QoS support should use an offline discovery mechanism */
		WARN(fwcapa & B43_FWCAPA_QOS, "QoS in OpenFW not supported\n");
	} else {
		b43info(dev->wl, "Loading firmware version %u.%u "
			"(20%.2i-%.2i-%.2i %.2i:%.2i:%.2i)\n",
			fwrev, fwpatch,
			(fwdate >> 12) & 0xF, (fwdate >> 8) & 0xF, fwdate & 0xFF,
			(fwtime >> 11) & 0x1F, (fwtime >> 5) & 0x3F, fwtime & 0x1F);
		if (dev->fw.pcm_request_failed) {
			b43warn(dev->wl, "No \"pcm5.fw\" firmware file found. "
				"Hardware accelerated cryptography is disabled.\n");
			b43_print_fw_helptext(dev->wl, 0);
		}
	}

	snprintf(wiphy->fw_version, sizeof(wiphy->fw_version), "%u.%u",
			dev->fw.rev, dev->fw.patch);
	wiphy->hw_version = dev->dev->core_id;

	if (dev->fw.layout.deprecated) {
		/* We're over the deadline, but we keep support for old fw
		 * until it turns out to be in major conflict with something new. */
		b43warn(dev->wl, "You are using an old firmware image. "
			"Support for old firmware will be removed soon "
			"(official deadline was July 2008).\n");
		b43_print_fw_helptext(dev->wl, 0);
	}

	return 0;

error:
	/* Stop the microcode PSM. */
	b43_macctl_maskset(dev, ~B43_MACCTL_PSM_RUN, B43_MACCTL_PSM_JMP0);

	return err;
}

static int b43_write_initvals(struct b43_wldev *dev,
			      const struct b43_iv *ivals,
			      size_t count,
			      size_t array_size)
{
	const struct b43_iv *iv;
	u16 offset;
	size_t i;
	bool bit32;

	BUILD_BUG_ON(sizeof(struct b43_iv) != 6);
	iv = ivals;
	for (i = 0; i < count; i++) {
		if (array_size < sizeof(iv->offset_size))
			goto err_format;
		array_size -= sizeof(iv->offset_size);
		offset = be16_to_cpu(iv->offset_size);
		bit32 = !!(offset & B43_IV_32BIT);
		offset &= B43_IV_OFFSET_MASK;
		if (offset >= 0x1000)
			goto err_format;
		if (bit32) {
			u32 value;

			if (array_size < sizeof(iv->data.d32))
				goto err_format;
			array_size -= sizeof(iv->data.d32);

			value = get_unaligned_be32(&iv->data.d32);
			b43_write32(dev, offset, value);

			iv = (const struct b43_iv *)((const uint8_t *)iv +
							sizeof(__be16) +
							sizeof(__be32));
		} else {
			u16 value;

			if (array_size < sizeof(iv->data.d16))
				goto err_format;
			array_size -= sizeof(iv->data.d16);

			value = be16_to_cpu(iv->data.d16);
			b43_write16(dev, offset, value);

			iv = (const struct b43_iv *)((const uint8_t *)iv +
							sizeof(__be16) +
							sizeof(__be16));
		}
	}
	if (array_size)
		goto err_format;

	return 0;

err_format:
	b43err(dev->wl, "Initial Values Firmware file-format error.\n");
	b43_print_fw_helptext(dev->wl, 1);

	return -EPROTO;
}

int b43_upload_initvals(struct b43_wldev *dev)
{
	const size_t hdr_len = sizeof(struct b43_fw_header);
	const struct b43_fw_header *hdr;
	struct b43_firmware *fw = &dev->fw;
	const struct b43_iv *ivals;
	size_t count;

	hdr = (const struct b43_fw_header *)(fw->initvals.data->data);
	ivals = (const struct b43_iv *)(fw->initvals.data->data + hdr_len);
	count = be32_to_cpu(hdr->size);
	return b43_write_initvals(dev, ivals, count,
				 fw->initvals.data->size - hdr_len);
}

int b43_upload_initvals_band(struct b43_wldev *dev)
{
	const size_t hdr_len = sizeof(struct b43_fw_header);
	const struct b43_fw_header *hdr;
	struct b43_firmware *fw = &dev->fw;
	const struct b43_iv *ivals;
	size_t count;

	if (!fw->initvals_band.data)
		return 0;

	hdr = (const struct b43_fw_header *)(fw->initvals_band.data->data);
	ivals = (const struct b43_iv *)(fw->initvals_band.data->data + hdr_len);
	count = be32_to_cpu(hdr->size);
	return b43_write_initvals(dev, ivals, count,
				  fw->initvals_band.data->size - hdr_len);
}
