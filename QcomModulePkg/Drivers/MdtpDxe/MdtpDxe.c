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

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EFIMdtp.h>
#include "Mdtp.h"
#include <Library/StackCanary.h>

EFI_HANDLE  mMdtpHandle = NULL;

/**
  QCOM_MDTP_GET_STATE ()

  @brief
  QCOM_MDTP_GET_STATE implementation of QCOM_MDTP_PROTOCOL
 */
EFI_STATUS
EFIAPI
QcomMdtpGetState
(
    IN  QCOM_MDTP_PROTOCOL        *This,
    OUT MDTP_SYSTEM_STATE         *MdtpState
)
{
	DEBUG((EFI_D_VERBOSE, "QcomMdtpGetState: Started\n"));

	return MdtpGetState(MdtpState);
}

/**
  QCOM_MDTP_VERIFY ()

  @brief
  QCOM_MDTP_VERIFY implementation of QCOM_MDTP_PROTOCOL
 */
EFI_STATUS
EFIAPI
QcomMdtpVerify
(
    IN QCOM_MDTP_PROTOCOL           *This,
    IN MDTP_VB_EXTERNAL_PARTITION   *ExternalPartition
)
{
	DEBUG((EFI_D_VERBOSE, "QcomMdtpVerify: Started\n"));

	return MdtpVerifyFwlock(ExternalPartition);
}

/**
  QCOM_MDTP_PROTOCOL Protocol implementation
 */
QCOM_MDTP_PROTOCOL MdtpProtocolImplementation =
{
	QCOM_MDTP_PROTOCOL_REVISION,
	QcomMdtpGetState,
	QcomMdtpVerify
};

/**
  MdtpDxe driver initialization, it is also the entry point of MdtpDxe driver.

  @param[in] ImageHandle        The firmware allocated handle for the EFI image.
  @param[in] SystemTable        A pointer to the EFI System Table.

  @retval EFI_SUCCESS           Initialization done successfully
  @retval non EFI_SUCCESS       Initialization failed.

 **/
EFI_STATUS MdtpDxeInitialize (
    IN EFI_HANDLE         ImageHandle,
    IN EFI_SYSTEM_TABLE   *SystemTable
)
{
	EFI_STATUS    Status = EFI_SUCCESS;

	DEBUG((EFI_D_INFO, "MdtpDxeInitialize: Started\n"));

	StackGuardChkSetup();

	/* Install our protocols.  The device path protocol is required for this driver
	 * to show up to the console. */
	Status = gBS->InstallMultipleProtocolInterfaces(&mMdtpHandle,
			&gQcomMdtpProtocolGuid,
			&MdtpProtocolImplementation,
			NULL);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpDxeInitialize: Install Protocol Interfaces failed, Status = %r\n", Status));
		goto ErrorExit;
	}

ErrorExit:
	return Status;
}

