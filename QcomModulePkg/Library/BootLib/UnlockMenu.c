/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiHiiServicesLib.h>
#include <Library/DeviceInfo.h>
#include <Library/DrawUI.h>
#include <Library/UnlockMenu.h>
#include <Library/MenuKeysDetection.h>
#include <Library/UpdateDeviceTree.h>

#define UNLOCK_OPTION_NUM 2

STATIC OPTION_MENU_INFO gMenuInfo;

STATIC MENU_MSG_INFO mUnlockMenuMsgInfo[] = {
	{{"Unlock bootloader?\n"},
		BIG_FACTOR, BGR_CYAN, BGR_BLACK, COMMON, 0, NOACTION},
	{{"\nIf you unlock the bootloader, you will be able to install "\
		"custom operating system on this phone.\n"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"A custom OS is not subject to the same testing "\
		"as the original OS, and can cause your phone "\
		"and installed applications to stop working properly.\n"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"To prevent unauthorized access to your personal data, "\
		"unlocking the bootloader will also delete all personal "\
		"data from your phone(a \"factory data reset\").\n"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"Press the Volume Up/Down buttons to select Yes "\
		"or No. Then press the Power button to continue.\n\n"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"__________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"Yes"},
		BIG_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, RECOVER},
	{{"Unlock bootloader(may void warranty)"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"__________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"No"},
		BIG_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, RESTART},
	{{"Do not unlock bootloader and restart phone"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"__________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
};

/**
  Draw the unlock menu
  @param[in] type               Unlock menu type
  @param[out] OptionMenuInfo    Unlock option info
  @retval     EFI_SUCCESS       The entry point is executed successfully.
  @retval     other	        Some error occurs when executing this entry point.
 **/
STATIC EFI_STATUS UnlockMenuShowScreen(OPTION_MENU_INFO *OptionMenuInfo, UINT32 Type)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 Location = 0;
	UINT32 Height = 0;
	UINT32 i = 0;
	UINT32 j = 0;

	ZeroMem(&OptionMenuInfo->Info, sizeof(MENU_OPTION_ITEM_INFO));

	OptionMenuInfo->Info.MsgInfo = mUnlockMenuMsgInfo;
	for (i = 0; i < ARRAY_SIZE(mUnlockMenuMsgInfo); i++) {
		if (OptionMenuInfo->Info.MsgInfo[i].Attribute == OPTION_ITEM) {
			if (j < UNLOCK_OPTION_NUM) {
				OptionMenuInfo->Info.OptionItems[j] = i;
				j++;
			}
		}
		OptionMenuInfo->Info.MsgInfo[i].Location = Location;
		Status = DrawMenu(&OptionMenuInfo->Info.MsgInfo[i], &Height);
		if (Status != EFI_SUCCESS)
			return Status;
		Location += Height;
	}

	if (Type == UNLOCK)
		OptionMenuInfo->Info.MenuType = DISPLAY_MENU_UNLOCK;
	else if (Type == UNLOCK_CRITICAL)
		OptionMenuInfo->Info.MenuType = DISPLAY_MENU_UNLOCK_CRITICAL;

	OptionMenuInfo->Info.OptionNum = UNLOCK_OPTION_NUM;

	/* Initialize the option index */
	OptionMenuInfo->Info.OptionIndex = UNLOCK_OPTION_NUM;

	return Status;
}

/**
  Draw the unlock menu and start to detect the key's status
  @param[in] type    The type of the unlock menu
                     [UNLOCK]: The normal unlock menu
                     [UNLOCK_CRITICAL]: The ctitical unlock menu
**/
VOID DisplayUnlockMenu(UINT32 Type)
{
	EFI_STATUS Status = EFI_SUCCESS;
	OPTION_MENU_INFO *OptionMenuInfo;
	OptionMenuInfo = &gMenuInfo;

	if (FixedPcdGetBool(EnableDisplayMenu)) {
		DrawMenuInit();

		/* Initialize the last menu type */
		OptionMenuInfo->LastMenuType =
			OptionMenuInfo->Info.MenuType;

		Status = UnlockMenuShowScreen(OptionMenuInfo, Type);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Unable to show unlock menu on screen: %r\n", Status));
			return;
		}

		MenuKeysDetectionInit(OptionMenuInfo);
		DEBUG((EFI_D_VERBOSE, "Creating unlock keys detect event\n"));
	} else {
		DEBUG((EFI_D_INFO, "Display menu is not enabled!\n"));
	}
}
