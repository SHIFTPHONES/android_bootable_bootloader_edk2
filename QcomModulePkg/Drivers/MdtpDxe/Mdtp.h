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

#ifndef __MDTP_H__
#define __MDTP_H__

#include <Protocol/EFIMdtp.h>

#define INITIAL_DELAY_USECONDS      (5*1000*1000)       /* 5 seconds */
#define INVALID_PIN_DELAY_USECONDS  (5*1000*1000)       /* 5 seconds */
#define MDTP_PIN_LEN                (8)


#ifdef MDTP_SUPPORT
#ifndef VERIFIED_BOOT
#error MDTP feature requires VERIFIED_BOOT feature
#endif
#endif

/*-------------------------------------------------------------------------
 * External Functions
 *-------------------------------------------------------------------------
 */

/**
 * MdtpVerifyFwlock
 *
 * Entry point of the MDTP Firmware Lock.
 * If needed, verify the DIP and all protected partitions.
 * Allow passing information about partition verified using an external method
 * (either boot or recovery). For boot and recovery, either use aboot's
 * verification result, or use boot_verifier APIs to verify internally.
 *
 * @param[in]  ExtPartition - External partition to verify
 *
 * @return - negative value for an error, 0 for success.
 */
EFI_STATUS MdtpVerifyFwlock(MDTP_VB_EXTERNAL_PARTITION *ExtPartition);

/**
 * MdtpGetState
 *
 * Returns MDTP state.
 *
 * @param[in]  MdtpState - Set to MDTP state.
 *
 * @return - negative value for an error, 0 for success.
 */
EFI_STATUS MdtpGetState(MDTP_SYSTEM_STATE *MdtpState);


#endif /* __MDTP_QSEE_H__ */
