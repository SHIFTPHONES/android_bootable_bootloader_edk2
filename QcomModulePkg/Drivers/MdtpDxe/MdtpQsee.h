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

#ifndef __MDTP_QSEE_H__
#define __MDTP_QSEE_H__

#include "Mdtp.h"
#include "MdtpError.h"

#define MDTP_MAX_BLOCKS                 (512)
#define MDTP_MAX_PARTITIONS             (3)
#define MDTP_MAX_PARTITION_NAME_LEN     (100)
#define MDTP_HASH_LEN                   (32)
#define MDTP_MAX_DIP_LEN                (1024*128)
#define MDTP_MAX_FILES                  (100)
#define MDTP_MAX_FILE_NAME_LEN          (100)
#define MDTP_ISV_DEVICE_ID_LEN          (32)
#define MDTP_ISV_NAME_LEN               (100)
#define MDTP_ISV_FRIENDLY_NAME_LEN      (64)

/*-------------------------------------------------------------------------*/

#define MDTP_CMD_OFFSET 0x10000
#define MDTP_LOCKOUT_PERIOD (5000)

/*-------------------------------------------------------------------------
 * Definitions
 *-------------------------------------------------------------------------
 */

/** Commands */
typedef enum {
	MDTP_GET_STATE_CMD = MDTP_CMD_OFFSET,
	MDTP_DEACTIVATE_LOCAL_CMD,
	MDTP_GET_ID_TOKEN_CMD,
	MDTP_PROCESS_SIGNED_MSG_CMD,
	MDTP_UPDATE_CRL_CMD,
	MDTP_SET_FWLOCK_CONFIG_CMD,
	MDTP_COMPLETE_FWLOCK_CMD,
	MDTP_PROCESS_QMI_CONFIG_IND_CMD,
	MDTP_PROCESS_QMI_RESPONSE_CMD,
	MDTP_PROCESS_CARD_DATA_CMD,
	MDTP_QMI_REQUEST_FAILED_CMD,
	MDTP_CLEANUP_CMD,
	MDTP_GET_FW_BASELINE_CMD,
	MDTP_UPDATE_FW_BASELINE_CMD,
	MDTP_DEACTIVATE_LOCAL_BOOT_CMD,
	MDTP_GET_ISV_PARAMS_CMD,
	MDTP_GET_MASTER_PIN_CMD,
	MDTP_SET_BOOTSTATE_RECOVERY_CMD,
	MDTP_SET_BOOTSTATE_HLOS_CMD,
	MDTP_SET_VFUSE_CMD,
	MDTP_DEBUG_PROVISION_CMD,
	MDTP_DEBUG_GET_DIP_CMD,
	MDTP_DEBUG_GET_ENABLED_CMD,
	MDTP_DEBUG_SET_UT_MODE_CMD,
	MDTP_DEBUG_TEST_EFUSE_CMD,
	MDTP_DEBUG_GET_RPMB_CMD,
	MDTP_CMD_LAST,
	MDTP_CMD_SIZE = 0x7FFFFFFF
} mdtp_cmd_id_t;

#pragma pack(push, mdtp, 1)

/** MDTP error types. */
typedef enum {
	MDTP_SUCCESS = 0,                 /* Success error code. */
	MDTP_FAILURE,                     /* General failure error code. */
	MDTP_MSG_VERIFICATION_FAILURE,    /* Signature verification failure */
	MDTP_PIN_VALIDATION_FAILURE,      /* Local PIN verification failure */
	MDTP_PIN_LOCKOUT_FAILURE,         /* Local PIN lockout failure */
	MDTP_ERR_SIZE = 0x7FFFFFFF
} mdtp_status_t;

/*-------------------------------------------------------------------------*/

/** MDTP_GET_STATE_CMD related definitions */

/** MDTP system state. */
typedef MDTP_SYSTEM_STATE mdtp_system_state_t;

/** MDTP applicative state. */
typedef union mdtp_app_state
{
	struct {
		UINT32 sim_locked     : 1;    /* SIM is locked by MDTP */
		UINT32 emergency_only : 1;    /* Emergency only mdoe is enabled by MDTP */
	};
	UINT32 value;
} mdtp_app_state_t;

/*-------------------------------------------------------------------------*/

/** MDTP_GET_FW_BASELINE_CMD related definitions */
typedef struct dip_buf {
	unsigned char data[MDTP_MAX_DIP_LEN];
	UINT32 len;
} mdtp_dip_buf_t;

typedef enum {
	MDTP_FWLOCK_MODE_SINGLE = 0,
	MDTP_FWLOCK_MODE_BLOCK,
	MDTP_FWLOCK_MODE_FILES,
	MDTP_FWLOCK_MODE_SIZE = 0x7FFFFFFF
} mdtp_fwlock_mode_t;

typedef struct DIP_hash_table_entry {
	unsigned char hash[MDTP_HASH_LEN];                  /* Hash on block */
} DIP_hash_table_entry_t;

typedef struct DIP_partition {
	UINT64 size;                                        /* Partition size */
	char name[MDTP_MAX_PARTITION_NAME_LEN];             /* Partition name */
	UINT8 lock_enabled;                                 /* Partition locked? */
	mdtp_fwlock_mode_t hash_mode;                       /* Hash per IMAGE or BLOCK */
	UINT8 force_verify_block[MDTP_MAX_BLOCKS];          /* Verify only given block numbers. */
	char files_to_protect[MDTP_MAX_FILES][MDTP_MAX_FILE_NAME_LEN];  /* Verify given files */
	UINT32 verify_ratio;                                /* Statistically verify this ratio of blocks */
	DIP_hash_table_entry_t hash_table[MDTP_MAX_BLOCKS]; /* Hash table */
} DIP_partition_t;

typedef struct DIP_partitions {
	DIP_partition_t partitions[MDTP_MAX_PARTITIONS];
} DIP_partitions_t;

/*-------------------------------------------------------------------------*/

/** MDTP_DEACTIVATE_LOCAL_CMD related definitions */
typedef enum {
	MDTP_DAEMON_PENDING_REQ_NONE,
	MDTP_DAEMON_PENDING_REQ_MODEM,
	MDTP_DAEMON_PENDING_REQ_SIZE = 0x7FFFFFFF
} mdtp_daemon_pending_req_status_t;

/*-------------------------------------------------------------------------*/

/** MDTP_DEACTIVATE_LOCAL_BOOT_CMD related definitions */
typedef struct mdtp_pin {
	char data[MDTP_PIN_LEN+1];              /* A null terminated PIN. */
} mdtp_pin_t;

/*-------------------------------------------------------------------------*/

/** MDTP_GET_ISV_PARAMS_CMD related definitions */
typedef struct mdtp_isv_device_id {
	char data[MDTP_ISV_DEVICE_ID_LEN];
} mdtp_isv_device_id_t;

typedef struct mdtp_isv_name {
	char data[MDTP_ISV_NAME_LEN];
} mdtp_isv_name_t;

typedef struct mdtp_isv_friendly_name {
	char data[MDTP_ISV_FRIENDLY_NAME_LEN];
} mdtp_isv_friendly_name_t;

typedef struct mdtp_isv_params {
	mdtp_isv_device_id_t isv_device_id;                             /* Device identifier from ISV */
	mdtp_isv_name_t isv_name;                                       /* ISV name */
	mdtp_isv_friendly_name_t isv_friendly_name;                     /* ISV friendly name */
} mdtp_isv_params_t;

/*-------------------------------------------------------------------------
 * QSEE Command-Response definitions
 *-------------------------------------------------------------------------
 */

typedef struct mdtp_generic_req_s {
	mdtp_cmd_id_t cmd_id;
} mdtp_generic_req_t;

typedef struct mdtp_generic_rsp_s {
	mdtp_status_t ret;
} mdtp_generic_rsp_t;

/*-------------------------------------------------------------------------*/

typedef struct mdtp_get_state_rsp_s {
	mdtp_status_t ret;
	mdtp_system_state_t system_state;
	mdtp_app_state_t app_state;
} mdtp_get_state_rsp_t;

/*-------------------------------------------------------------------------*/

typedef struct mdtp_get_fw_baseline_req_s {
	mdtp_cmd_id_t cmd_id;
	mdtp_dip_buf_t dip_buf;
} mdtp_get_fw_baseline_req_t;

typedef struct mdtp_get_fw_baseline_rsp_s {
	mdtp_status_t ret;
	DIP_partitions_t fw_baseline;
} mdtp_get_fw_baseline_rsp_t;

/*-------------------------------------------------------------------------*/

typedef struct mdtp_deactivate_local_req_s {
	mdtp_cmd_id_t cmd_id;
	mdtp_pin_t pin;
} mdtp_deactivate_local_req_t;

typedef struct mdtp_deactivate_local_rsp_s {
	mdtp_status_t ret;
	mdtp_daemon_pending_req_status_t daemon_req_status;
} mdtp_deactivate_local_rsp_t;

/*-------------------------------------------------------------------------*/

typedef struct mdtp_deactivate_local_boot_req_s {
	mdtp_cmd_id_t cmd_id;
	BOOLEAN master_pin;
	mdtp_pin_t pin;
} mdtp_deactivate_local_boot_req_t;

typedef struct mdtp_deactivate_local_boot_rsp_s {
	mdtp_status_t ret;
	mdtp_dip_buf_t dip_buf;
} mdtp_deactivate_local_boot_rsp_t;

/*-------------------------------------------------------------------------*/

typedef struct mdtp_get_isv_params_rsp_s {
	mdtp_status_t ret;
	mdtp_isv_params_t isv_params;
} mdtp_get_isv_params_rsp_t;

/*-------------------------------------------------------------------------*/

typedef struct mdtp_set_vfuse_req_s {
	mdtp_cmd_id_t cmd_id;
	UINT8 vfuse;
} mdtp_set_vfuse_req_t;

#pragma pack(pop, mdtp)

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
MdtpStatus MdtpQseeLoadSecapp();

/**
 * MdtpQseeUnloadSecapp
 *
 * Unloads the mdtp trusted application.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeUnloadSecapp();

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
MdtpStatus MdtpQseeGetState(mdtp_system_state_t *SystemState, mdtp_app_state_t *AppState);

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
MdtpStatus MdtpQseeGetFwBaseline(mdtp_dip_buf_t *DipBuf, DIP_partitions_t *FwBaseline);

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
MdtpStatus MdtpQseeDeactivateLocalBoot(BOOLEAN MasterPin, mdtp_pin_t *Pin, mdtp_dip_buf_t *DipBuf);

/**
 * MdtpQseeGetIsvParams
 *
 * Returns information generated by the ISV and stored in RPMB.
 *
 * @param[out] IsvParams - The ISV related parameters.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeGetIsvParams(mdtp_isv_params_t *IsvParams);

/**
 * MdtpQseeSetBootstateRecovery
 *
 * Sets the current boot state to RECOVERY, which means that all APIs which
 * are not permitted in recovery will be blocked.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeSetBootstateRecovery();

/**
 * MdtpQseeSetBootstateHlos
 *
 * Sets the current boot state to HLOS, which means that all APIs which
 * are not permitted in HLOS will be blocked.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpQseeSetBootstateHlos();

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
MdtpStatus MdtpQseeSetVfuse(UINT8 Vfuse);


#endif /* __MDTP_QSEE_H__ */
