/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextInEx.h>
#include "MdtpInternal.h"

#define RGB_BLACK                       (0x0)
#define WIDECHAR_TO_CHAR_MASK           (0x00ff)
#define MAX_STRING_LENGTH               (MDTP_ISV_NAME_LEN)


/*---------------------------------------------------------
 * Global Variables
 *-------------------------------------------------------*/

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL            *gGraphicsOutput = NULL;
STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL         *gSimpleTextOutput = NULL;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL       *gSimpleTextInputEx = NULL;

/*---------------------------------------------------------
 * External Functions
 *-------------------------------------------------------*/

/**
 * MdtpGetScreenResolution
 *
 * Returns the screen resolution (width and height).
 *
 * @param[out] Width - Set to screen width.
 * @param[out] Height - Set to screen height.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpGetScreenResolution(UINT32 *Width, UINT32 *Height)
{
	EFI_STATUS                     Status;

	if ((Width == NULL) || (Height == NULL)) {
		DEBUG((EFI_D_ERROR, "MdtpGetScreenResolution: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	if (!gGraphicsOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiGraphicsOutputProtocolGuid,
				(VOID**)&gGraphicsOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpGetScreenResolution: ERROR, handle GraphicsOutputProtocol failed, Status = %r\n", Status));
			gGraphicsOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	*Width = gGraphicsOutput->Mode->Info->HorizontalResolution;
	*Height = gGraphicsOutput->Mode->Info->VerticalResolution;

	if ((*Width == 0) || (*Height == 0)) {
		DEBUG((EFI_D_ERROR, "MdtpGetScreenResolution: ERROR, invalid values for screen resolution\n"));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpGetScreenLayout
 *
 * Returns the screen layout (number of rows and columns)
 *
 * @param[out] Rows - Set to number of rows.
 * @param[out] Height - Set to number of columns.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpGetScreenLayout(UINTN *Rows, UINTN *Columns)
{
	EFI_STATUS                     Status;

	if ((Rows == NULL) || (Columns == NULL)) {
		DEBUG((EFI_D_ERROR, "MdtpGetScreenLayout: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	if (!gSimpleTextOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiSimpleTextOutProtocolGuid,
				(VOID**)&gSimpleTextOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpGetScreenLayout: ERROR, handle SimpleTextOutProtocol failed, Status = %r\n", Status));
			gSimpleTextOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	Status = gSimpleTextOutput->QueryMode(gSimpleTextOutput, gSimpleTextOutput->Mode->Mode, Columns, Rows);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpGetScreenLayout: ERROR, failed to query text mode, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	if ((*Rows == 0) || (*Columns == 0)) {
		DEBUG((EFI_D_ERROR, "MdtpGetScreenLayout: ERROR, invalid values for screen layout\n"));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}


/**
 * MdtpDisplayBltImage
 *
 * Displays a Blt Buffer at (x,y) coordinates on the screen.
 *
 * @param[out] BltBuffer - A Blt image to display.
 * @param[out] x - x coordinate of the (x,y) location.
 * @param[out] y - y coordinate of the (x,y) location.
 * @param[out] Width - Image width.
 * @param[out] Height - Image height.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpDisplayBltImage(UINT8 *BltBuffer, UINTN x, UINTN y, UINTN Width, UINTN Height)
{
	EFI_STATUS                      Status;

	if (BltBuffer == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayBltImage: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	if (!gGraphicsOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiGraphicsOutputProtocolGuid,
				(VOID**)&gGraphicsOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpDisplayBltImage: ERROR, handle GraphicsOutputProtocol failed, Status = %r\n", Status));
			gGraphicsOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	Status = gGraphicsOutput->Blt(gGraphicsOutput,
			(EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)BltBuffer,
			EfiBltBufferToVideo,
			0,
			0,
			x,
			y,
			Width,
			Height,
			0);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayBltImage: ERROR, failed to display image, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpClearScreen
 *
 * Clears the screen.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpClearScreen()
{
	EFI_STATUS                     Status;

	if (!gSimpleTextOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiSimpleTextOutProtocolGuid,
				(VOID**)&gSimpleTextOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpClearScreen: ERROR, handle SimpleTextOutProtocol failed, Status = %r\n", Status));
			gSimpleTextOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	Status = gSimpleTextOutput->ClearScreen(gSimpleTextOutput);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpClearScreen: ERROR, failed to clear screen, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpClearRectangleSection
 *
 * Clears a rectangle section on the screen.
 * The section is of SectionWidth and SectionHeight dimensions, and will
 * be cleared from the given (x,y) coordinates and down.
 *
 * @param[out] x - x coordinate of the (x,y) location from which the section will be cleared.
 * @param[out] y - y coordinate of the (x,y) location from which the section will be cleared.
 * @param[out] SectionWidth - Width of the section to clear.
 * @param[out] SectionHeight - Height of the section to clear.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpClearRectangleSection(UINTN x, UINTN y, UINTN SectionWidth, UINTN SectionHeight)
{
	EFI_STATUS                      Status;
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL   BltBlackPixel = {RGB_BLACK, RGB_BLACK, RGB_BLACK, 0};

	if (!gGraphicsOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiGraphicsOutputProtocolGuid,
				(VOID**)&gGraphicsOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpClearRectangleSection: ERROR, handle GraphicsOutputProtocol failed, Status = %r\n", Status));
			gGraphicsOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	Status = gGraphicsOutput->Blt(gGraphicsOutput,
			&BltBlackPixel,
			EfiBltVideoFill,
			0,
			0,
			x,
			y,
			SectionWidth,
			SectionHeight,
			0);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpClearRectangleSection: ERROR, failed to clear section, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpReadKeyStroke
 *
 * Returns the latest key stroke.
 *
 * @param[out] KeyStroke - Set to the latest key stroke
 * (only volume up and volume down interest us).
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpReadKeyStroke(MdtpKeyStroke *KeyStroke)
{
	EFI_STATUS                  Status;
	EFI_KEY_DATA                KeyData;

	if (KeyStroke == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpReadKeyStroke: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	if (!gSimpleTextInputEx) {
		Status = gBS->OpenProtocol(gST->ConsoleInHandle,
				&gEfiSimpleTextInputExProtocolGuid,
				(VOID**)&gSimpleTextInputEx,
				gImageHandle,
				NULL,
				EFI_OPEN_PROTOCOL_GET_PROTOCOL);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpReadKeyStroke: ERROR, open TextInputExProtocol failed, Status = %r\n", Status));
			gSimpleTextInputEx = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}

	}

	SetMem(&KeyData, sizeof(KeyData), 0);

	/* Clear previous input from the queue */
	Status = gSimpleTextInputEx->Reset(gSimpleTextInputEx, FALSE);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "MdtpReadKeyStroke: ERROR, failed to reset text input, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	Status = gSimpleTextInputEx->ReadKeyStrokeEx(gSimpleTextInputEx, &KeyData);
	if (EFI_ERROR(Status))
		return MDTP_STATUS_UEFI_ERROR;

	if (KeyData.Key.ScanCode == SCAN_UP)
		*KeyStroke = KEY_VOLUME_UP;
	else if (KeyData.Key.ScanCode == SCAN_DOWN)
		*KeyStroke = KEY_VOLUME_DOWN;
	else
		*KeyStroke = KEY_NONE;              /* Keys other than volume up and volume down are ignored */

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpPrintString
 *
 * Prints a string at current cursor position.
 *
 * @param[in] String - String to print.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPrintString(CHAR8* String)
{
	EFI_STATUS                     Status;
	MdtpStatus                     RetVal;
	CHAR16                         UnicodeString[MAX_STRING_LENGTH];

	if (String == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	if (!gSimpleTextOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiSimpleTextOutProtocolGuid,
				(VOID**)&gSimpleTextOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, handle SimpleTextOutProtocol failed, Status = %r\n", Status));
			gSimpleTextOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	AsciiStrToUnicodeStr(String, &UnicodeString);

	Status = gSimpleTextOutput->OutputString(gSimpleTextOutput, UnicodeString);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, failed to write output string, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpPrintStringInCoordinates
 *
 * Prints a string at (x,y) coordinates on the screen.
 *
 * @param[in] String - String to print.
 * @param[in] x - x coordinate of the (x,y) location.
 * @param[in] y - y coordinate of the (x,y) location.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPrintStringInCoordinates(CHAR8* String, UINTN x, UINTN y)
{
	EFI_STATUS                     Status;
	MdtpStatus                     RetVal;
	CHAR16                         UnicodeString[MAX_STRING_LENGTH];

	if (String == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	if (!gSimpleTextOutput) {
		Status = gBS->HandleProtocol(gST->ConsoleOutHandle,
				&gEfiSimpleTextOutProtocolGuid,
				(VOID**)&gSimpleTextOutput);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, handle SimpleTextOutProtocol failed, Status = %r\n", Status));
			gSimpleTextOutput = NULL;
			return MDTP_STATUS_UEFI_ERROR;
		}
	}

	Status = gSimpleTextOutput->SetCursorPosition(gSimpleTextOutput, x, y);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, failed to set cursor position, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	AsciiStrToUnicodeStr(String, &UnicodeString);

	Status = gSimpleTextOutput->OutputString(gSimpleTextOutput, UnicodeString);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpPrintString: ERROR, failed to write output string, Status = %r\n", Status));
		return MDTP_STATUS_UEFI_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}
