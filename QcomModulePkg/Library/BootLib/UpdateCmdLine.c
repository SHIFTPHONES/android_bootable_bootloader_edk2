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
#include "Recovery.h"
#include <Library/PrintLib.h>
#include <Library/BootLinux.h>
#include <Library/PartitionTableUpdate.h>
#include <Protocol/EFICardInfo.h>
#include <Protocol/EFIChipInfoTypes.h>
#include <Protocol/Print2.h>
#include <Protocol/EFIPmicPon.h>
#include <Protocol/EFIChargerEx.h>
#include <DeviceInfo.h>
#include <LinuxLoaderLib.h>

STATIC CONST CHAR8 *BootDeviceCmdLine = " androidboot.bootdevice=";
STATIC CONST CHAR8 *UsbSerialCmdLine = " androidboot.serialno=";
STATIC CONST CHAR8 *AndroidBootMode = " androidboot.mode=";
STATIC CONST CHAR8 *LogLevel         = " quite";
STATIC CONST CHAR8 *BatteryChgPause = " androidboot.mode=charger";
STATIC CONST CHAR8 *AuthorizedKernel = " androidboot.authorized_kernel=true";
STATIC CONST CHAR8 *MdtpActiveFlag = " mdtp";

/*Send slot suffix in cmdline with which we have booted*/
STATIC CHAR8 *AndroidSlotSuffix = " androidboot.slot_suffix=";
STATIC CHAR8 *MultiSlotCmdSuffix = " rootwait ro init=/init";
STATIC CHAR8 *SkipRamFs = " skip_initramfs";
STATIC CHAR8 *SystemPath;

/* Assuming unauthorized kernel image by default */
STATIC UINT32 AuthorizeKernelImage = 0;

/* Display command line related structures */
#define MAX_DISPLAY_CMD_LINE 256
CHAR8 DisplayCmdLine[MAX_DISPLAY_CMD_LINE];
UINTN DisplayCmdLineLen = sizeof(DisplayCmdLine);

boot_state_t BootState = BOOT_STATE_MAX;
QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf = NULL;
STATIC CONST CHAR8 *VerityMode = " androidboot.veritymode=";
STATIC CONST CHAR8 *VerifiedState = " androidboot.verifiedbootstate=";
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

/*Function that returns whether the kernel is signed
 *Currently assumed to be signed*/
STATIC BOOLEAN TargetUseSignedKernel(VOID)
{
	return TRUE;
}

STATIC EFI_STATUS TargetPauseForBatteryCharge(UINT32 *BatteryStatus)
{
	EFI_STATUS Status;
	EFI_PM_PON_REASON_TYPE PONReason;
	EFI_QCOM_PMIC_PON_PROTOCOL *PmicPonProtocol;
	EFI_QCOM_CHARGER_EX_PROTOCOL *ChgDetectProtocol;
	BOOLEAN ChgPresent;
	BOOLEAN WarmRtStatus;
	BOOLEAN IsColdBoot;

	/* Determines whether to pause for batter charge,
	 * Serves only performance purposes, defaults to return zero*/
	*BatteryStatus = 0;

	Status = gBS->LocateProtocol(&gQcomPmicPonProtocolGuid, NULL,
			(VOID **) &PmicPonProtocol);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error locating pmic pon protocol: %r\n", Status));
		return Status;
	}

	/* Passing 0 for PMIC device Index since the protocol infers internally */
	Status = PmicPonProtocol->GetPonReason(0, &PONReason);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting pon reason: %r\n", Status));
		return Status;
	}

	Status = PmicPonProtocol->WarmResetStatus(0, &WarmRtStatus);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting warm reset status: %r\n", Status));
		return Status;
	}

	IsColdBoot = !WarmRtStatus;
	Status = gBS->LocateProtocol(&gQcomChargerExProtocolGuid, NULL, (void **) &ChgDetectProtocol);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error locating charger detect protocol: %r\n", Status));
		return Status;
	}

	Status = ChgDetectProtocol->GetChargerPresence(&ChgPresent);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting charger info: %r\n", Status));
		return Status;
	}

	DEBUG((EFI_D_INFO, " PON Reason is %d cold_boot:%d charger path: %d\n",
		PONReason, IsColdBoot, ChgPresent));
	/* In case of fastboot reboot,adb reboot or if we see the power key
	 * pressed we do not want go into charger mode.
	 * fastboot/adb reboot is warm boot with PON hard reset bit set.
	 */
	if (IsColdBoot &&
		(!(PONReason.HARD_RESET) &&
		(!(PONReason.KPDPWR)) &&
		(PONReason.PON1) &&
		(ChgPresent)))
	{
		*BatteryStatus = 1;
	} else {
		*BatteryStatus = 0;
	}

	return Status;
}

/**
  Check battery status
  @param[out] BatteryPresent  The pointer to battry's presence status.
  @param[out] ChargerPresent  The pointer to battry's charger status.
  @param[out] BatteryVoltage  The pointer to battry's voltage.
  @retval     EFI_SUCCESS     Check battery status successfully.
  @retval     other           Failed to check battery status.
**/
STATIC EFI_STATUS TargetCheckBatteryStatus(BOOLEAN *BatteryPresent, BOOLEAN *ChargerPresent,
	UINT32 *BatteryVoltage)
{
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_QCOM_CHARGER_EX_PROTOCOL *ChgDetectProtocol;

	Status = gBS->LocateProtocol(&gQcomChargerExProtocolGuid, NULL, (void **) &ChgDetectProtocol);
	if (EFI_ERROR(Status) || (NULL == ChgDetectProtocol)) {
		DEBUG((EFI_D_ERROR, "Error locating charger detect protocol\n"));
		return EFI_PROTOCOL_ERROR;
	}

	Status = ChgDetectProtocol->GetBatteryPresence(BatteryPresent);
	if (EFI_ERROR(Status)) {
		/* Not critical. Hence, loglevel priority is low*/
		DEBUG((EFI_D_VERBOSE, "Error getting battery presence: %r\n", Status));
		return Status;
	}

	Status = ChgDetectProtocol->GetBatteryVoltage(BatteryVoltage);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting battery voltage: %r\n", Status));
		return Status;
	}

	Status = ChgDetectProtocol->GetChargerPresence(ChargerPresent);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting charger presence: %r\n", Status));
		return Status;
	}

	return Status;
}

/**
   Add safeguards such as refusing to flash if the battery levels is lower than the min voltage
   or bypass if the battery is not present.
   @param[out] BatteryVoltage  The pointer to battry's voltage.
   @retval     BOOLEAN         The value whether the device is allowed to flash image.
 **/
BOOLEAN TargetBatterySocOk(UINT32  *BatteryVoltage)
{
	EFI_STATUS  BatteryStatus;
	BOOLEAN BatteryPresent = FALSE;
	BOOLEAN ChargerPresent = FALSE;

	BatteryStatus = TargetCheckBatteryStatus(&BatteryPresent, &ChargerPresent, BatteryVoltage);
	if ((BatteryStatus == EFI_SUCCESS) &&
		(!BatteryPresent || (BatteryPresent && (*BatteryVoltage > BATT_MIN_VOLT))))
	{
		return TRUE;
	}

	return FALSE;
}

VOID GetDisplayCmdline()
{
	EFI_STATUS Status;

	Status = gRT->GetVariable(
			L"DisplayPanelConfiguration",
			&gQcomTokenSpaceGuid,
			NULL,
			&DisplayCmdLineLen,
			DisplayCmdLine);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Unable to get Panel Config, %r\n", Status));
	}
}

/*
 * Returns length = 0 when there is failure.
 */
STATIC UINT32 GetSystemPath(CHAR8 **SysPath)
{
	INT32 Index;
	UINT32 Lun;
	CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
	CHAR16* CurSlotSuffix = GetCurrentSlotSuffix();
	CHAR8 LunCharMapping[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
	CHAR8 RootDevStr[BOOT_DEV_NAME_SIZE_MAX];

	*SysPath = AllocatePool(sizeof(char) * MAX_PATH_SIZE);
	if (!*SysPath) {
		DEBUG((EFI_D_ERROR, "Failed to allocated memory for System path query\n"));
		return 0;
	}

	StrnCpyS(PartitionName, StrLen(L"system") + 1, L"system", StrLen(L"system"));
	StrnCatS(PartitionName, MAX_GPT_NAME_SIZE - 1, CurSlotSuffix, StrLen(CurSlotSuffix));

	Index = GetPartitionIndex(PartitionName);
	if (Index == INVALID_PTN || Index >= MAX_NUM_PARTITIONS) {
		DEBUG((EFI_D_ERROR, "System partition does not exit\n"));
		FreePool(*SysPath);
		return 0;
	}

	Lun = GetPartitionLunFromIndex(Index);
	GetRootDeviceType(RootDevStr, BOOT_DEV_NAME_SIZE_MAX);
	if (!AsciiStrCmp("Unknown", RootDevStr)) {
		FreePool(*SysPath);
		return 0;
	}

	if (!AsciiStrCmp("EMMC", RootDevStr))
		AsciiSPrint(*SysPath, MAX_PATH_SIZE, " root=/dev/mmcblk0p%d", (Index + 1));
	else
		AsciiSPrint(*SysPath, MAX_PATH_SIZE, " root=/dev/sd%c%d", LunCharMapping[Lun],
				GetPartitionIdxInLun(PartitionName, Lun));

	DEBUG((EFI_D_VERBOSE, "System Path - %a \n", *SysPath));

	return AsciiStrLen(*SysPath);
}


/*Update command line: appends boot information to the original commandline
 *that is taken from boot image header*/
EFI_STATUS UpdateCmdLine(CONST CHAR8 * CmdLine,
				CHAR8 *FfbmStr,
				DeviceInfo *DeviceInfo,
				BOOLEAN Recovery,
				CHAR8 **FinalCmdLine)
{
	EFI_STATUS Status;
	UINT32 CmdLineLen = 0;
	UINT32 HaveCmdLine = 0;
	UINT32 SysPathLength = 0;
	UINT32 PauseAtBootUp = 0;
	CHAR8 SlotSuffixAscii[MAX_SLOT_SUFFIX_SZ];
	BOOLEAN MultiSlotBoot;
	CHAR8 ChipBaseBand[CHIP_BASE_BAND_LEN];
	CHAR8 *BootDevBuf = NULL;
	UINT32 BatteryStatus;
	CHAR8 StrSerialNum[SERIAL_NUM_SIZE];
	BOOLEAN MdtpActive = FALSE;

	Status = BoardSerialNum(StrSerialNum, sizeof(StrSerialNum));
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Error Finding board serial num: %x\n", Status));
		return Status;
	}

	if (CmdLine && CmdLine[0]) {
		CmdLineLen = AsciiStrLen(CmdLine);
		HaveCmdLine= 1;
	}

	if (FixedPcdGetBool(EnableMdtpSupport)) {
		Status = IsMdtpActive(&MdtpActive);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "Failed to get activation state for MDTP, Status=%r. Considering MDTP as active\n", Status));
			MdtpActive = TRUE;
		}
	}

	if (VerifiedBootEnbled()) {
		if (DeviceInfo == NULL) {
			DEBUG((EFI_D_ERROR, "DeviceInfo is NULL\n"));
			return EFI_INVALID_PARAMETER;
		}

		CmdLineLen += AsciiStrLen(VerityMode);
		CmdLineLen += AsciiStrLen(VbVm[DeviceInfo->verity_mode].name);
		Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid,
				     NULL, (VOID **) &VbIntf);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Unable to locate VerifiedBoot Protocol to update cmdline\n"));
			return Status;
		}

		if (VbIntf->Revision >= QCOM_VERIFIEDBOOT_PROTOCOL_REVISION) {
			Status = VbIntf->VBGetBootState(VbIntf, &BootState);
			if (Status != EFI_SUCCESS) {
				DEBUG((EFI_D_ERROR, "Failed to read boot state to update cmdline\n"));
				return Status;
			}
			CmdLineLen += AsciiStrLen(VerifiedState) +
				AsciiStrLen(VbSn[BootState].name);
		}
	}

	CmdLineLen += AsciiStrLen(BootDeviceCmdLine);

	BootDevBuf = AllocatePool(sizeof(CHAR8) * BOOT_DEV_MAX_LEN);
	if (BootDevBuf == NULL) {
		DEBUG((EFI_D_ERROR, "Boot device buffer: Out of resources\n"));
		return EFI_OUT_OF_RESOURCES;
	}

	Status = GetBootDevice(BootDevBuf, BOOT_DEV_MAX_LEN);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Failed to get Boot Device: %r\n", Status));
		FreePool(BootDevBuf);
		return Status;
	}

	CmdLineLen += AsciiStrLen(BootDevBuf);

	CmdLineLen += AsciiStrLen(UsbSerialCmdLine);
	CmdLineLen += AsciiStrLen(StrSerialNum);

	/* Ignore the EFI_STATUS return value as the default Battery Status = 0 and is not fatal */
	TargetPauseForBatteryCharge(&BatteryStatus);

	if (FfbmStr && FfbmStr[0] != '\0') {
		CmdLineLen += AsciiStrLen(AndroidBootMode);
		CmdLineLen += AsciiStrLen(FfbmStr);
		/* reduce kernel console messages to speed-up boot */
		CmdLineLen += AsciiStrLen(LogLevel);
	} else if (BatteryStatus && DeviceInfo->is_charger_screen_enabled) {
		DEBUG((EFI_D_INFO, "Device will boot into off mode charging mode\n"));
		PauseAtBootUp = 1;
		CmdLineLen += AsciiStrLen(BatteryChgPause);
	}

	if(TargetUseSignedKernel() && AuthorizeKernelImage) {
		CmdLineLen += AsciiStrLen(AuthorizedKernel);
	}

	if (NULL == BoardPlatformChipBaseBand()) {
		DEBUG((EFI_D_ERROR, "Invalid BaseBand String\n"));
		return EFI_NOT_FOUND;
	}

	CmdLineLen += AsciiStrLen(BOOT_BASE_BAND);
	CmdLineLen += AsciiStrLen(BoardPlatformChipBaseBand());

	if (MdtpActive)
		CmdLineLen += AsciiStrLen(MdtpActiveFlag);

	MultiSlotBoot = PartitionHasMultiSlot(L"boot");
	if(MultiSlotBoot) {
		CmdLineLen += AsciiStrLen(AndroidSlotSuffix) + 2;

		CmdLineLen += AsciiStrLen(MultiSlotCmdSuffix);

		if (!Recovery)
			CmdLineLen += AsciiStrLen(SkipRamFs);

		SysPathLength = GetSystemPath(&SystemPath);
		if (!SysPathLength)
			return EFI_NOT_FOUND;
		CmdLineLen += SysPathLength;
	}

	GetDisplayCmdline();
	CmdLineLen += AsciiStrLen(DisplayCmdLine);

#define STR_COPY(Dst,Src)  {while (*Src){*Dst = *Src; ++Src; ++Dst; } *Dst = 0; ++Dst;}
	if (CmdLineLen > 0) {
		CONST CHAR8 *Src;
		CHAR8* Dst;

		Dst = AllocatePool (CmdLineLen + 4);
		if (!Dst) {
			DEBUG((EFI_D_ERROR, "CMDLINE: Failed to allocate destination buffer\n"));
			return EFI_OUT_OF_RESOURCES;
		}

		SetMem(Dst, CmdLineLen + 4, 0x0);

		/* Save start ptr for debug print */
		*FinalCmdLine = Dst;

		if (HaveCmdLine) {
			Src = CmdLine;
			STR_COPY(Dst,Src);
		}

		if (VerifiedBootEnbled()) {
			Src = VerityMode;
			--Dst;
			STR_COPY(Dst,Src);
			--Dst;
			Src = VbVm[DeviceInfo->verity_mode].name;
			STR_COPY(Dst,Src);
			if (VbIntf->Revision >= QCOM_VERIFIEDBOOT_PROTOCOL_REVISION) {
				Src = VerifiedState;
				--Dst;
				STR_COPY(Dst,Src);
				--Dst;
				Src = VbSn[BootState].name;
				STR_COPY(Dst,Src);
			}
		}


		Src = BootDeviceCmdLine;
		if (HaveCmdLine) --Dst;
		HaveCmdLine = 1;
		STR_COPY(Dst,Src);

		Src = BootDevBuf;
		if (HaveCmdLine) --Dst;
		HaveCmdLine = 1;
		STR_COPY(Dst,Src);
		FreePool(BootDevBuf);

		Src = UsbSerialCmdLine;
		if (HaveCmdLine) --Dst;
		HaveCmdLine = 1;
		STR_COPY(Dst,Src);
		if (HaveCmdLine) --Dst;
		HaveCmdLine = 1;
		Src = StrSerialNum;
		STR_COPY(Dst,Src);

		if (FfbmStr && FfbmStr[0] != '\0') {
			Src = AndroidBootMode;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);

			Src = FfbmStr;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);

			Src = LogLevel;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);
		} else if (PauseAtBootUp) {
			Src = BatteryChgPause;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);
		}

		if(TargetUseSignedKernel() && AuthorizeKernelImage) {
			Src = AuthorizedKernel;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);
		}

		Src = BOOT_BASE_BAND;
		if (HaveCmdLine) --Dst;
		STR_COPY(Dst,Src);
		--Dst;

		SetMem(ChipBaseBand, CHIP_BASE_BAND_LEN, 0);
		AsciiStrnCpyS(ChipBaseBand, CHIP_BASE_BAND_LEN, BoardPlatformChipBaseBand(), CHIP_BASE_BAND_LEN-1);
		ToLower(ChipBaseBand);
		Src = ChipBaseBand;
		STR_COPY(Dst,Src);

		Src = DisplayCmdLine;
		if (HaveCmdLine) --Dst;
		STR_COPY(Dst,Src);

		if (MdtpActive) {
			Src = MdtpActiveFlag;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);
		}

		if (MultiSlotBoot) {
			/* Slot suffix */
			Src = AndroidSlotSuffix;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);
			--Dst;

			UnicodeStrToAsciiStr(GetCurrentSlotSuffix(), SlotSuffixAscii);
			Src = SlotSuffixAscii;
			STR_COPY(Dst,Src);

			/* Skip Initramfs*/
			if (!Recovery) {
				Src = SkipRamFs;
				if (HaveCmdLine) --Dst;
				STR_COPY(Dst, Src);
			}

			/*Add Multi slot command line suffix*/
			Src = MultiSlotCmdSuffix;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst, Src);

			/* Suffix System path in command line*/
			if (*SystemPath) {
				Src = SystemPath;
				if (HaveCmdLine) --Dst;
				STR_COPY(Dst, Src);
			}
		}
	}

	DEBUG((EFI_D_INFO, "Cmdline: %a\n", *FinalCmdLine));

	return EFI_SUCCESS;
}
