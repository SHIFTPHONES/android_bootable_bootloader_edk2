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
#include "MdtpInternal.h"
#include "MdtpImageManager.h"

/*---------------------------------------------------------
 * Global Variables
 *-------------------------------------------------------*/

STATIC MdtpImage gMdtpImage;
STATIC BOOLEAN MdtpImageManagerInitialized = FALSE;

/*---------------------------------------------------------
 * Internal Functions
 *-------------------------------------------------------*/

/**
 * Reads Metadata from a given offset.
 * The metadata is either of image sets (to determine the correct
 * image set to use) or of actual images (to be displayed later).
 */
STATIC MdtpStatus MdtpImageManagerReadMetadata(UINT64 Offset)
{
	EFI_STATUS              Status;
	MdtpPartitionHandle     MdtpPartitionHandle;
	UINT8                   *Buffer;
	MdtpStatus              RetVal;
	UINTN                   Index = 0;
	UINT32                  ParamsSize = MAX_PARAMS*sizeof(UINT32);
	UINT32                  ImageDataSize = MAX_IMAGES*sizeof(MdtpImageData);

	Status = gBS->AllocatePool(EfiBootServicesData, MDTP_HEADER_LEN, (VOID**)&Buffer);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerReadMetadata: ERROR, failed to allocate buffer. Status = %r\n", Status));
		return MDTP_STATUS_ALLOCATION_ERROR;
	}

	RetVal = MdtpPartitionGetHandle("mdtp", &MdtpPartitionHandle);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerReadMetadata: ERROR, failed to get MDTP partition handle\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	RetVal = MdtpPartitionRead(&MdtpPartitionHandle, (UINT8*)Buffer, MDTP_HEADER_LEN, Offset);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerReadMetadata: ERROR, failed to read from MDTP partition\n"));
		gBS->FreePool(Buffer);
		return MDTP_STATUS_PARTITION_ERROR;
	}

	/* Initialize params to identify unset values */
	for (Index = 0; Index<MAX_PARAMS; Index++) {
		gMdtpImage.Metadata.Params[Index] = MDTP_PARAM_UNSET_VALUE;
	}

	CopyMem(gMdtpImage.Metadata.Params, Buffer, ParamsSize);
	CopyMem(gMdtpImage.Metadata.ImageData, Buffer + sizeof(gMdtpImage.Metadata.Params), ImageDataSize);

	gBS->FreePool(Buffer);

	DEBUG((EFI_D_INFO, "MdtpImageManagerReadMetadata: Metadata loaded\n"));

	return MDTP_STATUS_SUCCESS;
}

/**
 * Sets a value for a given param in gMdtpImage.
 */
STATIC VOID MdtpImageManagerSetParamValue(MdtpParameterId ParamId, UINT32 Value)
{
	gMdtpImage.Metadata.Params[ParamId] = Value;
}

/*---------------------------------------------------------
 * External Functions
 *-------------------------------------------------------*/

/**
 * MdtpImageManagerGetOffset
 */
UINT32 MdtpImageManagerGetOffset(MdtpImageId ImageId)
{
	return gMdtpImage.Metadata.ImageData[ImageId].Offset;
}

/**
 * MdtpImageManagerGetWidth
 */
UINT32 MdtpImageManagerGetWidth(MdtpImageId ImageId)
{
	return gMdtpImage.Metadata.ImageData[ImageId].Width;
}

/**
 * MdtpImageManagerGetHeight
 */
UINT32 MdtpImageManagerGetHeight(MdtpImageId ImageId)
{
	return gMdtpImage.Metadata.ImageData[ImageId].Height;
}

/**
 * MdtpImageManagerGetParamValue
 */
UINT32 MdtpImageManagerGetParamValue(MdtpParameterId ParamId)
{
	return gMdtpImage.Metadata.Params[ParamId];
}

/**
 * MdtpImageManagerInit
 */
MdtpStatus MdtpImageManagerInit()
{
	MdtpImageData           ImageData;
	MdtpImage               MdtpImageSetsMetadata;
	UINT32                  ImageSetsNum;
	UINT32                  MetadataOffset = 0;
	UINT32                  Width = 0;
	UINT32                  Height = 0;
	UINT32                  Index;
	MdtpStatus              RetVal;

	/* Check if MDTP Image Manager is already initialized */
	if (MdtpImageManagerInitialized == TRUE)
		return MDTP_STATUS_SUCCESS;

	/* Read image sets metadata */
	if (MdtpImageManagerReadMetadata(MetadataOffset)) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, failed to read image sets metadata\n"));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	/* Verify that metadata version is supported */
	if (MdtpImageManagerGetParamValue(VERSION) != SUPPORTED_METADATA_VERSION) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, metadata version is not supported: %d\n", MdtpImageManagerGetParamValue(VERSION)));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	/* Verify that metadata type is as expected */
	if (MdtpImageManagerGetParamValue(TYPE) != IMAGE_SETS) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, unexpected type for image sets metadata: %d\n", MdtpImageManagerGetParamValue(TYPE)));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	ImageSetsNum = MdtpImageManagerGetParamValue(IMAGE_SETS_NUM);
	if (ImageSetsNum < 1) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, invalid number of image sets: %d\n", ImageSetsNum));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	/* Image sets are sorted by screen resolution (width, height), from low to high.
	 * We begin with the smallest image set, and check if bigger image sets also fit the screen. */
	ImageData = gMdtpImage.Metadata.ImageData[0];

	/* Get screen resolution */
	RetVal = MdtpGetScreenResolution(&Width, &Height);

	if (!RetVal && Width != 0 && Height != 0) {
		for (Index = 1; Index<ImageSetsNum;Index++) {

			/* if both width and height still fit the screen, update ImageData */
			if (gMdtpImage.Metadata.ImageData[Index].Width <= Width &&
					gMdtpImage.Metadata.ImageData[Index].Height <= Height) {
				ImageData = gMdtpImage.Metadata.ImageData[Index];
			}

			/* if we reached an image set in which the width is larger than
			 * the screen width, no point in checking additional image sets. */
			else if (gMdtpImage.Metadata.ImageData[Index].Width > Width)
				break;
		}

		DEBUG((EFI_D_INFO, "MdtpImageManagerInit: image set offset: 0x%x\n", ImageData.Offset));
		DEBUG((EFI_D_INFO, "MdtpImageManagerInit: image set width: %d, screen width: %d\n", ImageData.Width, Width));
		DEBUG((EFI_D_INFO, "MdtpImageManagerInit: image set height: %d, screen height: %d\n", ImageData.Height, Height));
	}
	else {
		/* Screen resolution is not available.
		 * This will cause an actual error only when (and if) trying to display MDTP images. */
		DEBUG((EFI_D_INFO, "MdtpImageManagerInit: screen resolution is not available\n"));
	}

	/* Backup image sets metadata for required parameters */
	MdtpImageSetsMetadata = gMdtpImage;

	/* Read images metadata */
	if (MdtpImageManagerReadMetadata(ImageData.Offset)) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, failed to read images metadata\n"));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	/* Verify that metadata version is supported */
	if (MdtpImageManagerGetParamValue(VERSION) != SUPPORTED_METADATA_VERSION) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, metadata version is not supported: %d\n", MdtpImageManagerGetParamValue(VERSION)));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	/* Verify that metadata type is as expected */
	if (MdtpImageManagerGetParamValue(TYPE) != IMAGES) {
		DEBUG((EFI_D_ERROR, "MdtpImageManagerInit: ERROR, unexpected type for images metadata: %d\n", MdtpImageManagerGetParamValue(TYPE)));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	/* Copy vFuse from image sets metadata */
	MdtpImageManagerSetParamValue(VIRTUAL_FUSE, MdtpImageSetsMetadata.Metadata.Params[VIRTUAL_FUSE]);

	/* MDTP Image Manager is now initialized */
	MdtpImageManagerInitialized = TRUE;

	DEBUG((EFI_D_INFO, "MdtpImageManagerInit: MdtpImage loaded successfully\n"));

	return MDTP_STATUS_SUCCESS;
}
