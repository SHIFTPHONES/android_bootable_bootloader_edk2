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

#include <Library/DebugLib.h>
#include "MdtpImageManager.h"
#include "MdtpInternal.h"

/*-------------------------------------------------------------------------
 * Definitions
/*-------------------------------------------------------------------------*/

typedef union
{
	struct {
		UINT8 Enable1       : 1;
		UINT8 Disable1      : 1;
		UINT8 Enable2       : 1;
		UINT8 Disable2      : 1;
		UINT8 Enable3       : 1;
		UINT8 Disable3      : 1;
		UINT8 Reserved1     : 1;
		UINT8 Reserved2     : 1;
	} Bitwise;
	UINT8 Mask;
} MdtpEfuses;

typedef struct {
	MdtpEfuses eFuses;
} MdtpEfusesMetadata;

/*---------------------------------------------------------
 * Internal Functions
 *-------------------------------------------------------*/

/**
 * MdtpGetVfuseMetadata
 *
 * Reads the virtual eFuse metadata stored in the MDTP partition.
 *
 * @param[out] Metadata - A block holding eFuse emulation.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
STATIC MdtpStatus MdtpGetVfuseMetadata(MdtpEfusesMetadata *Metadata)
{
	UINT32 eFuse = MdtpImageManagerGetParamValue(VIRTUAL_FUSE);

	if (eFuse == MDTP_PARAM_UNSET_VALUE) {
		DEBUG((EFI_D_ERROR, "MdtpGetVfuseMetadata: ERROR, failed to read eFuse\n"));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	Metadata->eFuses.Mask = (UINT8)eFuse;

	DEBUG((EFI_D_INFO, "MdtpGetVfuseMetadata: read eFuse successfully\n"));

	return MDTP_STATUS_SUCCESS;
}

/*---------------------------------------------------------
 * External Functions
 *-------------------------------------------------------*/

/**
 * MdtpGetVfuse
 *
 * Returns the value of the virtual eFuse stored in the MDTP partition.
 *
 * @param[out] Enabled - Set to true if MDTP is enabled, false otherwise.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpGetVfuse(BOOLEAN *Enabled)
{
	MdtpEfusesMetadata            Metadata;
	MdtpStatus                    RetVal;

	*Enabled = TRUE;

	RetVal = MdtpGetVfuseMetadata(&Metadata);
	if (RetVal) {
		DEBUG((EFI_D_ERROR, "MdtpGetVfuse: ERROR, failed to get vFuse metadata\n"));
		return MDTP_STATUS_IMAGE_MANAGER_ERROR;
	}

	if (!(Metadata.eFuses.Bitwise.Enable1 && !Metadata.eFuses.Bitwise.Disable1) &&
			!(Metadata.eFuses.Bitwise.Enable2 && !Metadata.eFuses.Bitwise.Disable2) &&
			!(Metadata.eFuses.Bitwise.Enable3 && !Metadata.eFuses.Bitwise.Disable3)) {
		*Enabled = FALSE;
	}

	return MDTP_STATUS_SUCCESS;
}
