/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <Board.h>
#include <Protocol/EFICardInfo.h>
#include <Protocol/EFIPlatformInfoTypes.h>
#include <Library/VerifiedBoot.h>
#include <Library/ShutdownServices.h>
#include <Library/VerifiedBootMenu.h>
#include <Library/DeviceInfo.h>
#include <LinuxLoaderLib.h>


STATIC QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf = NULL;


STATIC EFI_STATUS VerifiedBootInit()
{
	EFI_STATUS Status = EFI_SUCCESS;
	device_info_vb_t DevInfo_vb;
	STATIC BOOLEAN IsInitialized = FALSE;

	if (IsInitialized)
		return Status;

	Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **) &VbIntf);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
		return Status;
	}

	DevInfo_vb.is_unlocked = IsUnlocked();
	DevInfo_vb.is_unlock_critical = IsUnlockCritical();
	Status = VbIntf->VBDeviceInit(VbIntf, (device_info_vb_t *)&DevInfo_vb);
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_ERROR, "Error during VBDeviceInit: %r\n", Status));
	else
		IsInitialized = TRUE;

	return Status;
}

BOOLEAN VerifiedBootEnbled()
{
#ifdef VERIFIED_BOOT
	return TRUE;
#endif
	return FALSE;

}

EFI_STATUS VerifiedBootSendMilestone()
{
	EFI_STATUS Status = EFI_SUCCESS;

	Status = VerifiedBootInit();
	if (Status != EFI_SUCCESS)
		return Status;

	DEBUG((EFI_D_INFO, "Sending Milestone Call\n"));
	Status = VbIntf->VBSendMilestone(VbIntf);
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_INFO, "Error sending milestone call to TZ\n"));

	return Status;
}

EFI_STATUS VerifiedBootImage(VOID *ImageBuffer, UINT32 ImageSize, CHAR8 *PartitionName,
	BOOLEAN IsMdtpActive, CHAR8 *FfbmStr)
{
	EFI_STATUS Status = EFI_SUCCESS;
	QCOM_MDTP_PROTOCOL *MdtpProtocol;
	boot_state_t BootState = BOOT_STATE_MAX;

	Status = VerifiedBootInit();
	if (Status != EFI_SUCCESS)
		return Status;

	Status = VbIntf->VBVerifyImage(VbIntf, (UINT8 *)PartitionName, (UINT8 *) ImageBuffer, ImageSize, &BootState);
	if (Status != EFI_SUCCESS && BootState == BOOT_STATE_MAX)
	{
		DEBUG((EFI_D_ERROR, "VBVerifyImage failed with: %r\n", Status));
		// if MDTP is active Display Recovery UI
		if(IsMdtpActive) {
		    Status = gBS->LocateProtocol(&gQcomMdtpProtocolGuid, NULL, (VOID**)&MdtpProtocol);
		    if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "Failed to locate MDTP protocol, Status=%r\n", Status));
			return Status;
		    }
		    /* Perform Local Deactivation of MDTP */
		    MdtpProtocol->MdtpDeactivate(MdtpProtocol, FALSE);
		}
		return Status;
	}

	DEBUG((EFI_D_VERBOSE, "Boot State is : %d\n", BootState));
	switch (BootState)
	{
		case RED:
			DisplayVerifiedBootMenu(DISPLAY_MENU_RED);
			MicroSecondDelay(5000000);
			ShutdownDevice();
			break;
		case YELLOW:
			DisplayVerifiedBootMenu(DISPLAY_MENU_YELLOW);
			MicroSecondDelay(5000000);
			break;
		case ORANGE:
			if (FfbmStr && FfbmStr[0] != '\0') {
				DEBUG((EFI_D_VERBOSE, "Device will boot into FFBM mode\n"));
			} else {
				DisplayVerifiedBootMenu(DISPLAY_MENU_ORANGE);
				MicroSecondDelay(5000000);
			}
			break;
		default:
			break;
	}

	Status = VbIntf->VBSendRot(VbIntf);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error sending Rot : %r\n", Status));
		return Status;
	}

	return Status;
}
