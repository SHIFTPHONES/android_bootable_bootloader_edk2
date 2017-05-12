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
#include <Protocol/EFIVerifiedBoot.h>
#include <Library/VerifiedBoot.h>

#define FINGERPRINT_LINE_LEN 16
#define FINGERPRINT_FORMATED_LINE_LEN  FINGERPRINT_LINE_LEN + 5
#define VERIFIED_BOOT_OPTION_NUM  5
STATIC OPTION_MENU_INFO gMenuInfo;

typedef struct {
	MENU_MSG_INFO   WarningMsg;
	MENU_MSG_INFO   UrlMsg;
	MENU_MSG_INFO   Fingerprint;
	MENU_MSG_INFO   CommonMsg;
} WARNING_COMMON_MSG_INFO;

STATIC MENU_MSG_INFO mTitleMsgInfo[] = {
	{{"Start >"},
		BIG_FACTOR, BGR_WHITE, BGR_BLACK, ALIGN_RIGHT, 0, NOACTION},
	{{"Continue boot\n"},
		COMMON_FACTOR, BGR_SILVER, BGR_BLACK, ALIGN_RIGHT, 0, NOACTION},
	{{"< More options"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"Press VOLUME keys\n\n"},
		COMMON_FACTOR, BGR_SILVER, BGR_BLACK, COMMON, 0, NOACTION},
};

STATIC WARNING_COMMON_MSG_INFO mCommonMsgInfo[] = {
	[DISPLAY_MENU_YELLOW] = {
		{{"Your device has loaded a different operating system\n\n"\
			"Visit this link on another device:\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"g.co/ABH\n\n"},
			COMMON_FACTOR, BGR_YELLOW, BGR_BLACK, COMMON, 0, NOACTION},
		{{""}, COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"If no key pressed:\nYour device will boot in 5 seconds\n\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION}},
	[DISPLAY_MENU_ORANGE] = {
		{{"Your device software can't be checked for corruption. "\
			"Please lock the bootloader\n\n"\
			"Visit this link on another device:\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"g.co/ABH\n\n"},
			COMMON_FACTOR, BGR_ORANGE, BGR_BLACK, COMMON, 0, NOACTION},
		{{""}, COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"If no key pressed:\nYour device will boot in 5 seconds\n\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION}},
	[DISPLAY_MENU_RED] = {
		{{"Your device is corrupt. It can't be trusted and will not boot\n\n"\
			"Visit this link on another device:\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"g.co/ABH\n\n"},
			COMMON_FACTOR, BGR_RED, BGR_BLACK, COMMON, 0, NOACTION},
		{{""}, COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"If no key pressed:\nYour device will boot in 5 seconds\n\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION}},
	[DISPLAY_MENU_LOGGING] = {
		{{"The dm-verity is not started in enforcing mode and may "\
			"not work properly\n\n"\
			"Visit this link on another device:\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"g.co/ABH\n\n"},
			COMMON_FACTOR, BGR_RED, BGR_BLACK, COMMON, 0, NOACTION},
		{{""}, COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
		{{"If no key pressed:\nYour device will boot in 5 seconds\n\n"},
			COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION}},
};

STATIC MENU_MSG_INFO mOptionMenuMsgInfo[] = {
	{{"Options menu:\n"},
		BIG_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"\nPress volume key to select, and press power key to select\n\n"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, COMMON, 0, NOACTION},
	{{"____________________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"Power off"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, POWEROFF},
	{{"____________________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"Restart"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, RESTART},
	{{"____________________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"Recovery"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, RECOVER},
	{{"____________________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"Fastboot"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, FASTBOOT},
	{{"____________________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
	{{"Back to previous page"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, OPTION_ITEM, 0, BACK},
	{{"____________________"},
		COMMON_FACTOR, BGR_WHITE, BGR_BLACK, LINEATION, 0, NOACTION},
};

/**
Convert UINT8 array to a CHAR8 hex array.
The caller of the function should allocate memory properly.
Size of target should be at least twice than the source size (len).

@param[out]     *target    Pointer to the output array
@param[in]      *source    Pointer to the source array
@param[in]      len        Size of the source array

**/
STATIC VOID GetHexString(CHAR8 *Target, UINT8 *Source, UINTN Len)
{

	UINTN TargetIndex = 0;
	UINTN SourceIndex = 0;
	UINTN TargetLen = Len * 2;
	CHAR8 HexBuf[3];
	for (TargetIndex = 0; TargetIndex < TargetLen;
		TargetIndex = TargetIndex + 2) {
		AsciiSPrint(HexBuf, sizeof(HexBuf), "%02x",
			    Source[SourceIndex]);
		CopyMem(Target + TargetIndex, HexBuf, 2);
		SourceIndex++;
	 }
}

/**

Construct display output array.
The caller should allocate the buffer properly before invoking this function.
The assumption is display output and finger print have larger size than 22 and 16.

The target array starts with string "ID: " following with the fingerprint.
Each 16 charcaters of fingerprint are shown in one line.
If fingerprint size is too big, it only copies the number of characters
that matches the output size.

Eaxmple output:

ID: 3F957EBAD2EE02F2
    CD23ED905C51913D
    4E9AAA2C5A4A1AE8
    0F9D6BF727593F14

@param[out] *target    Pointer to the output array
@param[in]  target_len Size of output
@param[in]  *source    Pointer to the input array
@param[in]  source_len Size of input

**/
STATIC VOID GetDisplayOutPut(CHAR8 *Target, UINTN TargetLen, CHAR8 *Source,
		      UINTN SourceLen)
{
	UINTN LastLineCharsCount = 0;
	UINTN TargetIndex = 0;
	UINTN SourceIndex = 0;
	UINTN LineNum = 0;
	UINTN FinalLen = 0;

	/* First line starts with 4 characters of "ID: ",
	   other lines start with 4 spaces to make the length of each line 21
	*/
	CONST CHAR8 ID[4] = "ID: ";
	CONST CHAR8 StartlineSpace[4] = "    ";

	/* Each line contains 21 characters (4 spaces in the beginning),
	16 characters from fingerprint, one character for endline */
	UINTN NumberOfLines = SourceLen / FINGERPRINT_LINE_LEN +
		((SourceLen % FINGERPRINT_LINE_LEN== 0 ? 0 :1));

	/* each line contains 4 spaces at the beginning and one endline
	   character at the end */
	FinalLen = (sizeof(ID) + 1) * (NumberOfLines) + SourceLen;

	/* if final size is bigger that display output size,
	   reduce the numbe of lines,
	   one character is needed for NULL character */
	while (FinalLen > TargetLen - 1) {
		NumberOfLines--;
                FinalLen = FinalLen - FINGERPRINT_FORMATED_LINE_LEN;
		SourceLen = SourceLen - FINGERPRINT_LINE_LEN;
	}

	for (LineNum= 0; LineNum < NumberOfLines; LineNum++) {
		if (LineNum == 0) {
			CopyMem(Target + TargetIndex, ID, sizeof(ID));
		} else {
			CopyMem(Target + TargetIndex, StartlineSpace, sizeof(StartlineSpace));
		}
		TargetIndex = TargetIndex + sizeof(ID);
		if ((SourceLen - SourceIndex) >= FINGERPRINT_LINE_LEN) {
			CopyMem(Target + TargetIndex,
				Source + SourceIndex, FINGERPRINT_LINE_LEN);
			TargetIndex = TargetIndex + FINGERPRINT_LINE_LEN;
			SourceIndex = SourceIndex + FINGERPRINT_LINE_LEN;
		} else {
			LastLineCharsCount = SourceLen % FINGERPRINT_LINE_LEN;
			CopyMem(Target + TargetIndex, Source + SourceIndex,
			LastLineCharsCount);
			TargetIndex = TargetIndex + LastLineCharsCount;
			SourceIndex = SourceIndex + LastLineCharsCount;
		}
		Target[TargetIndex] = '\n';
		TargetIndex = TargetIndex + 1;
	}
	/* NULL terminat the target array */
	Target[TargetIndex] = '\0';
}

/**
Draw the verified boot option menu
@param[out] OptionMenuInfo    The option info
@retval     EFI_SUCCESS       The entry point is executed successfully.
@retval     other             Some error occurs when executing this entry point.
 **/
EFI_STATUS VerifiedBootOptionMenuShowScreen(OPTION_MENU_INFO *OptionMenuInfo)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 Location = 0;
	UINT32 Height = 0;
	UINT32 i = 0;
	UINT32 j = 0;

	/* Clear the screen before launch the verified boot option menu */
	gST->ConOut->ClearScreen (gST->ConOut);
	ZeroMem(&OptionMenuInfo->Info, sizeof(MENU_OPTION_ITEM_INFO));

	OptionMenuInfo->Info.MsgInfo = mOptionMenuMsgInfo;
	for (i = 0; i < ARRAY_SIZE(mOptionMenuMsgInfo); i++) {
		if (OptionMenuInfo->Info.MsgInfo[i].Attribute == OPTION_ITEM) {
			if (j < VERIFIED_BOOT_OPTION_NUM) {
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

	OptionMenuInfo->Info.MenuType = DISPLAY_MENU_MORE_OPTION;
	OptionMenuInfo->Info.OptionNum = VERIFIED_BOOT_OPTION_NUM;

	/* Initialize the option index */
	OptionMenuInfo->Info.OptionIndex = VERIFIED_BOOT_OPTION_NUM;

	return Status;
}

/**
  Draw the verified boot menu
  @param[in]  Type              The warning menu type
  @param[out] OptionMenuInfo    The option info
  @retval     EFI_SUCCESS       The entry point is executed successfully.
  @retval     other	       Some error occurs when executing this entry point.
 **/
EFI_STATUS VerifiedBootMenuShowScreen(OPTION_MENU_INFO *OptionMenuInfo, UINT32 Type)
{
	EFI_STATUS Status = EFI_SUCCESS;
	UINT32 Location = 0;
	UINT32 Height = 0;
	UINT32 i = 0;

	ZeroMem(&OptionMenuInfo->Info, sizeof(MENU_OPTION_ITEM_INFO));

	for (i = 0; i < ARRAY_SIZE(mTitleMsgInfo); i++) {
		mTitleMsgInfo[i].Location = Location;
		Status = DrawMenu(&mTitleMsgInfo[i], &Height);
		if (Status != EFI_SUCCESS)
			return Status;
		Location += Height;
	}

	mCommonMsgInfo[Type].WarningMsg.Location = Location;
	Status = DrawMenu(&mCommonMsgInfo[Type].WarningMsg, &Height);
	if (Status != EFI_SUCCESS)
		return Status;
	Location += Height;

	mCommonMsgInfo[Type].UrlMsg.Location = Location;
	Status = DrawMenu(&mCommonMsgInfo[Type].UrlMsg, &Height);
	if (Status != EFI_SUCCESS)
		return Status;
	Location += Height;

	if (Type == DISPLAY_MENU_YELLOW) {
		UINT8 FingerPrint[MAX_MSG_SIZE];
		UINTN FingerPrintLen = 0;

		mCommonMsgInfo[Type].Fingerprint.Location = Location;

		Status = GetCertFingerPrint(FingerPrint, ARRAY_SIZE(FingerPrint),
		                            &FingerPrintLen);
		if (Status == EFI_SUCCESS) {
			UINTN DisplayStrLen = 0;
			CHAR8 *DisplayStr = NULL;

			/* Each bytes needs two characters to be shown on display */
			DisplayStrLen = FingerPrintLen * 2;
			DisplayStr = AllocatePool(DisplayStrLen);
			if (DisplayStr == NULL) {
				return EFI_OUT_OF_RESOURCES;
			}
			/* Convert the fingerprint to a charcater string */
			GetHexString(DisplayStr, FingerPrint, FingerPrintLen);
			/* Save fingerprint in a formated string for display */
			GetDisplayOutPut(mCommonMsgInfo[Type].Fingerprint.Msg,
					 MAX_MSG_SIZE, DisplayStr,
					 DisplayStrLen);
			if (DisplayStr) {
				FreePool(DisplayStr);
			}
		}else {
			AsciiSPrint(mCommonMsgInfo[Type].Fingerprint.Msg,
				MAX_MSG_SIZE, "ID: %a\n", "unsupported");
		}
		Status = DrawMenu(&mCommonMsgInfo[Type].Fingerprint, &Height);
		if (Status != EFI_SUCCESS)
			return Status;
		Location += Height;
	}

	mCommonMsgInfo[Type].CommonMsg.Location = Location;
	Status = DrawMenu(&mCommonMsgInfo[Type].CommonMsg, &Height);
	if (Status != EFI_SUCCESS)
		return Status;
	Location += Height;

	OptionMenuInfo->Info.MenuType = Type;
	/* Initialize the time out time: 5s */
	OptionMenuInfo->Info.TimeoutTime = 5;

	return Status;
}

/**
  Draw the verified boot menu and start to detect the key's status
  @param[in] Type    The type of the warning menu:
                     [DISPLAY_MENU_YELLOW]  ----- Yellow warning menu
                     [DISPLAY_MENU_ORANGE]  ----- Orange warning menu
                     [DISPLAY_MENU_RED]     ----- Red warning menu
                     [DISPLAY_MENU_LOGGING] ----- Logging warning menu
**/
VOID DisplayVerifiedBootMenu(UINT32 Type)
{
	EFI_STATUS Status = EFI_SUCCESS;
	OPTION_MENU_INFO *OptionMenuInfo;
	OptionMenuInfo = &gMenuInfo;

	if (FixedPcdGetBool(EnableDisplayMenu)) {
		DrawMenuInit();

		/* Initialize the last_msg_type */
		OptionMenuInfo->LastMenuType =
			OptionMenuInfo->Info.MenuType;

		Status = VerifiedBootMenuShowScreen(OptionMenuInfo, Type);
		if (Status != EFI_SUCCESS) {
			DEBUG((EFI_D_ERROR, "Unable to show verified menu on screen: %r\n", Status));
			return;
		}

		MenuKeysDetectionInit(OptionMenuInfo);
		DEBUG((EFI_D_VERBOSE, "Creating boot verify keys detect event\n"));
	} else {
		DEBUG((EFI_D_INFO, "Display menu is not enabled!\n"));
	}
}
