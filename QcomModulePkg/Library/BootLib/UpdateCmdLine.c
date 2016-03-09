/** @file UpdateCmdLine.c
 *
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **/

#include "UpdateCmdLine.h"
#include <Library/PrintLib.h>
#include <Protocol/EFICardInfo.h>
#include <Protocol/EFIChipInfoTypes.h>
#include <Protocol/Print2.h>
#include <DeviceInfo.h>

STATIC CONST CHAR8 *bootdev_cmdline = " androidboot.bootdevice=1DA4000.ufshc";
STATIC CONST CHAR8 *usb_sn_cmdline = " androidboot.serialno=";
STATIC CONST CHAR8 *androidboot_mode = " androidboot.mode=";
STATIC CONST CHAR8 *loglevel         = " quite";
STATIC CONST CHAR8 *battchg_pause = " androidboot.mode=charger";
STATIC CONST CHAR8 *auth_kernel = " androidboot.authorized_kernel=true";
//STATIC CONST CHAR8 *secondary_gpt_enable = "gpt";

STATIC CHAR8 *baseband_apq     = " androidboot.baseband=apq";
STATIC CHAR8 *baseband_msm     = " androidboot.baseband=msm";
STATIC CHAR8 *baseband_csfb    = " androidboot.baseband=csfb";
STATIC CHAR8 *baseband_svlte2a = " androidboot.baseband=svlte2a";
STATIC CHAR8 *baseband_mdm     = " androidboot.baseband=mdm";
STATIC CHAR8 *baseband_mdm2    = " androidboot.baseband=mdm2";
STATIC CHAR8 *baseband_sglte   = " androidboot.baseband=sglte";
STATIC CHAR8 *baseband_dsda    = " androidboot.baseband=dsda";
STATIC CHAR8 *baseband_dsda2   = " androidboot.baseband=dsda2";
STATIC CHAR8 *baseband_sglte2  = " androidboot.baseband=sglte2";

STATIC CHAR8 *display = " mdss_mdp.panel=1:dsi:0:qcom,mdss_dsi_sharp_wqxga_video:1:qcom,mdss_dsi_sharp_wqxga_video:cfg:split_dsi";

/* Assuming unauthorized kernel image by default */
STATIC INT32 auth_kernel_img = 0;

#if VERIFIED_BOOT
STATIC CONST CHAR8 *verity_mode = " androidboot.veritymode=";
STATIC CONST CHAR8 *verified_state = " androidboot.verifiedbootstate=";
STATIC struct verified_boot_verity_mode vbvm[] =
{
	{FALSE, "logging"},
	{TRUE, "enforcing"},
};

STATIC struct verified_boot_state_name vbsn[] =
{
	{GREEN, "green"},
	{ORANGE, "orange"},
	{YELLOW, "yellow"},
	{RED, "red"},
};
#endif

/*Function that returns value of boolean boot_into_ffbm
 *A condition at update_cmdline( ) function
 *Serves performance purpose only, hard-coded to return zero */
BOOLEAN get_ffbm(CHAR8 *ffbm, UINT32 size)
{
	return 0;
}

/*Function that returns whether the kernel is signed
 *Currently assumed to be signed*/
BOOLEAN target_use_signed_kernel(VOID)
{
	return 1;
}

/*Determine correct androidboot.baseband to use
 *Currently assumed to always be MSM*/
STATIC UINT32 TargetBaseBand()
{
	UINT32 Baseband;
	UINT32 Platform = BoardPlatformRawChipId();

	switch(Platform)
	{
		case EFICHIPINFO_ID_MSMCOBALT:
			Baseband = BASEBAND_MSM;
			break;
		default:
			DEBUG((EFI_D_ERROR, "Unsupported platform: %u\n", Platform));
			ASSERT(0);
	};

	return Baseband;
}

/*Determines whether to pause for batter charge,
 *Serves only performance purposes, defaults to return zero*/
UINT32 target_pause_for_battery_charge(VOID)
{
	return 0;
}


/*Update command line: appends boot information to the original commandline
 *that is taken from boot image header*/
UINT8 *update_cmdline(CONST CHAR8 * cmdline)
{
	EFI_STATUS Status;
	UINT32 cmdline_len = 0;
	UINT32 have_cmdline = 0;
	CHAR8  *cmdline_final = NULL;
	UINT32 pause_at_bootup = 0; //this would have to come from protocol
	//UINT32 boot_state = ORANGE;
	/*
	BOOLEAN	warm_boot = FALSE; // not needed
	BOOLEAN gpt_exists = TRUE; // this is needed
	*/
	CHAR8 ffbm[10];
	UINTN boot_into_ffbm = get_ffbm(ffbm, sizeof(ffbm));
	MEM_CARD_INFO card_info = {};
	EFI_MEM_CARDINFO_PROTOCOL *pCardInfoProtocol=NULL;
	CHAR8 StrSerialNum[64];

	Status = BoardSerialNum(StrSerialNum, sizeof(StrSerialNum));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Finding board serial num: %x\n", Status));
		return Status;
	}

	if (cmdline && cmdline[0])
	{
		cmdline_len = AsciiStrLen(cmdline);
		have_cmdline = 1;
	}
#if VERIFIED_BOOT
	if ((device.verity_mode != 0) && (device.verity_mode != 1))
	{
		DEBUG((EFI_D_ERROR, "Devinfo partition possibly corrupted!!!. Please erase devinfo partition to continue booting.\n"));
		ASSERT(0);
	}
	cmdline_len += AsciiStrLen(verity_mode) + AsciiStrLen(vbvm[device.verity_mode]);
#endif

	cmdline_len += AsciiStrLen(bootdev_cmdline);

	cmdline_len += AsciiStrLen(usb_sn_cmdline);
	cmdline_len += AsciiStrLen(StrSerialNum);

	if (boot_into_ffbm)
	{
		cmdline_len += AsciiStrLen(androidboot_mode);
		cmdline_len += AsciiStrLen(ffbm);
		/* reduce kernel console messages to speed-up boot */
		cmdline_len += AsciiStrLen(loglevel);
	}
	else if (target_pause_for_battery_charge())
	{
		pause_at_bootup = 1;
		cmdline_len += AsciiStrLen(battchg_pause);
	}

	if(target_use_signed_kernel() && auth_kernel_img)
	{
		cmdline_len += AsciiStrLen(auth_kernel);
	}

	/* Determine correct androidboot.baseband to use */
	switch(TargetBaseBand())
	{
		case BASEBAND_APQ:
			cmdline_len += AsciiStrLen(baseband_apq);
			break;

		case BASEBAND_MSM:
			cmdline_len += AsciiStrLen(baseband_msm);
			break;

		case BASEBAND_CSFB:
			cmdline_len += AsciiStrLen(baseband_csfb);
			break;

		case BASEBAND_SVLTE2A:
			cmdline_len += AsciiStrLen(baseband_svlte2a);
			break;
		case BASEBAND_MDM:
			cmdline_len += AsciiStrLen(baseband_mdm);
			break;
		case BASEBAND_MDM2:
			cmdline_len += AsciiStrLen(baseband_mdm2);
			break;
		case BASEBAND_SGLTE:
			cmdline_len += AsciiStrLen(baseband_sglte);
			break;

		case BASEBAND_SGLTE2:
			cmdline_len += AsciiStrLen(baseband_sglte2);
			break;

		case BASEBAND_DSDA:
			cmdline_len += AsciiStrLen(baseband_dsda);
			break;

		case BASEBAND_DSDA2:
			cmdline_len += AsciiStrLen(baseband_dsda2);
			break;
	}

	cmdline_len += AsciiStrLen(display);

#define STR_COPY(dst,src)  {while (*src){*dst = *src; ++src; ++dst; } *dst = 0; ++dst;}
	if (cmdline_len > 0)
	{
		CONST CHAR8 *src;

		CHAR8* dst;
		dst = AllocatePool (cmdline_len + 4);
		//ASSERT(dst != NULL);

		/* Save start ptr for debug print */
		cmdline_final = dst;
		if (have_cmdline)
		{
			src = cmdline;
			STR_COPY(dst,src);
		}

		src = bootdev_cmdline;
		if (have_cmdline) --dst;
		have_cmdline = 1;
		STR_COPY(dst,src);

		src = usb_sn_cmdline;
		if (have_cmdline) --dst;
		have_cmdline = 1;
		STR_COPY(dst,src);
		if (have_cmdline) --dst;
		have_cmdline = 1;
		src = StrSerialNum;
		STR_COPY(dst,src);

		src = loglevel;
		if (have_cmdline) --dst;
		STR_COPY(dst,src);

		if (boot_into_ffbm) {
			src = androidboot_mode;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
			src = ffbm;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
			src = loglevel;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
		} else if (pause_at_bootup) {
			src = battchg_pause;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
		}

		if(target_use_signed_kernel() && auth_kernel_img)
		{
			src = auth_kernel;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
		}

		switch(TargetBaseBand())
		{
			case BASEBAND_APQ:
				src = baseband_apq;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_MSM:
				src = baseband_msm;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_CSFB:
				src = baseband_csfb;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_SVLTE2A:
				src = baseband_svlte2a;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_MDM:
				src = baseband_mdm;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_MDM2:
				src = baseband_mdm2;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_SGLTE:
				src = baseband_sglte;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_SGLTE2:
				src = baseband_sglte2;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_DSDA:
				src = baseband_dsda;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;

			case BASEBAND_DSDA2:
				src = baseband_dsda2;
				if (have_cmdline) --dst;
				STR_COPY(dst,src);
				break;
		}

		src = display;
		if (have_cmdline) --dst;
		STR_COPY(dst,src);

	}

	return (UINT8 *)cmdline_final;
}
