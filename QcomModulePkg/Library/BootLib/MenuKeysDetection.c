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
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/DeviceInfo.h>
#include <Library/ShutdownServices.h>
#include <Library/DrawUI.h>
#include <Library/FastbootMenu.h>
#include <Library/VerifiedBootMenu.h>
#include <Library/MenuKeysDetection.h>
#include <Library/LinuxLoaderLib.h>
#include <Library/Recovery.h>
#include <Library/KeyPad.h>
#include <Library/DeviceInfo.h>

#include <Protocol/EFIVerifiedBoot.h>

STATIC UINT32 StartTimer;
STATIC EFI_EVENT CallbackKeyDetection;

typedef VOID (*Keys_Action_Func)(OPTION_MENU_INFO *gMsgInfo);

/* Device's action when the volume or power key is pressed
 * Up_Action_Func:   The device's action when the volume up key is pressed
 * Down_Action_Func: The device's action when the volume down key is pressed
 * Enter_Action_Func: The device's action when the power up key is pressed
 */
typedef struct {
	Keys_Action_Func Up_Action_Func;
	Keys_Action_Func Down_Action_Func;
	Keys_Action_Func Enter_Action_Func;
} PAGES_ACTION;

/* Exit the key's detection */
VOID ExitMenuKeysDetection()
{
	if (FixedPcdGetBool(EnableDisplayMenu)) {
		/* Close the timer and event */
		if (CallbackKeyDetection) {
			gBS->SetTimer(CallbackKeyDetection, TimerCancel, 0);
			gBS->CloseEvent (CallbackKeyDetection);
			CallbackKeyDetection = NULL;
		}
		DEBUG((EFI_D_INFO, "Exit key detection timer\n"));

		/* Clear the screen */
		gST->ConOut->ClearScreen (gST->ConOut);

		/* Show boot logo */
		RestoreBootLogoBitBuffer();
	}
}

/* Waiting for exit the menu keys' detection
 * The CallbackKeyDetection would be null when the device is timeout
 * or the user chooses to exit keys' detection.
 * Clear the screen and show the penguin on the screen
 */
VOID WaitForExitKeysDetection()
{
	/* Waiting for exit menu keys detection if there is no any usr action
	* otherwise it will do the action base on the keys detection event
	*/
	while (CallbackKeyDetection)
		MicroSecondDelay(10000);
}

STATIC VOID UpdateDeviceStatus(OPTION_MENU_INFO *MsgInfo, UINT32 Reason)
{
	CHAR8 FfbmPageBuffer[FFBM_MODE_BUF_SIZE];

	/* Clear the screen */
	gST->ConOut->ClearScreen (gST->ConOut);

	switch (Reason) {
	case RECOVER:
		switch (MsgInfo->Info.MenuType) {
			case DISPLAY_MENU_UNLOCK:
				SetDeviceUnlockValue(UNLOCK, TRUE);
				break;
			case DISPLAY_MENU_UNLOCK_CRITICAL:
				SetDeviceUnlockValue(UNLOCK_CRITICAL, TRUE);
				break;
		}

		RebootDevice(RECOVERY_MODE);
		break;
	case RESTART:
		RebootDevice(NORMAL_MODE);
		break;
	case POWEROFF:
		ShutdownDevice();
		break;
	case FASTBOOT:
		RebootDevice(FASTBOOT_MODE);
		break;
	case CONTINUE:
		/* Continue boot, no need to detect the keys'status */
		ExitMenuKeysDetection();
		break;
	case BACK:
		VerifiedBootMenuShowScreen(MsgInfo, MsgInfo->LastMenuType);
		StartTimer = GetTimerCountms();
		break;
	case FFBM:
		AsciiSPrint(FfbmPageBuffer, sizeof(FfbmPageBuffer), "ffbm-00");
		WriteToPartition(&gEfiMiscPartitionGuid, FfbmPageBuffer);
		RebootDevice(NORMAL_MODE);
		break;
	}
}

STATIC VOID UpdateBackground(OPTION_MENU_INFO *MenuInfo, UINT32 LastIndex,
			UINT32 CurentIndex)
{
	EFI_STATUS Status, Status0;
	UINT32  LastOption = MenuInfo->Info.OptionItems[LastIndex];
	UINT32  CurrentOption = MenuInfo->Info.OptionItems[CurentIndex];

	Status = UpdateMsgBackground(&MenuInfo->Info.MsgInfo[CurrentOption], BGR_BLUE);
	Status0 = UpdateMsgBackground(&MenuInfo->Info.MsgInfo[LastOption],
		MenuInfo->Info.MsgInfo[LastOption].BgColor);
	if (Status != EFI_SUCCESS || Status0 != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "Failed to update option item's background\n"));
		return;
	}
}

/* Update select option's background when volume up key is pressed */
STATIC VOID MenuVolumeUpFunc(OPTION_MENU_INFO *MenuInfo)
{
	EFI_STATUS Status;
	UINT32     OptionItem = 0;

	UINT32 LastIndex = MenuInfo->Info.OptionIndex;
	UINT32 CurentIndex = LastIndex - 1;

	if (LastIndex == 0 || LastIndex > MenuInfo->Info.OptionNum -1) {
		CurentIndex = MenuInfo->Info.OptionNum - 1;
		LastIndex = 0;
	}

	MenuInfo->Info.OptionIndex = CurentIndex;
	OptionItem = MenuInfo->Info.OptionItems[CurentIndex];

	if (MenuInfo->Info.MenuType == DISPLAY_MENU_FASTBOOT) {
		Status = UpdateFastbootOptionItem(OptionItem, NULL);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Failed to update fastboot option item\n"));
			return;
		}
	} else {
		UpdateBackground(MenuInfo, LastIndex, CurentIndex);
	}
}

/* Update select option's background when volume down key is pressed */
STATIC VOID MenuVolumeDownFunc(OPTION_MENU_INFO *MenuInfo)
{
	EFI_STATUS Status;
	UINT32     OptionItem = 0;

	UINT32 LastIndex = MenuInfo->Info.OptionIndex;
	UINT32 CurentIndex = LastIndex + 1;

	if (LastIndex >= MenuInfo->Info.OptionNum - 1) {
		CurentIndex = 0;
		LastIndex = MenuInfo->Info.OptionNum - 1;
	}

	MenuInfo->Info.OptionIndex = CurentIndex;
	OptionItem = MenuInfo->Info.OptionItems[CurentIndex];

	if (MenuInfo->Info.MenuType == DISPLAY_MENU_FASTBOOT) {
		Status = UpdateFastbootOptionItem(OptionItem, NULL);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Failed to update fastboot option item\n"));
			return;
		}
	} else {
		UpdateBackground(MenuInfo, LastIndex, CurentIndex);
	}
}

/* Enter to boot verification option page if volume key is pressed */
STATIC VOID BootWarningVolumeKeysFunc(OPTION_MENU_INFO *MenuInfo)
{
	MenuInfo->LastMenuType = MenuInfo->Info.MenuType;
	VerifiedBootOptionMenuShowScreen(MenuInfo);
}

/* Update device's status via select option */
STATIC VOID PowerKeyFunc(OPTION_MENU_INFO *MenuInfo)
{
	int Reason = -1;
	UINT32 OptionIndex = MenuInfo->Info.OptionIndex;
	UINT32 OptionItem;

	switch (MenuInfo->Info.MenuType) {
		case DISPLAY_MENU_YELLOW:
		case DISPLAY_MENU_ORANGE:
		case DISPLAY_MENU_RED:
		case DISPLAY_MENU_LOGGING:
			Reason = CONTINUE;
			break;
		case DISPLAY_MENU_MORE_OPTION:
		case DISPLAY_MENU_UNLOCK:
		case DISPLAY_MENU_UNLOCK_CRITICAL:
		case DISPLAY_MENU_FASTBOOT:
			if (OptionIndex < MenuInfo->Info.OptionNum) {
				OptionItem = MenuInfo->Info.OptionItems[OptionIndex];
				Reason = MenuInfo->Info.MsgInfo[OptionItem].Action;
			}
			break;
		default:
			DEBUG((EFI_D_ERROR, "Unsupported menu type\n"));
			break;
	}

	if (Reason != -1) {
		UpdateDeviceStatus(MenuInfo, Reason);
	}
}

STATIC PAGES_ACTION MenuPagesAction[] = {
	[DISPLAY_MENU_UNLOCK] = {
		MenuVolumeUpFunc,
		MenuVolumeDownFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_UNLOCK_CRITICAL] = {
		MenuVolumeUpFunc,
		MenuVolumeDownFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_YELLOW] = {
		BootWarningVolumeKeysFunc,
		BootWarningVolumeKeysFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_ORANGE] = {
		BootWarningVolumeKeysFunc,
		BootWarningVolumeKeysFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_RED] = {
		BootWarningVolumeKeysFunc,
		BootWarningVolumeKeysFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_LOGGING] = {
		BootWarningVolumeKeysFunc,
		BootWarningVolumeKeysFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_MORE_OPTION] = {
		MenuVolumeUpFunc,
		MenuVolumeDownFunc,
		PowerKeyFunc,
	},
	[DISPLAY_MENU_FASTBOOT] = {
		MenuVolumeUpFunc,
		MenuVolumeDownFunc,
		PowerKeyFunc,
	},
};

STATIC BOOLEAN CheckKeyStatus(UINT32 KeyType)
{
	EFI_STATUS Status;
	UINT32 KeyPressed;

	Status = GetKeyPress(&KeyPressed);
	if (Status == EFI_SUCCESS)
	{
		if (KeyPressed == KeyType)
			return TRUE;
	} else if (Status == EFI_DEVICE_ERROR) {
		DEBUG((EFI_D_ERROR, "Error reading key status: %r\n", Status));
	}

	return FALSE;
}

STATIC BOOLEAN IsKeyPressed(UINT32 KeyType)
{
	UINT32 count = 0;
	EFI_STATUS Status = EFI_SUCCESS;
	BOOLEAN Result = FALSE;
	BOOLEAN IsCanceledTimer = FALSE;

	if (CheckKeyStatus(KeyType)) {
		/*if key is pressed, wait for 1s to see if it is released*/
		while(count++ < 10 && CheckKeyStatus(KeyType)) {
			/* Stop timer if the key is be holding */
			if (!IsCanceledTimer) {
				Status = gBS->SetTimer(CallbackKeyDetection, TimerCancel, 0);
				if (Status == EFI_SUCCESS)
					IsCanceledTimer = TRUE;
			}

			MicroSecondDelay(100000);
		}

		Result = TRUE;
	}

	if (IsCanceledTimer) {
		Status = gBS->SetTimer(CallbackKeyDetection, TimerPeriodic, 500000);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "ERROR: Failed to set keys detection Timer: %r\n", Status));
			ExitMenuKeysDetection();
		}
	}
	return Result;
}

/**
  Handle key detection's status
  @param[in] Event      The event of key's detection.
  @param[in] Context    The parameter of key's detection.
  @retval EFI_SUCCESS   The entry point is executed successfully.
  @retval other         Some error occurs when executing this entry point.
 **/
VOID EFIAPI MenuKeysHandler(IN EFI_EVENT Event, IN VOID *Context)
{
	UINT32 TimerDiff;
	OPTION_MENU_INFO  *MenuInfo = Context;

	if (MenuInfo->Info.TimeoutTime > 0) {
		TimerDiff = GetTimerCountms() - StartTimer;
		if (TimerDiff > (MenuInfo->Info.TimeoutTime)*1000) {
			ExitMenuKeysDetection();
			return;
		}
	}

	if (IsKeyPressed(SCAN_UP))
		MenuPagesAction[MenuInfo->Info.MenuType].Up_Action_Func(MenuInfo);
	else if (IsKeyPressed(SCAN_DOWN))
		MenuPagesAction[MenuInfo->Info.MenuType].Down_Action_Func(MenuInfo);
	else if (IsKeyPressed(SCAN_SUSPEND))
		MenuPagesAction[MenuInfo->Info.MenuType].Enter_Action_Func(MenuInfo);

}

/**
  Create a event and timer to detect key's status
  @param[in] mMenuInfo    The option menu info.
  @retval EFI_SUCCESS     The entry point is executed successfully.
  @retval other           Some error occurs when executing this entry point.
 **/
EFI_STATUS EFIAPI MenuKeysDetectionInit(IN VOID *mMenuInfo)
{
	EFI_STATUS Status = EFI_SUCCESS;
	OPTION_MENU_INFO  *MenuInfo = mMenuInfo;

	if (FixedPcdGetBool(EnableDisplayMenu)) {
		StartTimer = GetTimerCountms();

		/* Close the timer and event firstly */
		if (CallbackKeyDetection) {
			gBS->SetTimer(CallbackKeyDetection, TimerCancel, 0);
			gBS->CloseEvent (CallbackKeyDetection);
		}

		/* Create event for handle key status */
		Status = gBS->CreateEvent (
			EVT_TIMER | EVT_NOTIFY_SIGNAL,
			TPL_CALLBACK,
			MenuKeysHandler,
			MenuInfo,
			&CallbackKeyDetection
		);
		DEBUG((EFI_D_VERBOSE, "Create keys detection event: %r\n", Status));

		if (!EFI_ERROR (Status)) {
			Status = gBS->SetTimer(CallbackKeyDetection,
				TimerPeriodic,
				500000);
			DEBUG((EFI_D_VERBOSE, "Set keys detection Timer: %r\n", Status));
		}
	}
	return Status;
}
