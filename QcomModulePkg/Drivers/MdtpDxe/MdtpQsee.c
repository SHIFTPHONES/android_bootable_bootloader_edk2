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
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Protocol/EFIQseecom.h>
#include "MdtpQsee.h"

/*---------------------------------------------------------
 * Global Variables
 *---------------------------------------------------------
 */

STATIC QCOM_QSEECOM_PROTOCOL    *gQseecomProtocol = NULL;
STATIC UINT32                   gMdtpQseeSecappHandle = 0;
STATIC BOOLEAN                  gMdtpQseeSecappLoaded = FALSE;

/*---------------------------------------------------------
 * External Functions
 *---------------------------------------------------------
 */

/**
 * MdtpQseeLoadSecapp
 *
 * Loads the mdtp trusted application.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeLoadSecapp()
{
	EFI_STATUS            Status = EFI_SUCCESS;

	/* Check if secapp is already loaded */
	if (gMdtpQseeSecappLoaded == TRUE) {
		DEBUG((EFI_D_INFO, "MdtpQseeLoadSecapp: MDTP secapp is already loaded\n"));
		return MDTP_STATUS_SUCCESS;
	}

	/* Locate Qseecom protocol */
	Status = gBS->LocateProtocol (&gQcomQseecomProtocolGuid, NULL, (VOID**)&gQseecomProtocol);

	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpQseeLoadSecapp: ERROR, locate QcomQseecomProtocol failed, Status = %r\n", Status));
		gQseecomProtocol = NULL;
		return MDTP_STATUS_QSEE_ERROR;
	}

	/* start TZ app */
	Status = gQseecomProtocol->QseecomStartApp(gQseecomProtocol, "mdtpsecapp", &gMdtpQseeSecappHandle);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpQseeLoadSecapp: ERROR, failed to load mdtpsecapp, Status = %r\n", Status));
		return MDTP_STATUS_QSEE_ERROR;
	}

	DEBUG((EFI_D_INFO, "MdtpQseeLoadSecapp: mdtpsecapp loaded successfully\n"));
	gMdtpQseeSecappLoaded = TRUE;
	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpQseeUnloadSecapp
 *
 * Unloads the mdtp trusted application.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeUnloadSecapp()
{
	EFI_STATUS            Status = EFI_SUCCESS;

	/* Check if secapp is already unloaded */
	if (gMdtpQseeSecappLoaded == FALSE) {
		DEBUG((EFI_D_INFO, "MdtpQseeUnloadSecapp: MDTP secapp is already unloaded\n"));
		return MDTP_STATUS_SUCCESS;
	}

	Status = gQseecomProtocol->QseecomShutdownApp(gQseecomProtocol, gMdtpQseeSecappHandle);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "MdtpQseeUnloadSecapp: ERROR, failed to unload mdtpsecapp, Status = %r\n", Status));
		return MDTP_STATUS_QSEE_ERROR;
	}

	DEBUG((EFI_D_INFO, "MdtpQseeUnloadSecapp: mdtpsecapp unloaded successfully\n"));
	gMdtpQseeSecappLoaded = FALSE;
	gMdtpQseeSecappHandle = 0;

	return MDTP_STATUS_SUCCESS;
}

/**
 * MdtpQseeGetState
 *
 * @param[out]  SystemState - Current MDTP system state.
 * @param[out]  AppState - Current MDTP applicative state.
 *
 * Returns the current state of the MDTP feature.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeGetState(mdtp_system_state_t *SystemState, mdtp_app_state_t *AppState)
{
	EFI_STATUS              Status = EFI_SUCCESS;
	mdtp_generic_req_t      *Req = NULL;
	mdtp_get_state_rsp_t    *Rsp = NULL;
	MdtpStatus              RetVal = MDTP_STATUS_SUCCESS;

	if (SystemState == NULL || AppState == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpQseeGetState: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetState: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_get_state_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetState: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetState: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_GET_STATE_CMD;

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status) || Rsp->ret != 0) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetState: ERROR, QseecomSendCmd failed, Status=%r, Rsp->ret=%d\n", Status, Rsp->ret));
			RetVal = MDTP_STATUS_QSEE_ERROR;
		}
		else {
			*SystemState = Rsp->system_state;
			*AppState = Rsp->app_state;
		}
	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeGetState: RetVal=%d\n", RetVal));

	return RetVal;
}

/**
 * MdtpQseeGetFwBaseline
 *
 * Returns a baseline of the protected firmware from the DIP.
 *
 * @param[in]   DipBuf - The content of the DIP partition to read.
 * @param[out]  FwBaseline - A baseline of protected firmware.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeGetFwBaseline(mdtp_dip_buf_t *DipBuf, DIP_partitions_t *FwBaseline)
{
	EFI_STATUS                      Status = EFI_SUCCESS;
	mdtp_get_fw_baseline_req_t      *Req = NULL;
	mdtp_get_fw_baseline_rsp_t      *Rsp = NULL;
	MdtpStatus                      RetVal = MDTP_STATUS_SUCCESS;

	if (DipBuf == NULL || FwBaseline == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpQseeGetFwBaseline: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_get_fw_baseline_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetFwBaseline: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_get_fw_baseline_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetFwBaseline: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetFwBaseline: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_GET_FW_BASELINE_CMD;
		CopyMem(&Req->dip_buf, DipBuf, sizeof(*DipBuf));

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status) || Rsp->ret != 0) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetFwBaseline: ERROR, QseecomSendCmd failed, Status=%r, Rsp->ret=%d\n", Status, Rsp->ret));
			RetVal = MDTP_STATUS_QSEE_ERROR;
		}
		else {
			*FwBaseline = Rsp->fw_baseline;
		}
	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeGetFwBaseline: RetVal=%d\n", RetVal));

	return RetVal;
}

/**
 * MdtpQseeDeactivateLocalBoot
 *
 * Deactivate the MDTP protection manually, using ISV or master PIN.
 *
 * @param[in]   MasterPin - Is this deactivation with ISV or master PIN?
 * @param[in]   Pin: The PIN for deactivation.
 * @param[out]  DipBuf - The content of the DIP partition to write.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeDeactivateLocalBoot(BOOLEAN MasterPin, mdtp_pin_t *Pin, mdtp_dip_buf_t *DipBuf)
{
	EFI_STATUS                              Status = EFI_SUCCESS;
	mdtp_deactivate_local_boot_req_t        *Req = NULL;
	mdtp_deactivate_local_boot_rsp_t        *Rsp = NULL;
	MdtpStatus                              RetVal = MDTP_STATUS_SUCCESS;

	if (DipBuf == NULL || Pin == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_deactivate_local_boot_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_deactivate_local_boot_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_DEACTIVATE_LOCAL_BOOT_CMD;
		Req->pin= *Pin;
		Req->master_pin = MasterPin;

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: ERROR, QseecomSendCmd failed, Status = %r\n", Status));
			RetVal = MDTP_STATUS_QSEE_ERROR;
			break;
		}
		else if (Rsp->ret != 0) {
			/* If in lockout, retry in a few seconds */
			if (Rsp->ret == MDTP_PIN_LOCKOUT_FAILURE) {
				DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: user should retry after lockout, Rsp->ret=%d\n", Rsp->ret));
			}
			/* PIN compare fails */
			else {
				DEBUG((EFI_D_ERROR, "MdtpQseeDeactivateLocalBoot: PIN compare failed, Rsp->ret=%d\n", Rsp->ret));
			}

			RetVal = MDTP_STATUS_QSEE_ERROR;
			break;
		}
		/* PIN compare succeeded */
		else {
			CopyMem(DipBuf, &Rsp->dip_buf, sizeof(Rsp->dip_buf));
			break;
		}
	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeDeactivateLocalBoot: RetVal=%d\n", RetVal));

	return RetVal;
}

/**
 * MdtpQseeGetIsvParams
 *
 * Returns information generated by the ISV and stored in RPMB.
 *
 * @param[out] IsvParams - The ISV related parameters.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeGetIsvParams(mdtp_isv_params_t *IsvParams)
{
	EFI_STATUS                    Status = EFI_SUCCESS;
	mdtp_generic_req_t            *Req = NULL;
	mdtp_get_isv_params_rsp_t     *Rsp = NULL;
	MdtpStatus                    RetVal = MDTP_STATUS_SUCCESS;

	if (IsvParams == NULL) {
		DEBUG((EFI_D_ERROR, "MdtpQseeGetIsvParams: ERROR, parameter is NULL\n"));
		return MDTP_STATUS_INVALID_PARAMETER;
	}

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetIsvParams: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_get_isv_params_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetIsvParams: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetIsvParams: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_GET_ISV_PARAMS_CMD;

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status) || Rsp->ret != 0) {
			DEBUG((EFI_D_ERROR, "MdtpQseeGetIsvParams: ERROR, QseecomSendCmd failed, Status=%r, Rsp->ret=%d\n", Status, Rsp->ret));
			RetVal = MDTP_STATUS_QSEE_ERROR;
		}
		else {
			*IsvParams = Rsp->isv_params;
		}
	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeGetIsvParams: RetVal=%d\n", RetVal));

	return RetVal;
}

/**
 * MdtpQseeSetBootstateRecovery
 *
 * Sets the current boot state to RECOVERY, which means that all APIs which
 * are not permitted in recovery will be blocked.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeSetBootstateRecovery()
{
	EFI_STATUS              Status = EFI_SUCCESS;
	mdtp_generic_req_t      *Req = NULL;
	mdtp_generic_rsp_t      *Rsp = NULL;
	MdtpStatus              RetVal = MDTP_STATUS_SUCCESS;

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateRecovery: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateRecovery: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateRecovery: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_SET_BOOTSTATE_RECOVERY_CMD;

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status) || Rsp->ret != 0) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateRecovery: ERROR, QseecomSendCmd failed, Status=%r, Rsp->ret=%d\n", Status, Rsp->ret));
			RetVal = MDTP_STATUS_QSEE_ERROR;
		}

	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeSetBootstateRecovery: RetVal=%d\n", RetVal));

	return RetVal;
}

/**
 * MdtpQseeSetBootstateHlos
 *
 * Sets the current boot state to HLOS, which means that all APIs which
 * are not permitted in HLOS will be blocked.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeSetBootstateHlos()
{
	EFI_STATUS                  Status = EFI_SUCCESS;
	mdtp_generic_req_t          *Req = NULL;
	mdtp_generic_rsp_t          *Rsp = NULL;
	MdtpStatus                  RetVal = MDTP_STATUS_SUCCESS;

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateHlos: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateHlos: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateHlos: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_SET_BOOTSTATE_HLOS_CMD;

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status) || Rsp->ret != 0) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetBootstateHlos: ERROR, QseecomSendCmd failed, Status=%r, Rsp->ret=%d\n", Status, Rsp->ret));
			RetVal = MDTP_STATUS_QSEE_ERROR;
		}

	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeSetBootstateHlos: RetVal=%d\n", RetVal));

	return RetVal;
}

/**
 * MdtpQseeSetVfuse
 *
 * Sets the value of MDTP virtual eFuse.
 * This will be used in Bootloader, instead of reading the virtual eFuse from MDTP
 * partition.
 *
 * @param[in]  Vfuse - The value of MDTP virtual eFuse.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeSetVfuse(UINT8 Vfuse)
{
	EFI_STATUS                  Status = EFI_SUCCESS;
	mdtp_set_vfuse_req_t        *Req = NULL;
	mdtp_generic_rsp_t          *Rsp = NULL;
	MdtpStatus                  RetVal = MDTP_STATUS_SUCCESS;

	do {
		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_set_vfuse_req_t), (VOID**)&Req);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetVfuse: ERROR, failed to allocate request buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Req = NULL;
			break;
		}

		Status = gBS->AllocatePool(EfiBootServicesData, sizeof(mdtp_generic_rsp_t), (VOID**)&Rsp);
		if (EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetVfuse: ERROR, failed to allocate response buffer, Status = %r\n", Status));
			RetVal = MDTP_STATUS_ALLOCATION_ERROR;
			Rsp = NULL;
			break;
		}

		if (!gMdtpQseeSecappLoaded) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetVfuse: ERROR, secapp is not loaded\n"));
			RetVal = MDTP_STATUS_NOT_INITIALIZED;
			break;
		}

		Req->cmd_id = MDTP_SET_VFUSE_CMD;
		Req->vfuse = Vfuse;

		Status = gQseecomProtocol->QseecomSendCmd(gQseecomProtocol, gMdtpQseeSecappHandle, (UINT8*)Req, sizeof(*Req), (UINT8*)Rsp, sizeof(*Rsp));
		if (EFI_ERROR(Status) || Rsp->ret != 0) {
			DEBUG((EFI_D_ERROR, "MdtpQseeSetVfuse: ERROR, QseecomSendCmd failed, Status=%r, Rsp->ret=%d\n", Status, Rsp->ret));
			RetVal = MDTP_STATUS_QSEE_ERROR;
		}

	} while (0);

	if (Req != NULL)
		gBS->FreePool(Req);
	if (Rsp != NULL)
		gBS->FreePool(Rsp);

	DEBUG((EFI_D_INFO, "MdtpQseeSetVfuse: RetVal=%d\n", RetVal));

	return RetVal;
}
