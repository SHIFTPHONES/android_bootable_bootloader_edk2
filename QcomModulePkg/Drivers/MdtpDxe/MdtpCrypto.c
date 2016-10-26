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
#include <Protocol/Hash.h>
#include <Protocol/Rng.h>
#include "MdtpInternal.h"

/*---------------------------------------------------------
 * Global Variables
 *-------------------------------------------------------*/

STATIC EFI_HASH_PROTOCOL        *gHashProtocol = NULL;
STATIC EFI_RNG_PROTOCOL         *gRngProtocol = NULL;

/*---------------------------------------------------------
 * External Functions
 *-------------------------------------------------------*/

/**
 * MdtpCryptoHash
 *
 * Calculates Hash value (SHA256).
 *
 * @param[in]   Message - A message to calculate hash for.
 * @param[in]   MessegeSize - Size of Message.
 * @param[out]  Digest - A buffer to hold the hash result.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpCryptoHash(UINT8* Message, UINT64 MessegeSize, EFI_SHA256_HASH *Digest)
{
	EFI_STATUS            Status = EFI_SUCCESS;
	EFI_HASH_OUTPUT       Hash;

	Hash.Sha256Hash = Digest;

	if (!gHashProtocol) {
		Status = gBS->LocateProtocol(&gEfiHashProtocolGuid, NULL, (VOID**)&gHashProtocol);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpCryptoHash: ERROR, locate HashProtocol failed, Status = %r\n", Status));
			gHashProtocol = NULL;
			return MDTP_STATUS_CRYPTO_ERROR;
		}
	}

	Status = gHashProtocol->Hash(gHashProtocol, &gEfiHashAlgorithmSha256Guid, FALSE, Message, MessegeSize, &Hash);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpCryptoHash: ERROR, hash failed, Status = %r\n", Status));
		return MDTP_STATUS_CRYPTO_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpCryptoRng
 *
 * Returns random values.
 *
 * @param[out]   RngValue - A buffer to store the random values.
 * @param[in]    RngLength - Size of RngValue.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpCryptoRng(UINT8 *RngValue, UINTN RngLength)
{
	EFI_STATUS            Status = EFI_SUCCESS;

	if (!gRngProtocol) {
		Status = gBS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID**)&gRngProtocol);

		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpCryptoRng: ERROR, locate RngProtocol failed, Status = %r\n", Status));
			gRngProtocol = NULL;
			return MDTP_STATUS_CRYPTO_ERROR;
		}
	}

	Status = gRngProtocol->GetRNG(gRngProtocol, NULL, RngValue, RngLength);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpCryptoRng: ERROR, GetRNG failed, Status = %r\n", Status));
		return MDTP_STATUS_CRYPTO_ERROR;
	}

	return MDTP_STATUS_SUCCESS;
}

