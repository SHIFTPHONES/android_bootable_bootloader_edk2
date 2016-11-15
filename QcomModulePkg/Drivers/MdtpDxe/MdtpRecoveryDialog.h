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

#ifndef __MDTP_RECOVERY_DIALOG_H__
#define __MDTP_RECOVERY_DIALOG_H__


/* Image relative locations */
#define ERROR_MESSAGE_RELATIVE_Y_LOCATION   (0.18)
#define MAIN_TEXT_RELATIVE_Y_LOCATION       (0.33)
#define PIN_RELATIVE_Y_LOCATION             (0.47)
#define PIN_TEXT_RELATIVE_Y_LOCATION        (0.57)
#define BUTTON_RELATIVE_Y_LOCATION          (0.75)
#define OK_TEXT_RELATIVE_Y_LOCATION         (0.82)
#define HELP_TEXT_RELATIVE_Y_LOCATION       (0.82)

#define TEXTUAL_ERROR_RELATIVE_Y_LOCATION   (0.25)
#define ISV_PARAMS_RELATIVE_Y_LOCATION      (0.5)
#define SPACE_BETWEEN_TEXT_LINES            (2)

#define MDTP_PRESSING_DELAY_USEC            (400*1000)   /* 400 mseconds */
#define MDTP_MAX_IMAGE_SIZE_BMP             (1183000)    /* Max Bmp size in bytes, including some extra bytes */
#define MDTP_MAX_IMAGE_SIZE_BLT             (1578000)    /* Max Blt size in bytes. Blt requires 4 bytes per pixel */
#define BYTES_PER_PIXEL_BMP                 (3)
#define BYTES_PER_PIXEL_BLT                 (4)

#define NUM_BUTTONS                         (2)
#define OK_BUTTON_ID                        (1)
#define HELP_BUTTON_ID                      (2)

#define CENTER_IMAGE_ON_X_AXIS(ImageWidth,SreenWidth)         (((SreenWidth)-(ImageWidth))/2)
#define ALIGN_MULTI_ON_X_AXIS(ImageWidth,SreenWidth,n,i)      ((SreenWidth)*(i)/(n+1)-(ImageWidth)/2)

/*-------------------------------------------------------------------------
 * Definitions
 *-------------------------------------------------------------------------
 */

typedef struct {
	UINT8 ImageBmp[MDTP_MAX_IMAGE_SIZE_BMP];
	UINT8 ImageBlt[MDTP_MAX_IMAGE_SIZE_BLT];
	UINT32 Width;
	UINT32 Height;
} MdtpImageInfo;

typedef struct {
	UINT32 Width;
	UINT32 Height;
	BOOLEAN Valid;
} ScreenResolution;

/*---------------------------------------------------------
 * External Functions
 *---------------------------------------------------------
 */

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
VOID MdtpRecoveryDialogGetPin(CHAR8 *EnteredPin, UINT32 PinLength, BOOLEAN MasterPin);

/**
 * MdtpRecoveryDialogDisplayInvalidPin
 *
 * Will be called in case user has entered an invalid PIN.
 * Displays an error message and waits for INVALID_PIN_DELAY_USECONDS
 * before allowing the user to try again.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogDisplayInvalidPin();

/**
 * MdtpRecoveryDialogDisplayErrorMessage
 *
 * Displays an error message and blocks boot process.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogDisplayErrorMessage();

/**
 * MdtpRecoveryDialogDisplayTextualErrorMessage
 *
 * Displays a textual error message and blocks boot process.
 * This function will be called in case mdtp image is corrupted.
 *
 * @return - None.
 */
VOID MdtpRecoveryDialogDisplayTextualErrorMessage();

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
VOID MdtpRecoveryDialogShutdown();


#endif /* __MDTP_RECOVERY_DIALOG_H__ */

