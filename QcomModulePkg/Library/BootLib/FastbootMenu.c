/* Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
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
#include "AutoGen.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DeviceInfo.h>
#include <Library/DrawUI.h>
#include <Library/FastbootMenu.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MenuKeysDetection.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UpdateDeviceTree.h>
#include <Library/BootLinux.h>
#include <Protocol/EFIVerifiedBoot.h>
#include <Uefi.h>

STATIC OPTION_MENU_INFO gMenuInfo;

STATIC MENU_MSG_INFO mFastbootOptionTitle[] = {
    {{"START"},
     BIG_FACTOR,
     BGR_GREEN,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     RESTART},
    {{"Restart bootloader"},
     BIG_FACTOR,
     BGR_RED,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     FASTBOOT},
    {{"Recovery mode"},
     BIG_FACTOR,
     BGR_RED,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     RECOVER},
    {{"Power off"},
     BIG_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     POWEROFF},
    {{"Boot to FFBM"},
     BIG_FACTOR,
     BGR_YELLOW,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     FFBM},
    {{"Boot to QMMI"},
     BIG_FACTOR,
     BGR_YELLOW,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     QMMI},
    {{"Activate slot _a"},
     BIG_FACTOR,
     BGR_ORANGE,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     SET_ACTIVE_SLOT_A},
    {{"Activate slot _b"},
     BIG_FACTOR,
     BGR_ORANGE,
     BGR_BLACK,
     OPTION_ITEM,
     0,
     SET_ACTIVE_SLOT_B},
};

#define FASTBOOT_MSG_INDEX_HEADER                  0
#define FASTBOOT_MSG_INDEX_FASTBOOT                1
#define FASTBOOT_MSG_INDEX_CURRENT_SLOT            2
#define FASTBOOT_MSG_INDEX_PRODUCT_NAME            3
#define FASTBOOT_MSG_INDEX_PRODUCT_MODEL           4
#define FASTBOOT_MSG_INDEX_VARIANT                 5
#define FASTBOOT_MSG_INDEX_BOOTLOADER_VERSION      6
#define FASTBOOT_MSG_INDEX_BASEBAND_VERSION        7
#define FASTBOOT_MSG_INDEX_SERIAL_NUMBER           8
#define FASTBOOT_MSG_INDEX_HARDWARE_REVISION       9
#define FASTBOOT_MSG_INDEX_SECURE_BOOT            10
#define FASTBOOT_MSG_INDEX_DEVICE_STATE_UNLOCKED  11
#define FASTBOOT_MSG_INDEX_DEVICE_STATE_LOCKED    12

STATIC MENU_MSG_INFO mFastbootCommonMsgInfo[] = {
    {{"\nPress volume key to select, "
      "and press power key to select\n\n"},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"Fastboot mode\n\n"},
     COMMON_FACTOR,
     BGR_YELLOW,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"CURRENT_SLOT - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"\nPRODUCT_NAME - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"PRODUCT_MODEL - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"VARIANT - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"BOOTLOADER VERSION - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"BASEBAND VERSION - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"SERIAL NUMBER - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"HARDWARE REVISION - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"SECURE BOOT - "},
     COMMON_FACTOR,
     BGR_WHITE,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"\nDEVICE STATE - unlocked"},
     COMMON_FACTOR,
     BGR_RED,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
    {{"\nDEVICE STATE - locked"},
     COMMON_FACTOR,
     BGR_GREEN,
     BGR_BLACK,
     COMMON,
     0,
     NOACTION},
};

/**
  Update the fastboot option item
  @param[in] OptionItem  The new fastboot option item
  @param[out] pLocation  The pointer of the location
  @retval EFI_SUCCESS	 The entry point is executed successfully.
  @retval other		 Some error occurs when executing this entry point.
 **/
EFI_STATUS
UpdateFastbootOptionItem (UINT32 OptionItem, UINT32 *pLocation)
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 Location = 0;
  UINT32 Height = 0;
  MENU_MSG_INFO *FastbootLineInfo = NULL;

  FastbootLineInfo = AllocateZeroPool (sizeof (MENU_MSG_INFO));
  if (FastbootLineInfo == NULL) {
    DEBUG ((EFI_D_ERROR, "Failed to allocate zero pool.\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  SetMenuMsgInfo (FastbootLineInfo, "__________", COMMON_FACTOR,
                  mFastbootOptionTitle[OptionItem].FgColor,
                  mFastbootOptionTitle[OptionItem].BgColor, LINEATION, Location,
                  NOACTION);
  Status = DrawMenu (FastbootLineInfo, &Height);
  if (Status != EFI_SUCCESS)
    goto Exit;
  Location += Height;

  mFastbootOptionTitle[OptionItem].Location = Location;
  Status = DrawMenu (&mFastbootOptionTitle[OptionItem], &Height);
  if (Status != EFI_SUCCESS)
    goto Exit;
  Location += Height;

  FastbootLineInfo->Location = Location;
  Status = DrawMenu (FastbootLineInfo, &Height);
  if (Status != EFI_SUCCESS)
    goto Exit;
  Location += Height;

Exit:
  FreePool (FastbootLineInfo);
  FastbootLineInfo = NULL;

  if (pLocation != NULL)
    *pLocation = Location;

  return Status;
}

/**
  Draw the fastboot menu
  @param[out] OptionMenuInfo  Fastboot option info
  @retval     EFI_SUCCESS     The entry point is executed successfully.
  @retval     other           Some error occurs when executing this entry point.
 **/
STATIC EFI_STATUS
FastbootMenuShowScreen (OPTION_MENU_INFO *OptionMenuInfo)
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 Location = 0;
  UINT32 OptionItem = 0;
  UINT32 Height = 0;
  UINT32 i = 0;
  CHAR8 StrTemp[MAX_RSP_SIZE] = "";
  CHAR8 StrTemp1[MAX_RSP_SIZE] = "";
  CHAR8 VersionTemp[MAX_VERSION_LEN] = "";
  CHAR8 SlotSuffixAscii[MAX_SLOT_SUFFIX_SZ] = "";
  Slot BootSlot;

  ZeroMem (&OptionMenuInfo->Info, sizeof (MENU_OPTION_ITEM_INFO));

  /* Update fastboot option title */
  OptionMenuInfo->Info.MsgInfo = mFastbootOptionTitle;
  for (i = 0; i < ARRAY_SIZE (mFastbootOptionTitle); i++) {
    OptionMenuInfo->Info.OptionItems[i] = i;
  }
  OptionItem =
      OptionMenuInfo->Info.OptionItems[OptionMenuInfo->Info.OptionIndex];
  Status = UpdateFastbootOptionItem (OptionItem, &Location);
  if (Status != EFI_SUCCESS)
    return Status;

  /* Update fastboot common message */
  for (i = 0; i < ARRAY_SIZE (mFastbootCommonMsgInfo); i++) {
    switch (i) {
    case FASTBOOT_MSG_INDEX_HEADER:
    case FASTBOOT_MSG_INDEX_FASTBOOT:
      break;
    case FASTBOOT_MSG_INDEX_CURRENT_SLOT:
      /* Get current slot */
      BootSlot = GetCurrentSlotSuffix();
      if (!BootSlot.Suffix[0])
        continue;
      UnicodeStrToAsciiStr(BootSlot.Suffix, SlotSuffixAscii);
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), SlotSuffixAscii,
                     sizeof (SlotSuffixAscii));
      break;
    case FASTBOOT_MSG_INDEX_PRODUCT_NAME:
      /* Get product name */
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
        sizeof (mFastbootCommonMsgInfo[i].Msg), PRODUCT_NAME,
        AsciiStrLen (PRODUCT_NAME));
      break;
    case FASTBOOT_MSG_INDEX_PRODUCT_MODEL:
      /* Get product model */
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
        sizeof (mFastbootCommonMsgInfo[i].Msg), PRODUCT_MODEL,
        AsciiStrLen (PRODUCT_MODEL));
      break;
    case FASTBOOT_MSG_INDEX_VARIANT:
      /* Get variant value */
      BoardHwPlatformName (StrTemp, sizeof (StrTemp));
      GetRootDeviceType (StrTemp1, sizeof (StrTemp1));

      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), StrTemp,
                     sizeof (StrTemp));
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), " ",
                     AsciiStrLen (" "));
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), StrTemp1,
                     sizeof (StrTemp1));
      break;
    case FASTBOOT_MSG_INDEX_BOOTLOADER_VERSION:
      /* Get bootloader version */
      GetBootloaderVersion (VersionTemp, sizeof (VersionTemp));
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), VersionTemp,
                     sizeof (VersionTemp));
      break;
    case FASTBOOT_MSG_INDEX_BASEBAND_VERSION:
      /* Get baseband version */
      ZeroMem (VersionTemp, sizeof (VersionTemp));
      GetRadioVersion (VersionTemp, sizeof (VersionTemp));
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), VersionTemp,
                     sizeof (VersionTemp));
      break;
    case FASTBOOT_MSG_INDEX_SERIAL_NUMBER:
      /* Get serial number */
      ZeroMem (StrTemp, sizeof (StrTemp));
      BoardSerialNum (StrTemp, MAX_RSP_SIZE);
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), StrTemp,
                     sizeof (StrTemp));
      break;
    case FASTBOOT_MSG_INDEX_HARDWARE_REVISION:
      /* Get hardware revision */
      ZeroMem (StrTemp, sizeof (StrTemp));
      BoardHardwareRevision (StrTemp, MAX_RSP_SIZE);
      AsciiStrnCatS (mFastbootCommonMsgInfo[i].Msg,
                     sizeof (mFastbootCommonMsgInfo[i].Msg), StrTemp,
                     sizeof (StrTemp));
      break;
    case FASTBOOT_MSG_INDEX_SECURE_BOOT:
      /* Get secure boot value */
      AsciiStrnCatS (
          mFastbootCommonMsgInfo[i].Msg, sizeof (mFastbootCommonMsgInfo[i].Msg),
          IsSecureBootEnabled () ? "yes" : "no",
          IsSecureBootEnabled () ? AsciiStrLen ("yes") : AsciiStrLen ("no"));
      break;
    case FASTBOOT_MSG_INDEX_DEVICE_STATE_UNLOCKED:
      /* Get device status, only show when unlocked */
      if (!IsUnlocked ())
        continue;
      break;
    case FASTBOOT_MSG_INDEX_DEVICE_STATE_LOCKED:
      /* Get device status, only show when locked */
      if (IsUnlocked ())
        continue;
      break;
    }

    mFastbootCommonMsgInfo[i].Location = Location;
    Status = DrawMenu (&mFastbootCommonMsgInfo[i], &Height);
    if (Status != EFI_SUCCESS)
      return Status;
    Location += Height;
  }

  OptionMenuInfo->Info.MenuType = DISPLAY_MENU_FASTBOOT;
  OptionMenuInfo->Info.OptionNum = ARRAY_SIZE (mFastbootOptionTitle);

  return Status;
}

/* Draw the fastboot menu and start to detect the key's status */
VOID DisplayFastbootMenu (VOID)
{
  EFI_STATUS Status;
  OPTION_MENU_INFO *OptionMenuInfo;

  if (IsEnableDisplayMenuFlagSupported ()) {
    OptionMenuInfo = &gMenuInfo;
    DrawMenuInit ();
    OptionMenuInfo->LastMenuType = OptionMenuInfo->Info.MenuType;

    Status = FastbootMenuShowScreen (OptionMenuInfo);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "Unable to show fastboot menu on screen: %r\n",
              Status));
      return;
    }

    MenuKeysDetectionInit (OptionMenuInfo);
    DEBUG ((EFI_D_VERBOSE, "Creating fastboot menu keys detect event\n"));
  } else {
    DEBUG ((EFI_D_INFO, "Display menu is not enabled!\n"));
  }
}
