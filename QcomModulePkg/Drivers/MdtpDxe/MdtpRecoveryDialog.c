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
#include "MdtpImageManager.h"
#include "MdtpQsee.h"
#include "MdtpInternal.h"


/*----------------------------------------------------------------------------
 * Global Variables
 * -------------------------------------------------------------------------*/

STATIC MdtpImageInfo        *gMdtpImage = NULL;
STATIC ScreenResolution     gScreenResolution = {0, 0, FALSE};

STATIC UINT32               gPinDigitLocationX[MDTP_PIN_LEN] = {0};
STATIC UINT32               gPinDigitLocationY = 0;
STATIC BOOLEAN              gDisplayPin = FALSE;
STATIC BOOLEAN              gInitialScreenDisplayed = FALSE;

/*----------------------------------------------------------------------------
 * Internal Functions
 * -------------------------------------------------------------------------*/

/**
 * Sets screen width and height in a global variable, so they are available to all the functions
 * in this file.
 * This function will be called as part of the recovery dialog initialization.
 */
STATIC MdtpStatus MdtpSetScreenResolution()
{
	UINT32                  Width;
	UINT32                  Height;
	MdtpStatus              RetVal;

	RetVal = MdtpGetScreenResolution(&Width, &Height);

	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpSetScreenResolution: ERROR, failed to get screen resolution\n"));
		return MDTP_STATUS_RECOVERY_DIALOG_ERROR;
	}

	gScreenResolution.Width = Width;
	gScreenResolution.Height = Height;
	gScreenResolution.Valid = TRUE;

	return MDTP_STATUS_SUCCESS;
}

/**
 * Initializes the recovery dialog: allocates the required buffers
 * and sets the screen resolution.
 */
STATIC MdtpStatus MdtpRecoveryDialogInit()
{
	EFI_STATUS              Status;
	MdtpStatus              RetVal;

	if (!gMdtpImage) {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(MdtpImageInfo), (VOID**)&gMdtpImage);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpRecoveryDialogInit: ERROR, failed to allocate buffer, Status = %r\n", Status));
			gMdtpImage = NULL;
			return MDTP_STATUS_ALLOCATION_ERROR;
		}

		if (gScreenResolution.Valid == FALSE) {
			RetVal = MdtpSetScreenResolution();
			if (RetVal) {
				DEBUG((EFI_D_ERROR, "MdtpRecoveryDialogInit: ERROR, recovery dialog failed to initialize\n"));
				MdtpRecoveryDialogShutdown();
				return MDTP_STATUS_RECOVERY_DIALOG_ERROR;
			}
		}
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * Converts raw Bmp image (without Bmp header) to Blt:
 * 1. Rows in Bmp are reversed (bottom to top), so they are reversed back
 * 2. A fourth byte is added for the "Reserved" byte, and set to 0
 */
STATIC VOID MdtpConvertBmpToBlt()
{
	MdtpImageInfo           *Image = gMdtpImage;
	UINT32                  Width = Image->Width;
	UINT32                  Height = Image->Height;
	UINT32                  Row;
	UINT32                  Col;

	SetMem(Image->ImageBlt, sizeof(Image->ImageBlt, 0));

	for (Row = 0; Row<Height; Row++) {
		for (Col = 0; Col<Width; Col++) {
			CopyMem(&(Image->ImageBlt[(Row*Width + Col)*(BYTES_PER_PIXEL_BLT)]),
					&(Image->ImageBmp[((Height - 1 - Row)*Width + Col)*BYTES_PER_PIXEL_BMP]),
					BYTES_PER_PIXEL_BMP);
		}
	}
}

/**
 * Loads Bmp images from the mdtp partition, and converts them to Blt.
 */
STATIC MdtpImageInfo* MdtpLoadImage(UINT32 Offset, UINT32 Width, UINT32 Height)
{
	MdtpPartitionHandle         MdtpPartitionHandle;
	MdtpImageInfo               *Image = gMdtpImage;
	MdtpStatus                  RetVal;

	RetVal = MdtpPartitionGetHandle("mdtp", &MdtpPartitionHandle);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpLoadImage: ERROR, failed to get MDTP partition handle\n"));
		return NULL;
	}

	RetVal = MdtpPartitionRead(&MdtpPartitionHandle, (UINT8*)Image->ImageBmp, Width*Height*BYTES_PER_PIXEL_BMP, Offset);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpLoadImage: ERROR, failed to read from MDTP partition\n"));
		return NULL;
	}

	Image->Width = Width;
	Image->Height = Height;

	MdtpConvertBmpToBlt();

	return Image;
}

/**
 * Displays an image on the screen.
 * Before the image is displayed, a validation is performed to make sure this is
 * possible based on the image dimensions and the screen resolution.
 */
STATIC VOID MdtpDisplayImageInCoordinates(MdtpImageInfo *Image, UINT32 x, UINT32 y)
{
	UINT32                      Width;
	UINT32                      Height;
	MdtpStatus                  RetVal;

	if (gScreenResolution.Valid == FALSE) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayImageInCoordinates: ERROR, screen resolution is not available, unable to display image\n"));
	}

	if (Image) {
		Width = Image->Width;
		Height = Image->Height;
	}
	else {
		DEBUG((EFI_D_ERROR, "MdtpDisplayImageInCoordinates: ERROR, invalid image\n"));
		return;
	}

	if (Width == gScreenResolution.Width && Height == gScreenResolution.Height) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayImageInCoordinates: ERROR, full screen image, cannot be displayed\n"));
		return;
	}

	if (Width > gScreenResolution.Width || Height > gScreenResolution.Height ||
			(x > (gScreenResolution.Width - Width)) || (y > (gScreenResolution.Height - Height))) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayImageInCoordinates: ERROR, invalid image size\n"));
		return;
	}

	RetVal = MdtpDisplayBltImage(Image->ImageBlt, x, y, Width, Height);
	if (RetVal)
		DEBUG((EFI_D_ERROR, "MdtpDisplayImageInCoordinates: ERROR while displaying the image\n"));
}

/**
 * Displays the main error image.
 */
STATIC MdtpStatus MdtpDisplayErrorImage()
{
	MdtpImageInfo                   *Image;

	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(ALERT_MESSAGE),gScreenResolution.Width);
	UINT32 y = ((gScreenResolution.Height)*ERROR_MESSAGE_RELATIVE_Y_LOCATION);

	Image = MdtpLoadImage(MdtpImageManagerGetOffset(ALERT_MESSAGE),
			MdtpImageManagerGetWidth(ALERT_MESSAGE),
			MdtpImageManagerGetHeight(ALERT_MESSAGE));

	if (NULL == Image) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayErrorImage: ERROR, failed to load error image\n"));
		return MDTP_STATUS_RECOVERY_DIALOG_ERROR;
	}

	MdtpDisplayImageInCoordinates(Image, x, y);

	return MDTP_STATUS_SUCCESS;
}

/**
 * Reads an image with (width,height) dimensions from a specific offset in the mdtp partition,
 * and displays it at (x,y) coordinates on the screen.
 */
STATIC VOID MdtpDisplayImage(UINT32 Offset, UINT32 Width, UINT32 Height, UINT32 x, UINT32 y)
{
	MdtpImageInfo                   *Image;

	Image = MdtpLoadImage(Offset, Width, Height);
	if (NULL == Image) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayImage: ERROR, failed to load image\n"));
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	}

	MdtpDisplayImageInCoordinates(Image, x, y);
}

/**
 * Displays initial delay message.
 */
STATIC VOID MdtpDisplayInitialDelay()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(MAINTEXT_5SECONDS),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*MAIN_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(MAINTEXT_5SECONDS),
			MdtpImageManagerGetWidth(MAINTEXT_5SECONDS),
			MdtpImageManagerGetHeight(MAINTEXT_5SECONDS), x, y);
}

/**
 * Displays "enter PIN" message.
 */
STATIC VOID MdtpDisplayEnterPin()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(MAINTEXT_ENTERPIN),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*MAIN_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(MAINTEXT_ENTERPIN),
			MdtpImageManagerGetWidth(MAINTEXT_ENTERPIN),
			MdtpImageManagerGetHeight(MAINTEXT_ENTERPIN), x, y);
}

/**
 * Displays "enter master PIN" message.
 */
STATIC VOID MdtpDisplayEnterMasterPin()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(MAINTEXT_ENTERMASTERPIN),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*MAIN_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(MAINTEXT_ENTERMASTERPIN),
			MdtpImageManagerGetWidth(MAINTEXT_ENTERMASTERPIN),
			MdtpImageManagerGetHeight(MAINTEXT_ENTERMASTERPIN), x, y);
}

/**
 * Displays "invalid PIN message".
 */
STATIC VOID MdtpDisplayInvalidPin()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(MAINTEXT_INCORRECTPIN),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*MAIN_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(MAINTEXT_INCORRECTPIN),
			MdtpImageManagerGetWidth(MAINTEXT_INCORRECTPIN),
			MdtpImageManagerGetHeight(MAINTEXT_INCORRECTPIN), x, y);
}

/**
 * Displays digits instructions.
 */
STATIC VOID MdtpDisplayDigitsInstructions()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(PINTEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*PIN_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(PINTEXT),
			MdtpImageManagerGetWidth(PINTEXT),
			MdtpImageManagerGetHeight(PINTEXT), x, y);
}

/**
 * Clears digits instructions.
 */
STATIC VOID MdtpClearDigitsInstructions()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(PINTEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*PIN_TEXT_RELATIVE_Y_LOCATION;

	MdtpClearRectangleSection(x, y, MdtpImageManagerGetWidth(PINTEXT), MdtpImageManagerGetHeight(PINTEXT));
}

/**
 * Displays a digit as unselected.
 */
STATIC VOID MdtpDisplayDigit(UINT32 x, UINT32 y, UINT32 digit)
{
	if (gDisplayPin == FALSE)
		return;

	MdtpDisplayImage(MdtpImageManagerGetOffset(PIN_UNSELECTED_0 + digit),
			MdtpImageManagerGetWidth(PIN_UNSELECTED_0 + digit),
			MdtpImageManagerGetHeight(PIN_UNSELECTED_0 + digit), x, y);
}

/**
 * Displays a digit as selected.
 */
STATIC VOID MdtpDisplaySelectedDigit(UINT32 x, UINT32 y, UINT32 digit)
{
	if (gDisplayPin == FALSE)
		return;

	MdtpDisplayImage(MdtpImageManagerGetOffset(PIN_SELECTED_0 + digit),
			MdtpImageManagerGetWidth(PIN_SELECTED_0 + digit),
			MdtpImageManagerGetHeight(PIN_SELECTED_0 + digit), x, y);
}

/**
 * Displays OK button as unselected.
 */
STATIC VOID MdtpDisplayOkButton()
{
	UINT32 x = ALIGN_MULTI_ON_X_AXIS(MdtpImageManagerGetWidth(BTN_OK_OFF),gScreenResolution.Width, NUM_BUTTONS, OK_BUTTON_ID);
	UINT32 y = (gScreenResolution.Height)*BUTTON_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(BTN_OK_OFF),
			MdtpImageManagerGetWidth(BTN_OK_OFF),
			MdtpImageManagerGetHeight(BTN_OK_OFF), x, y);
}

/**
 * Displays OK button as selected.
 */
STATIC VOID MdtpDisplaySelectedOkButton()
{
	UINT32 x = ALIGN_MULTI_ON_X_AXIS(MdtpImageManagerGetWidth(BTN_OK_ON),gScreenResolution.Width, NUM_BUTTONS, OK_BUTTON_ID);
	UINT32 y = (gScreenResolution.Height)*BUTTON_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(BTN_OK_ON),
			MdtpImageManagerGetWidth(BTN_OK_ON),
			MdtpImageManagerGetHeight(BTN_OK_ON),  x, y);
}

/**
 * Displays help button as unselected.
 */
STATIC VOID MdtpDisplayHelpButton()
{
	UINT32 x = ALIGN_MULTI_ON_X_AXIS(MdtpImageManagerGetWidth(BTN_HELP_OFF),gScreenResolution.Width, NUM_BUTTONS, HELP_BUTTON_ID);
	UINT32 y = (gScreenResolution.Height)*BUTTON_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(BTN_HELP_OFF),
			MdtpImageManagerGetWidth(BTN_HELP_OFF),
			MdtpImageManagerGetHeight(BTN_HELP_OFF), x, y);
}

/**
 * Displays help button as selected.
 */
STATIC VOID MdtpDisplaySelectedHelpButton()
{
	UINT32 x = ALIGN_MULTI_ON_X_AXIS(MdtpImageManagerGetWidth(BTN_HELP_ON),gScreenResolution.Width, NUM_BUTTONS, HELP_BUTTON_ID);
	UINT32 y = (gScreenResolution.Height)*BUTTON_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(BTN_HELP_ON),
			MdtpImageManagerGetWidth(BTN_HELP_ON),
			MdtpImageManagerGetHeight(BTN_HELP_ON),  x, y);
}

/**
 * Displays the instructions for the OK button.
 */
STATIC VOID MdtpDisplayOkInstructions()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(ACCEPTEDIT_TEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*OK_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(ACCEPTEDIT_TEXT),
			MdtpImageManagerGetWidth(ACCEPTEDIT_TEXT),
			MdtpImageManagerGetHeight(ACCEPTEDIT_TEXT), x, y);
}

/**
 * Clears the instructions for the OK button.
 */
STATIC VOID MdtpClearOkInstructions()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(ACCEPTEDIT_TEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*OK_TEXT_RELATIVE_Y_LOCATION;

	MdtpClearRectangleSection(x, y, MdtpImageManagerGetWidth(ACCEPTEDIT_TEXT), MdtpImageManagerGetHeight(ACCEPTEDIT_TEXT));
}

/**
 * Displays the instructions for the help button.
 */
STATIC VOID MdtpDisplayHelpInstructions()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(HELP_TEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*HELP_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(HELP_TEXT),
			MdtpImageManagerGetWidth(HELP_TEXT),
			MdtpImageManagerGetHeight(HELP_TEXT), x, y);
}

/**
 * Clears the instructions for the help button.
 */
STATIC VOID MdtpClearHelpInstructions()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(HELP_TEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*HELP_TEXT_RELATIVE_Y_LOCATION;

	MdtpClearRectangleSection(x, y, MdtpImageManagerGetWidth(HELP_TEXT), MdtpImageManagerGetHeight(HELP_TEXT));
}

/**
 * Displays "press any key" message.
 */
STATIC VOID MdtpDisplayPressAnyKey()
{
	UINT32 x = CENTER_IMAGE_ON_X_AXIS(MdtpImageManagerGetWidth(PRESS_ANY_KEY_TEXT),gScreenResolution.Width);
	UINT32 y = (gScreenResolution.Height)*HELP_TEXT_RELATIVE_Y_LOCATION;

	MdtpDisplayImage(MdtpImageManagerGetOffset(PRESS_ANY_KEY_TEXT),
			MdtpImageManagerGetWidth(PRESS_ANY_KEY_TEXT),
			MdtpImageManagerGetHeight(PRESS_ANY_KEY_TEXT), x, y);
}

/**
 * Displays the help dialog, with ISV parameters.
 */
STATIC VOID MdtpDisplayHelpDialog()
{
	mdtp_isv_params_t         IsvParams;
	MdtpStatus                RetVal;
	CHAR8                     *Title              = " ISV information";
	CHAR8                     *DeviceId           = "   Device Identifier : ";
	CHAR8                     *IsvName            = "   ISV name          : ";
	CHAR8                     *IsvFriendlyName    = "   ISV description   : ";
	CHAR8                     *NotAvailable       = "Not available";
	UINTN                     NotAvailableLen;
	UINTN                     Rows = 0;
	UINTN                     Columns = 0;
	UINTN                     FirstLineLocation;

	/* Making sure that the "press any key" is not missed. */
	gBS->Stall(MDTP_PRESSING_DELAY_USEC);

	RetVal = MdtpQseeGetIsvParams(&IsvParams);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayHelpDialog: ERROR, can't get ISV parameters\n"));

		NotAvailableLen = AsciiStrLen(NotAvailable) + 1;

		CopyMem(IsvParams.isv_device_id.data, NotAvailable, NotAvailableLen);
		CopyMem(IsvParams.isv_name.data, NotAvailable, NotAvailableLen);
		CopyMem(IsvParams.isv_friendly_name.data, NotAvailable, NotAvailableLen);
	}

	MdtpClearScreen();                                                              /* No point in checking the return value here */
	MdtpDisplayErrorImage();                                                        /* No point in checking the return value here */

	/* Print ISV params.
	 * No point in checking the return values, since we would still want to allow "press any key" to return to the main screen. */
	RetVal = MdtpGetScreenLayout(&Rows, &Columns);
	if ((!RetVal)) {
		FirstLineLocation = TEXTUAL_ERROR_RELATIVE_Y_LOCATION*Rows;

		MdtpPrintStringInCoordinates(Title, 0, FirstLineLocation);

		MdtpPrintStringInCoordinates(DeviceId, 0, FirstLineLocation+SPACE_BETWEEN_TEXT_LINES);
		MdtpPrintString(IsvParams.isv_device_id.data);

		MdtpPrintStringInCoordinates(IsvName, 0, FirstLineLocation+SPACE_BETWEEN_TEXT_LINES*2);
		MdtpPrintString(IsvParams.isv_name.data);

		MdtpPrintStringInCoordinates(IsvFriendlyName, 0, FirstLineLocation+SPACE_BETWEEN_TEXT_LINES*3);
		MdtpPrintString(IsvParams.isv_friendly_name.data);
	}

	MdtpDisplayPressAnyKey();
}

/**
 * Displays the basic layout of the screen (done only once).
 */
STATIC VOID MdtpDisplayInitialScreen(UINT32 PinLength, BOOLEAN DelayRequired)
{
	UINT32                      TotalPinLength;
	UINT32                      CompletePinCentered;
	UINT32                      Index;
	MdtpStatus                  RetVal;

	if (gInitialScreenDisplayed == TRUE)
		return;

	RetVal = MdtpRecoveryDialogInit();
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpDisplayInitialScreen: ERROR, failed to initialize recovery dialog\n"));
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */
	}

	if (MdtpClearScreen())
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */

	if (MdtpDisplayErrorImage())
		MdtpRecoveryDialogDisplayErrorMessage(); /* This will never return */

	if (DelayRequired) {
		MdtpDisplayInitialDelay();
		gBS->Stall(INITIAL_DELAY_USECONDS);
	}

	gPinDigitLocationY = ((gScreenResolution.Height)*PIN_RELATIVE_Y_LOCATION);

	TotalPinLength = PinLength*MdtpImageManagerGetWidth(PIN_UNSELECTED_0) + MdtpImageManagerGetParamValue(DIGIT_SPACE)*(PinLength - 1);

	if (gScreenResolution.Width > TotalPinLength) {
		gDisplayPin = TRUE;

		CompletePinCentered = (gScreenResolution.Width - TotalPinLength)/2;

		for (Index = 0; Index<PinLength; Index++) {
			gPinDigitLocationX[Index] = CompletePinCentered + Index*(MdtpImageManagerGetParamValue(DIGIT_SPACE) + MdtpImageManagerGetWidth(PIN_UNSELECTED_0));
		}

		for (Index = 0; Index<PinLength; Index++) {
			MdtpDisplayDigit(gPinDigitLocationX[Index], gPinDigitLocationY, 0);
		}
	}
	else {
		DEBUG((EFI_D_ERROR, "MdtpDisplayInitialScreen: ERROR, screen is not wide enough, PIN digits can't be displayed\n"));
	}

	MdtpDisplayOkButton();
	MdtpDisplayHelpButton();

	gInitialScreenDisplayed = TRUE;
}

/**
 * Displays the recovery PIN screen and sets the received buffer
 * with the PIN the user has entered.
 * The entered PIN will be validated by the calling function.
 */
STATIC VOID MdtpGetPinInterface(char *EnteredPin, UINT32 PinLength, BOOLEAN MasterPin)
{
	UINT32                      PreviousPosition = 0;
	UINT32                      CurrentPosition = 0;
	BOOLEAN                     DrawInitialScreen = TRUE;
	BOOLEAN                     RedrawInitialScreen = TRUE;
	MdtpKeyStroke               KeyStroke;
	UINT32                      Index;
	MdtpStatus                  RetVal;

	/* Convert ascii to digits */
	for (UINT32 Index=0; Index<PinLength; Index++) {
		EnteredPin[Index] -= '0';
	}

	while (1) {
		if (DrawInitialScreen) {
			MdtpDisplayInitialScreen(PinLength, RedrawInitialScreen);

			if (MasterPin) {
				MdtpDisplayEnterMasterPin();
			} else {
				MdtpDisplayEnterPin();
			}

			MdtpDisplaySelectedDigit(gPinDigitLocationX[0], gPinDigitLocationY, EnteredPin[0]);
			MdtpDisplayDigitsInstructions();

			DrawInitialScreen = FALSE;
			RedrawInitialScreen = FALSE;
		}


		/* Reset current key stroke before reading the next one */
		KeyStroke = KEY_NONE;

		RetVal = MdtpReadKeyStroke(&KeyStroke);
		if (RetVal)
			continue;

		/* Volume up pressed */
		if (KeyStroke == KEY_VOLUME_UP) {
			/* current position is the OK button */
			if (CurrentPosition == PinLength) {
				/* Convert digits to ascii and
				 * validate entered PIN in the calling function */
				for (UINT32 i=0; i<PinLength; i++) {
					EnteredPin[i] += '0';
				}
				return;
			}

			/* Current position is the help button */
			else if (CurrentPosition == PinLength + 1) {
				MdtpDisplayHelpDialog();

				/* Wait for any key press */
				while (1) {
					/* Reset current key stroke before reading the next one */
					KeyStroke = KEY_NONE;

					RetVal = MdtpReadKeyStroke(&KeyStroke);
					if (RetVal)
						continue;

					if (KeyStroke == KEY_VOLUME_UP || KeyStroke == KEY_VOLUME_DOWN) {
						/* Redraw the recovery UI */
						DrawInitialScreen = TRUE;
						gInitialScreenDisplayed = FALSE;
						PreviousPosition = 0;
						CurrentPosition = 0;

						for (UINT32 i=0; i<PinLength; i++) {
							EnteredPin[i] = 0;
						}

						break;
					}
				}

				gBS->Stall(MDTP_PRESSING_DELAY_USEC);
			}

			else {
				/* Current position is a PIN slot */
				EnteredPin[CurrentPosition] = (EnteredPin[CurrentPosition]+1) % 10;
				MdtpDisplaySelectedDigit(gPinDigitLocationX[CurrentPosition], gPinDigitLocationY, EnteredPin[CurrentPosition]);
				gBS->Stall(MDTP_PRESSING_DELAY_USEC);
			}
		}

		/* Volume down pressed */
		else if (KeyStroke == KEY_VOLUME_DOWN) {
			PreviousPosition = CurrentPosition;
			CurrentPosition = (CurrentPosition+1) % (PinLength+2);

			/* Current position is the help button, previous position was the OK button */
			if (CurrentPosition == PinLength + 1) {
				MdtpDisplayOkButton();
				MdtpDisplaySelectedHelpButton();
				MdtpDisplayHelpInstructions();
			}

			/* Current position is the OK button, previous position was a digit */
			else if (CurrentPosition == PinLength) {
				MdtpClearDigitsInstructions();
				MdtpDisplayDigit(gPinDigitLocationX[PreviousPosition], gPinDigitLocationY, EnteredPin[PreviousPosition]);
				MdtpDisplaySelectedOkButton();
				MdtpDisplayOkInstructions();
			}

			/* Current position is a digit, previous position was the help button */
			else if (CurrentPosition == 0) {
				MdtpClearHelpInstructions();
				MdtpDisplayHelpButton();
				MdtpDisplaySelectedDigit(gPinDigitLocationX[CurrentPosition], gPinDigitLocationY, EnteredPin[CurrentPosition]);
				MdtpDisplayDigitsInstructions();
			}

			/* both the previous and the current positions are PIN slots */
			else {
				MdtpDisplayDigit(gPinDigitLocationX[PreviousPosition], gPinDigitLocationY, EnteredPin[PreviousPosition]);
				MdtpDisplaySelectedDigit(gPinDigitLocationX[CurrentPosition], gPinDigitLocationY, EnteredPin[CurrentPosition]);
			}

			gBS->Stall(MDTP_PRESSING_DELAY_USEC);
		}

		/* No key was pressed (keys other than volume up and volume down are ignored) */
		else if (KeyStroke == KEY_NONE)
			continue;
	}
}

/*----------------------------------------------------------------------------
 * External Functions
 * -------------------------------------------------------------------------*/

/**
 * MdtpRecoveryDialogGetPin
 *
 * Displays the recovery PIN dialog and sets the received buffer
 * with the PIN the user has entered.
 *
 * @param[out] EnteredPin - A buffer holding the received PIN.
 * @param[in]  PinLength - Length of EnteredPin buffer.
 * @param[in]  MasterPin - Indicates whether this is an ISV or a master PIN
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogGetPin(CHAR8 *EnteredPin, UINT32 PinLength, BOOLEAN MasterPin)
{
	MdtpGetPinInterface(EnteredPin, PinLength, MasterPin);

	return;
}

/**
 * MdtpRecoveryDialogDisplayInvalidPin
 *
 * Will be called in case user has entered an invalid PIN.
 * Displays an error message and waits for INVALID_PIN_DELAY_USECONDS
 * before allowing the user to try again.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogDisplayInvalidPin()
{
	MdtpClearOkInstructions();
	MdtpDisplayOkButton();

	MdtpDisplayInvalidPin();

	gBS->Stall(INVALID_PIN_DELAY_USECONDS);
}

/**
 * MdtpRecoveryDialogDisplayErrorMessage
 *
 * Displays an error message and blocks boot process.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogDisplayErrorMessage()
{
	MdtpRecoveryDialogInit();             /* No point in checking the return value here */

	MdtpClearScreen();                    /* No point in checking the return value here */

	if (gScreenResolution.Valid == TRUE)
		MdtpDisplayErrorImage();            /* No point in checking the return value here */

	/* Invalid state. Nothing to be done but contacting the OEM.
	 * Block boot process. */
	DEBUG((EFI_D_ERROR, "MdtpRecoveryDialogDisplayErrorMessage: ERROR, blocking boot process\n"));

	for (;;);
}

/**
 * MdtpRecoveryDialogDisplayTextualErrorMessage
 *
 * Displays a textual error message and blocks boot process.
 * This function will be called in case mdtp image is corrupted.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogDisplayTextualErrorMessage()
{
	MdtpStatus                  RetVal;
	UINTN                       Rows = 0;
	UINTN                       Columns = 0;
	UINTN                       FirstLineLocation;
	CHAR8                       *MainText = "Device unable to boot";
	CHAR8                       *ErrorMsg = "\nError: mdtp image is corrupted\n";

	MdtpClearScreen();                    /* No point in checking the return value here */

	RetVal = MdtpGetScreenLayout(&Rows, &Columns);
	if ((!RetVal)) {
		FirstLineLocation = ISV_PARAMS_RELATIVE_Y_LOCATION*Rows;

		MdtpPrintStringInCoordinates(MainText, 0, FirstLineLocation);                              /* No point in checking the return value here */
		MdtpPrintStringInCoordinates(ErrorMsg, 0, FirstLineLocation + SPACE_BETWEEN_TEXT_LINES);   /* No point in checking the return value here */
	}

	/* Invalid state. Nothing to be done but contacting the OEM.
	 * Stop boot process. */
	DEBUG((EFI_D_ERROR, "MdtpRecoveryDialogDisplayTextualErrorMessage: ERROR, mdtp image corrupted, blocking boot process\n"));

	for (;;);
}

/**
 * MdtpRecoveryDialogShutdown
 *
 * Releases resources that were allocated for the recovery dialog.
 * This function should be called only if the user has entered
 * a valid PIN, therefore the recovery dialog is not longer
 * needed and the device boots.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogShutdown()
{
	if (gMdtpImage) {
		gBS->FreePool(gMdtpImage);
		gMdtpImage = NULL;
	}
}
