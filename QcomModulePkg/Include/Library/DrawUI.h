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


#ifndef _DRAWUI_H_
#define _DRAWUI_H_

#include <Uefi.h>

/* The defined number of characters in a row for the
 * smallest ScaleFactorType, by default value is 40
 */
#define CHAR_NUM_PERROW     40
#define MAX_MSG_SIZE        256
#define MAX_RSP_SIZE        64
#define OPTION_MAX          5

typedef enum {
	DISPLAY_MENU_YELLOW = 0,
	DISPLAY_MENU_ORANGE,
	DISPLAY_MENU_RED,
	DISPLAY_MENU_LOGGING,
	DISPLAY_MENU_MORE_OPTION,
	DISPLAY_MENU_UNLOCK,
	DISPLAY_MENU_FASTBOOT,
	DISPLAY_MENU_UNLOCK_CRITICAL,
} DISPLAY_MENU_TYPE;

typedef enum {
	BGR_WHITE,
	BGR_BLACK,
	BGR_ORANGE,
	BGR_YELLOW,
	BGR_RED,
	BGR_GREEN,
	BGR_BLUE,
	BGR_CYAN,
	BGR_SILVER,
} COLOR_TYPE;

typedef enum {
	COMMON_FACTOR = 1,
	BIG_FACTOR,
	MAX_FACTORTYPE = 2,
} SCALE_FACTOR_TYPE;

typedef enum {
	POWEROFF = 0,
	RESTART,
	RECOVER,
	FASTBOOT,
	BACK,
	CONTINUE,
	FFBM,
	NOACTION,
} OPTION_ITEM_ACTION;

typedef enum {
	COMMON = 0,
	ALIGN_RIGHT,
	ALIGN_LEFT,
	OPTION_ITEM,
	LINEATION,
} MENU_STRING_TYPE;

typedef struct{
	CHAR8   Msg[MAX_MSG_SIZE];
	UINT32  ScaleFactorType;
	UINT32  FgColor;
	UINT32  BgColor;
	UINT32  Attribute;
	UINT32  Location;
	UINT32  Action;
} MENU_MSG_INFO;

typedef struct {
	MENU_MSG_INFO   *MsgInfo;
	UINT32          OptionItems[OPTION_MAX];
	UINT32          OptionNum;
	UINT32          OptionIndex;
	UINT32          MenuType;
	UINT32          TimeoutTime;
} MENU_OPTION_ITEM_INFO;

typedef struct {
	MENU_OPTION_ITEM_INFO   Info;
	UINT32                  LastMenuType;
} OPTION_MENU_INFO;

VOID SetMenuMsgInfo(MENU_MSG_INFO *MenuMsgInfo, CHAR8* Msg, UINT32 ScaleFactorType,
	UINT32 FgColor, UINT32 BgColor, UINT32 Attribute, UINT32 Location, UINT32 Action);
EFI_STATUS DrawMenu(MENU_MSG_INFO *TargetMenu, UINT32 *Height);
EFI_STATUS UpdateMsgBackground(MENU_MSG_INFO *MenuMsgInfo, UINT32 NewBgColor);
EFI_STATUS BackUpBootLogoBltBuffer();
EFI_STATUS RestoreBootLogoBitBuffer();
VOID FreeBootLogoBltBuffer();
VOID DrawMenuInit();
#endif
