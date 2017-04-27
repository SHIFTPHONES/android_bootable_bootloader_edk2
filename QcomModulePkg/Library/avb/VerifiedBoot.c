/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "VerifiedBoot.h"
#include "BootLinux.h"
#include <Library/VerifiedBootMenu.h>

STATIC CONST CHAR8 *VerityMode = " androidboot.veritymode=";
STATIC CONST CHAR8 *VerifiedState = " androidboot.verifiedbootstate=";
STATIC CONST CHAR8 *KeymasterLoadState = " androidboot.keymaster=1";
STATIC CONST CHAR8 *Space = " ";
STATIC struct verified_boot_verity_mode VbVm[] =
{
	{FALSE, "logging"},
	{TRUE, "enforcing"},
};
STATIC struct verified_boot_state_name VbSn[] =
{
	{GREEN, "green"},
	{ORANGE, "orange"},
	{YELLOW, "yellow"},
	{RED, "red"},
};

struct boolean_string
{
	BOOLEAN value;
	CHAR8 *name;
};

STATIC struct boolean_string BooleanString[] =
{
	{FALSE, "false"},
	{TRUE, "true"}
};

UINT32 GetAVBVersion()
{
#if VERIFIED_BOOT
	return 1;
#else
	return 0;
#endif
}

BOOLEAN VerifiedBootEnbled()
{
	return (GetAVBVersion() > NO_AVB);
}

STATIC EFI_STATUS AppendVBCmdLine(BootInfo *Info, CONST CHAR8 *Src)
{
	EFI_STATUS Status = EFI_SUCCESS;
	INT32 SrcLen = AsciiStrLen(Src);
	CHAR8 *Dst = Info->VBCmdLine + Info->VBCmdLineFilledLen;
	INT32 DstLen = Info->VBCmdLineLen - Info->VBCmdLineFilledLen;

	GUARD(AsciiStrnCatS(Dst, DstLen, Src, SrcLen));
	Info->VBCmdLineFilledLen += SrcLen;

	return EFI_SUCCESS;
}

STATIC EFI_STATUS AppendVBCommonCmdLine(BootInfo *Info)
{
	EFI_STATUS Status = EFI_SUCCESS;

	GUARD(AppendVBCmdLine(Info, VerityMode));
	GUARD(AppendVBCmdLine(Info, VbVm[IsEnforcing()].name));
	if (Info->VbIntf->Revision >= QCOM_VERIFIEDBOOT_PROTOCOL_REVISION) {
		GUARD(AppendVBCmdLine(Info, VerifiedState));
		GUARD(AppendVBCmdLine(Info, VbSn[Info->BootState].name));
	}
	GUARD(AppendVBCmdLine(Info, KeymasterLoadState));
	GUARD(AppendVBCmdLine(Info, Space));
	return EFI_SUCCESS;
}

STATIC EFI_STATUS VBCommonInit(BootInfo *Info)
{
	EFI_STATUS Status = EFI_SUCCESS;
	Info->BootState = RED;

	Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL,
	                             (VOID **)&(Info->VbIntf));
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
		return Status;
	}
	/* allocate VB command line*/
	Info->VBCmdLine = AllocatePool(DTB_PAD_SIZE);
	if (Info->VBCmdLine == NULL) {
		DEBUG((EFI_D_ERROR, "VB CmdLine allocation failed!\n"));
		Status = EFI_OUT_OF_RESOURCES;
		return Status;
	}
	Info->VBCmdLineLen = DTB_PAD_SIZE;
	Info->VBCmdLineFilledLen = 0;
	Info->VBCmdLine[Info->VBCmdLineFilledLen] = '\0';

	return Status;
}

STATIC EFI_STATUS LoadImageNoAuth(BootInfo *Info)
{
	EFI_STATUS Status = EFI_SUCCESS;

	if (Info->Images[0].ImageBuffer != NULL && Info->Images[0].ImageSize > 0) {
		/* fastboot boot option image already loaded */
		return Status;
	}

	Status = LoadImage(Info->Pname, (VOID **)&(Info->Images[0].ImageBuffer),
	                   (UINT32 *)&(Info->Images[0].ImageSize));
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR,
		       "ERROR: Failed to load image from partition: %r\n", Status));
		return EFI_LOAD_ERROR;
	}
	Info->NumLoadedImages = 1;
	Info->Images[0].Name = AllocatePool(StrLen(Info->Pname) + 1);
	UnicodeStrToAsciiStr(Info->Pname, Info->Images[0].Name);
	return Status;
}

STATIC EFI_STATUS LoadImageAndAuthVB1(BootInfo *Info)
{
	EFI_STATUS Status = EFI_SUCCESS;
	CHAR8 StrPnameAscii[MAX_GPT_NAME_SIZE]; /* partition name starting with
	                                           / and no suffix */
	CHAR8 PnameAscii[MAX_GPT_NAME_SIZE];
	CHAR8 *SystemPath = NULL;
	UINT32 SystemPathLen = 0;

	GUARD(VBCommonInit(Info));
	GUARD(LoadImageNoAuth(Info));

	device_info_vb_t DevInfo_vb;
	DevInfo_vb.is_unlocked = IsUnlocked();
	DevInfo_vb.is_unlock_critical = IsUnlockCritical();
	Status = Info->VbIntf->VBDeviceInit(Info->VbIntf,
	                                    (device_info_vb_t *)&DevInfo_vb);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error during VBDeviceInit: %r\n", Status));
		return Status;
	}

	AsciiStrnCpyS(StrPnameAscii, ARRAY_SIZE(StrPnameAscii), "/", AsciiStrLen("/"));
	UnicodeStrToAsciiStr(Info->Pname, PnameAscii);
	if (Info->MultiSlotBoot) {
		AsciiStrnCatS(StrPnameAscii, ARRAY_SIZE(StrPnameAscii), PnameAscii,
		              AsciiStrLen(PnameAscii) - (MAX_SLOT_SUFFIX_SZ - 1));
	} else {
		AsciiStrnCatS(StrPnameAscii, ARRAY_SIZE(StrPnameAscii),
		              PnameAscii, AsciiStrLen(PnameAscii));
	}

	Status = Info->VbIntf->VBVerifyImage(Info->VbIntf, (UINT8 *)StrPnameAscii,
	                                     (UINT8 *)Info->Images[0].ImageBuffer,
	                                     Info->Images[0].ImageSize,
	                                     &Info->BootState);
	if (Status != EFI_SUCCESS || Info->BootState == BOOT_STATE_MAX) {
		DEBUG((EFI_D_ERROR, "VBVerifyImage failed with: %r\n", Status));
		return Status;
	}

	Status = Info->VbIntf->VBSendRot(Info->VbIntf);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error sending Rot : %r\n", Status));
		return Status;
	}

	SystemPathLen = GetSystemPath(&SystemPath);
	if (SystemPathLen == 0 || SystemPath == NULL) {
		DEBUG((EFI_D_ERROR, "GetSystemPath failed!\n"));
		return EFI_LOAD_ERROR;
	}
	GUARD(AppendVBCommonCmdLine(Info));
	GUARD(AppendVBCmdLine(Info, SystemPath));

	return Status;
}

STATIC EFI_STATUS DisplayVerifiedBootScreen(BootInfo *Info)
{
	EFI_STATUS Status = EFI_SUCCESS;
	CHAR8 FfbmStr[FFBM_MODE_BUF_SIZE] = {'\0'};

	if (GetAVBVersion() < AVB_1) {
		return EFI_SUCCESS;
	}

	if (!StrnCmp(Info->Pname, L"boot", StrLen(L"boot"))) {
		Status = GetFfbmCommand(FfbmStr, FFBM_MODE_BUF_SIZE);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_VERBOSE,
			       "No Ffbm cookie found, ignore: %r\n", Status));
			FfbmStr[0] = '\0';
		}
	}

	DEBUG((EFI_D_VERBOSE, "Boot State is : %d\n", Info->BootState));
	switch (Info->BootState) {
	case RED:
		DisplayVerifiedBootMenu(DISPLAY_MENU_RED);
		MicroSecondDelay(5000000);
		ShutdownDevice();
		break;
	case YELLOW:
		DisplayVerifiedBootMenu(DISPLAY_MENU_YELLOW);
		MicroSecondDelay(5000000);
		break;
	case ORANGE:
		if (FfbmStr[0] == '\0') {
			DisplayVerifiedBootMenu(DISPLAY_MENU_ORANGE);
			MicroSecondDelay(5000000);
		}
		break;
	default:
		break;
	}
	return EFI_SUCCESS;
}

EFI_STATUS LoadImageAndAuth(BootInfo *Info)
{
	EFI_STATUS Status = EFI_SUCCESS;
	BOOLEAN MdtpActive = FALSE;
	QCOM_MDTP_PROTOCOL *MdtpProtocol;
	UINT32 AVBVersion = NO_AVB;

	if (Info == NULL) {
		DEBUG((EFI_D_ERROR, "Invalid parameter Info\n"));
		return EFI_INVALID_PARAMETER;
	}

	/* Get Partition Name*/
	if (!Info->MultiSlotBoot) {
		if (Info->BootIntoRecovery) {
			DEBUG((EFI_D_INFO, "Booting Into Recovery Mode\n"));
			StrnCpyS(Info->Pname, ARRAY_SIZE(Info->Pname),
			         L"recovery", StrLen(L"recovery"));
		} else {
			DEBUG((EFI_D_INFO, "Booting Into Mission Mode\n"));
			StrnCpyS(Info->Pname, ARRAY_SIZE(Info->Pname), L"boot",
			         StrLen(L"boot"));
		}
	} else {
		FindBootableSlot(Info->BootableSlot,
		                 ARRAY_SIZE(Info->BootableSlot) - 1);
		if (!Info->BootableSlot[0]) {
			DEBUG((EFI_D_ERROR, "No bootable slot\n"));
			return EFI_LOAD_ERROR;
		}
		StrnCpyS(Info->Pname, ARRAY_SIZE(Info->Pname),
		         Info->BootableSlot, StrLen(Info->BootableSlot));
	}

	DEBUG((EFI_D_VERBOSE, "MultiSlot %a, partition name %s\n",
	       BooleanString[Info->MultiSlotBoot].name, Info->Pname));

	if (FixedPcdGetBool(EnableMdtpSupport)) {
		Status = IsMdtpActive(&MdtpActive);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR,
			       "Failed to get activation state for MDTP, "
			       "Status=%r."
			       " Considering MDTP as active and continuing \n",
			       Status));
			if (Status != EFI_NOT_FOUND)
				MdtpActive = TRUE;
		}
	}

	AVBVersion = GetAVBVersion();
	DEBUG((EFI_D_VERBOSE, "AVB version %d\n", AVBVersion));

	/* Load and Authenticate */
	switch (AVBVersion) {
	case NO_AVB:
		return LoadImageNoAuth(Info);
		break;
	case AVB_1:
		Status = LoadImageAndAuthVB1(Info);
		break;
	default:
		DEBUG((EFI_D_ERROR, "Unsupported AVB version %d\n", AVBVersion));
		Status = EFI_UNSUPPORTED;
	}

	// if MDTP is active Display Recovery UI
	if (Status != EFI_SUCCESS && MdtpActive) {
		Status = gBS->LocateProtocol(&gQcomMdtpProtocolGuid, NULL,
		                             (VOID **)&MdtpProtocol);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR,
			       "Failed to locate MDTP protocol, Status=%r\n", Status));
			return Status;
		}
		/* Perform Local Deactivation of MDTP */
		Status = MdtpProtocol->MdtpDeactivate(MdtpProtocol, FALSE);
	}

	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "LoadImageAndAuth failed %r\n", Status));
		return Status;
	}

	DisplayVerifiedBootScreen(Info);

	DEBUG((EFI_D_VERBOSE, "Sending Milestone Call\n"));
	Status = Info->VbIntf->VBSendMilestone(Info->VbIntf);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error sending milestone call to TZ\n"));
		return Status;
	}

	return Status;
}

VOID FreeVerifiedBootResource(BootInfo *Info)
{
	DEBUG((EFI_D_VERBOSE, "FreeVerifiedBootResource\n"));
	if (Info->VBCmdLine != NULL) {
		FreePool(Info->VBCmdLine);
	}
	return;
}
