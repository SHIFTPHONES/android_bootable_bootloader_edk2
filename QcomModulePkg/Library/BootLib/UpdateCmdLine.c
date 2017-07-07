/** @file UpdateCmdLine.c
 *
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2017, The Linux Foundation. All rights reserved.
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
STATIC CONST CHAR8 *AlarmBootCmdLine = " androidboot.alarmboot=true";

/*Send slot suffix in cmdline with which we have booted*/
STATIC CHAR8 *AndroidSlotSuffix = " androidboot.slot_suffix=";
STATIC CHAR8 *MultiSlotCmdSuffix = " rootwait ro init=/init";
STATIC CHAR8 *SkipRamFs = " skip_initramfs";

/* Assuming unauthorized kernel image by default */
STATIC UINT32 AuthorizeKernelImage = 0;

/* Display command line related structures */
#define MAX_DISPLAY_CMD_LINE 256
CHAR8 DisplayCmdLine[MAX_DISPLAY_CMD_LINE];
UINTN DisplayCmdLineLen = sizeof(DisplayCmdLine);

/*Function that returns whether the kernel is signed
 *Currently assumed to be signed*/
STATIC BOOLEAN TargetUseSignedKernel(VOID)
{
	return TRUE;
}

STATIC EFI_STATUS TargetPauseForBatteryCharge(BOOLEAN *BatteryStatus)
{
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_PM_PON_REASON_TYPE PONReason;
	EFI_QCOM_PMIC_PON_PROTOCOL *PmicPonProtocol;
	EFI_CHARGER_EX_PROTOCOL *ChgDetectProtocol;
	BOOLEAN ChgPresent;
	BOOLEAN WarmRtStatus;
	BOOLEAN IsColdBoot;

	/* Determines whether to pause for batter charge,
	 * Serves only performance purposes, defaults to return zero*/
	*BatteryStatus = 0;

	Status = gBS->LocateProtocol(&gChargerExProtocolGuid, NULL, (VOID **) &ChgDetectProtocol);
	if (EFI_ERROR(Status) || (NULL == ChgDetectProtocol)) {
		DEBUG((EFI_D_ERROR, "Error locating charger detect protocol: %r\n", Status));
		return Status;
	}

	/* The new protocol are supported on future chipsets */
	if (ChgDetectProtocol->Revision >= CHARGER_EX_REVISION) {
		Status = ChgDetectProtocol->IsOffModeCharging(BatteryStatus);
		if (EFI_ERROR(Status))
			DEBUG((EFI_D_ERROR, "Error getting off mode charging info: %r\n", Status));

		return Status;
	} else {
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
			(PONReason.PON1 || PONReason.USB_CHG) &&
			(ChgPresent)))
		{
			*BatteryStatus = 1;
		} else {
			*BatteryStatus = 0;
		}

		return Status;
	}
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
	EFI_CHARGER_EX_PROTOCOL *ChgDetectProtocol;

	Status = gBS->LocateProtocol(&gChargerExProtocolGuid, NULL, (void **) &ChgDetectProtocol);
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
   @param[out] BatteryVoltage  The current voltage of battery
   @retval     BOOLEAN         The value whether the device is allowed to flash image.
 **/
BOOLEAN TargetBatterySocOk(UINT32 *BatteryVoltage)
{
	EFI_STATUS  Status = EFI_SUCCESS;
	EFI_CHARGER_EX_PROTOCOL *ChgDetectProtocol = NULL;
	EFI_CHARGER_EX_FLASH_INFO FlashInfo;
	BOOLEAN BatteryPresent = FALSE;
	BOOLEAN ChargerPresent = FALSE;

	*BatteryVoltage = 0;
	Status = gBS->LocateProtocol(&gChargerExProtocolGuid, NULL, (VOID **) &ChgDetectProtocol);
	if (EFI_ERROR(Status) || (NULL == ChgDetectProtocol)) {
		DEBUG((EFI_D_ERROR, "Error locating charger detect protocol\n"));
		return FALSE;
	}

	/* The new protocol are supported on future chipsets */
	if (ChgDetectProtocol->Revision >= CHARGER_EX_REVISION) {
		Status = ChgDetectProtocol->IsPowerOk(EFI_CHARGER_EX_POWER_FLASH_BATTERY_VOLTAGE_TYPE, &FlashInfo);
		if (EFI_ERROR(Status)) {
			/* But be bypassable where the device doesn't even have a battery */
			if (Status == EFI_UNSUPPORTED)
				return TRUE;

			DEBUG((EFI_D_ERROR, "Error getting the info of charger: %r\n", Status));
			return FALSE;
		}

		*BatteryVoltage = FlashInfo.BattCurrVoltage;
		if (!(FlashInfo.bCanFlash) || (*BatteryVoltage < FlashInfo.BattRequiredVoltage))
			return FALSE;
		return TRUE;
	} else {
		Status = TargetCheckBatteryStatus(&BatteryPresent, &ChargerPresent, BatteryVoltage);
		if (((Status == EFI_SUCCESS) &&
			(!BatteryPresent || (BatteryPresent && (*BatteryVoltage > BATT_MIN_VOLT)))) ||
			(Status == EFI_UNSUPPORTED))
		{
			return TRUE;
		}

		return FALSE;
	}
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
UINT32 GetSystemPath(CHAR8 **SysPath)
{
	INT32 Index;
	UINT32 Lun;
	CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
	Slot CurSlot = GetCurrentSlotSuffix();
	CHAR8 LunCharMapping[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
	CHAR8 RootDevStr[BOOT_DEV_NAME_SIZE_MAX];

	*SysPath = AllocatePool(sizeof(char) * MAX_PATH_SIZE);
	if (!*SysPath) {
		DEBUG((EFI_D_ERROR, "Failed to allocated memory for System path query\n"));
		return 0;
	}

	StrnCpyS(PartitionName, StrLen(L"system") + 1, L"system", StrLen(L"system"));
	StrnCatS(PartitionName, MAX_GPT_NAME_SIZE - 1, CurSlot.Suffix, StrLen(CurSlot.Suffix));

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
		AsciiSPrint(*SysPath, MAX_PATH_SIZE, " root=/dev/mmcblk0p%d", Index);
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
				BOOLEAN Recovery,
				BOOLEAN AlarmBoot,
				CONST CHAR8 *VBCmdLine,
				CHAR8 **FinalCmdLine)
{
	EFI_STATUS Status;
	UINT32 CmdLineLen = 0;
	UINT32 HaveCmdLine = 0;
	UINT32 PauseAtBootUp = 0;
	CHAR8 SlotSuffixAscii[MAX_SLOT_SUFFIX_SZ];
	BOOLEAN MultiSlotBoot;
	CHAR8 ChipBaseBand[CHIP_BASE_BAND_LEN];
	CHAR8 *BootDevBuf = NULL;
	BOOLEAN BatteryStatus;
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

	if (VBCmdLine != NULL) {
		DEBUG((EFI_D_VERBOSE, "UpdateCmdLine VBCmdLine present len %d\n",
			AsciiStrLen(VBCmdLine)));
		CmdLineLen += AsciiStrLen(VBCmdLine);
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
	} else if (BatteryStatus && IsChargingScreenEnable()) {
		DEBUG((EFI_D_INFO, "Device will boot into off mode charging mode\n"));
		PauseAtBootUp = 1;
		CmdLineLen += AsciiStrLen(BatteryChgPause);
	} else if (AlarmBoot) {
		CmdLineLen += AsciiStrLen(AlarmBootCmdLine);
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

		gBS->SetMem(Dst, CmdLineLen + 4, 0x0);

		/* Save start ptr for debug print */
		*FinalCmdLine = Dst;

		if (HaveCmdLine) {
			Src = CmdLine;
			STR_COPY(Dst,Src);
		}


		if (VBCmdLine != NULL) {
			Src = VBCmdLine;
			if (HaveCmdLine) --Dst;
			STR_COPY(Dst,Src);
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
		} else if (AlarmBoot) {
			Src = AlarmBootCmdLine;
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

		gBS->SetMem(ChipBaseBand, CHIP_BASE_BAND_LEN, 0);
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

		if (HaveCmdLine) {
			if (MultiSlotBoot && !IsBootDevImage()) {
				/* Slot suffix */
				Src = AndroidSlotSuffix;
				--Dst;
				STR_COPY(Dst,Src);
				--Dst;

				UnicodeStrToAsciiStr(GetCurrentSlotSuffix().Suffix, SlotSuffixAscii);
				Src = SlotSuffixAscii;
				STR_COPY(Dst,Src);

				/* Skip Initramfs*/
				if (!Recovery) {
					Src = SkipRamFs;
					--Dst;
					STR_COPY(Dst, Src);
				}

				/*Add Multi slot command line suffix*/
				Src = MultiSlotCmdSuffix;
				--Dst;
				STR_COPY(Dst, Src);
			}
		}
	}

	DEBUG((EFI_D_INFO, "Cmdline: %a\n", *FinalCmdLine));
	DEBUG((EFI_D_INFO, "\n"));

	return EFI_SUCCESS;
}
