/* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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
#include "LinuxLoaderLib.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/Recovery.h>
#include <Library/StackCanary.h>
#include <FastbootLib/FastbootCmds.h>

DeviceInfo DevInfo;

BOOLEAN IsUnlocked()
{
	return DevInfo.is_unlocked;
}

BOOLEAN IsUnlockCritical()
{
	return DevInfo.is_unlock_critical;
}

BOOLEAN IsEnforcing()
{
	return DevInfo.verity_mode;
}

BOOLEAN IsChargingScreenEnable()
{
	return DevInfo.is_charger_screen_enabled;
}

VOID GetBootloaderVersion(CHAR8* BootloaderVersion, UINT32 Len)
{
	AsciiSPrint(BootloaderVersion, Len, "%a", DevInfo.bootloader_version);
}

VOID GetRadioVersion(CHAR8* RadioVersion, UINT32 Len)
{
	AsciiSPrint(RadioVersion, Len, "%a", DevInfo.radio_version);
}

EFI_STATUS EnableChargingScreen(BOOLEAN IsEnabled)
{
	EFI_STATUS Status = EFI_SUCCESS;

	if (IsChargingScreenEnable() != IsEnabled) {
		DevInfo.is_charger_screen_enabled = IsEnabled;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Error %a charger screen: %r\n", (IsEnabled? "Enabling":"Disabling"), Status));
			return Status;
		}
	}

	return Status;
}

EFI_STATUS EnableEnforcingMode(BOOLEAN IsEnabled)
{
	EFI_STATUS Status = EFI_SUCCESS;

	if (IsEnforcing() != IsEnabled) {
		DevInfo.verity_mode= IsEnabled;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "VBRwDeviceState Returned error: %r\n", Status));
			return Status;
		}
	}

	return Status;
}

STATIC EFI_STATUS SetUnlockValue(BOOLEAN State)
{
	EFI_STATUS Status = EFI_SUCCESS;

	if (IsUnlocked() != State) {
		DevInfo.is_unlocked = State;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Unable set the unlock value: %r\n", Status));
			return Status;
		}
	}

	return Status;
}

STATIC EFI_STATUS SetUnlockCriticalValue(BOOLEAN State)
{
	EFI_STATUS Status = EFI_SUCCESS;

	if (IsUnlockCritical() != State) {
		DevInfo.is_unlock_critical = State;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Unable set the unlock critical value: %r\n", Status));
			return Status;
		}
	}
	return Status;
}

EFI_STATUS SetDeviceUnlockValue(UINT32 Type, BOOLEAN State)
{
	EFI_STATUS Status = EFI_SUCCESS;
	struct RecoveryMessage Msg;

	switch (Type) {
		case UNLOCK:
			Status = SetUnlockValue(State);
			break;
		case UNLOCK_CRITICAL:
			Status = SetUnlockCriticalValue(State);
			break;
		default:
			Status = EFI_UNSUPPORTED;
			break;
	}
	if (Status != EFI_SUCCESS)
		return Status;

	Status = ResetDeviceState();
	if (Status != EFI_SUCCESS) {
		if (Type == UNLOCK)
			SetUnlockValue(!State);
		else if (Type == UNLOCK_CRITICAL)
			SetUnlockCriticalValue(!State);

		DEBUG((EFI_D_ERROR, "Unable to set the Value: %r", Status));
		return Status;
	}

	SetMem((VOID *)&Msg, sizeof(Msg), 0);
	AsciiStrnCpyS(Msg.recovery, sizeof(Msg.recovery), RECOVERY_WIPE_DATA, AsciiStrLen(RECOVERY_WIPE_DATA));
	WriteToPartition(&gEfiMiscPartitionGuid, &Msg);

	return Status;
}

EFI_STATUS DeviceInfoInit()
{
	EFI_STATUS Status = EFI_SUCCESS;
	STATIC BOOLEAN FirstReadDevInfo = TRUE;

	if (FirstReadDevInfo) {
		Status = ReadWriteDeviceInfo(READ_CONFIG, (UINT8 *)&DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Unable to Read Device Info: %r\n", Status));
			return Status;
		}

		FirstReadDevInfo = FALSE;
	}

	if (CompareMem(DevInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE)) {
		DEBUG((EFI_D_ERROR, "Device Magic does not match\n"));
		CopyMem(DevInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
		if (IsSecureBootEnabled())
		{
			DevInfo.is_unlocked = FALSE;
			DevInfo.is_unlock_critical = FALSE;
		} else {
			DevInfo.is_unlocked = TRUE;
			DevInfo.is_unlock_critical = TRUE;
		}
		DevInfo.is_charger_screen_enabled = FALSE;
		DevInfo.verity_mode = TRUE;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, (UINT8 *)&DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Unable to Write Device Info: %r\n", Status));
			return Status;
		}
	}

	return Status;
}
