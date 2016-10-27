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

#ifndef __MDTP_IMAGE_MANAGER_H__
#define __MDTP_IMAGE_MANAGER_H__

#include "MdtpError.h"

#define MAX_IMAGES                      (40)
#define MDTP_HEADER_LEN                 (4096)
#define META_DATA_PARTITION_LEN         (2048)
#define MAX_PARAMS                      (512)
#define MDTP_PARAM_UNSET_VALUE          (111)
#define SUPPORTED_METADATA_VERSION      (1)

/*-------------------------------------------------------------------------
 * Definitions
/*-------------------------------------------------------------------------*/

/*
MDTP image layout:
- The mdtp image file contains two layers that both include MdtpImage headers:
    1. The first header includes the image sets metadata.
    2. Once we decided which image set we would like to display, we read the metadata
       of that specific image set (contains metadata of the actual images).
- The MdtpImage header is a fixed length of 4096 Bytes.
- The MdtpImage header is divided into 2 partitions:
    1. MDTP parameters (vFuse, digit-space, etc..)
    2. Images/image sets metadata (offset, width, height)
- Each partition size is 2048 Bytes.
- Each parameter is 4 Bytes long, 512 params max.
- Each metadata parameter (offset/width/height) is 4 Bytes long.
 */

typedef struct {
	UINT32 Offset;
	UINT32 Width;
	UINT32 Height;
} MdtpImageData;


typedef struct {
	UINT32 Params[MAX_PARAMS];
	MdtpImageData ImageData[MAX_IMAGES];
} MdtpMetadata;


typedef union {
	MdtpMetadata Metadata;
	UINT8 Header[MDTP_HEADER_LEN];      /* To make sure the header length is exactly MDTP_HEADER_LEN */
} MdtpImage;


typedef enum {
	ACCEPTEDIT_TEXT = 0,
	ALERT_MESSAGE = 1,
	BTN_OK_OFF = 2,
	BTN_OK_ON = 3,
	MAINTEXT_5SECONDS = 4,
	MAINTEXT_ENTERPIN = 5,
	MAINTEXT_INCORRECTPIN = 6,
	PINTEXT = 7,
	PIN_SELECTED_0 = 8,
	PIN_SELECTED_1 = 9,
	PIN_SELECTED_2 = 10,
	PIN_SELECTED_3 = 11,
	PIN_SELECTED_4 = 12,
	PIN_SELECTED_5 = 13,
	PIN_SELECTED_6 = 14,
	PIN_SELECTED_7 = 15,
	PIN_SELECTED_8 = 16,
	PIN_SELECTED_9 = 17,
	PIN_UNSELECTED_0 = 18,
	PIN_UNSELECTED_1 = 19,
	PIN_UNSELECTED_2 = 20,
	PIN_UNSELECTED_3 = 21,
	PIN_UNSELECTED_4 = 22,
	PIN_UNSELECTED_5 = 23,
	PIN_UNSELECTED_6 = 24,
	PIN_UNSELECTED_7 = 25,
	PIN_UNSELECTED_8 = 26,
	PIN_UNSELECTED_9 = 27,
	BTN_HELP_OFF = 28,
	BTN_HELP_ON = 29,
	HELP_TEXT = 30,
	MAINTEXT_ENTERMASTERPIN = 31,
	PRESS_ANY_KEY_TEXT = 32
} MdtpImageId;


typedef enum {
	VIRTUAL_FUSE = 0,
	DIGIT_SPACE = 1,
	VERSION = 2,
	TYPE = 3,
	IMAGE_SETS_NUM = 4
} MdtpParameterId;


typedef enum {
	IMAGES = 1,
	IMAGE_SETS = 2
} MdtpMetadataType;


/*---------------------------------------------------------
 * External Functions
 *-------------------------------------------------------*/

/**
 * MdtpImageManagerInit
 *
 * Initializes MdtpManager.
 * Reads MDTP metadata from the MDTP partition, and selects the
 * best-fit image set based on the screen resolution.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpImageManagerInit();

/**
 * MdtpImageManagerGetOffset
 *
 * Returns the offset of a given image.
 *
 * @param[in]  ImageId - Id of the required image.
 *
 * @return - offset of the required image.
 */
UINT32 MdtpImageManagerGetOffset(MdtpImageId ImageId);

/**
 * MdtpImageManagerGetWidth
 *
 * Returns the width of a given image.
 *
 * @param[in]  ImageId - Id of the required image.
 *
 * @return - width of the required image.
 */
UINT32 MdtpImageManagerGetWidth(MdtpImageId ImageId);

/**
 * MdtpImageManagerGetHeight
 *
 * Returns the height of a given image.
 *
 * @param[in]  ImageId - Id of the required image.
 *
 * @return - height of the required image.
 */
UINT32 MdtpImageManagerGetHeight(MdtpImageId ImageId);

/**
 * MdtpImageManagerGetParamValue
 *
 * Returns the value of a given param.
 *
 * @param[in]  ParamId - Id of the required param.
 *
 * @return - value of the required param.
 */
UINT32 MdtpImageManagerGetParamValue(MdtpParameterId ParamId);


#endif /* __MDTP_IMAGE_MANAGER_H__ */
