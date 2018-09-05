/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
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

#include "BootLinux.h"
#include <Library/DebugLib.h>
#include <Protocol/EFIScm.h>
#include <Protocol/scm_sip_interface.h>
#include <Library/HypervisorMvCalls.h>

STATIC BOOLEAN VmEnabled = FALSE;
STATIC HypBootInfo *HypInfo = NULL;

BOOLEAN IsVmEnabled (VOID)
{
  return VmEnabled;
}

HypBootInfo *GetVmData (VOID)
{
  EFI_STATUS Status;
  QCOM_SCM_PROTOCOL *QcomScmProtocol = NULL;
  UINT64 Parameters[SCM_MAX_NUM_PARAMETERS] = {0};
  UINT64 Results[SCM_MAX_NUM_RESULTS] = {0};

  if (IsVmEnabled ()) {
    return HypInfo;
  }

  DEBUG ((EFI_D_INFO, "GetVmData: making ScmCall to get HypInfo\n"));
  /* Locate QCOM_SCM_PROTOCOL */
  Status = gBS->LocateProtocol (&gQcomScmProtocolGuid, NULL,
                                (VOID **)&QcomScmProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "GetVmData: Locate SCM Protocol failed, "
                         "Status: (0x%x)\n", Status));
    return NULL;
  }

  /* Make ScmSipSysCall */
  Status = QcomScmProtocol->ScmSipSysCall (
      QcomScmProtocol, HYP_INFO_GET_HYP_DTB_ADDRESS_ID,
      HYP_INFO_GET_HYP_DTB_ADDRESS_ID_PARAM_ID, Parameters, Results);

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "GetVmData: No Vm data present! "
                         "Status = (0x%x)\n", Status));
    return NULL;
  }

  HypInfo = (HypBootInfo *)Results[1];
  if (!HypInfo) {
    DEBUG ((EFI_D_ERROR, "GetVmData: ScmSipSysCall returned NULL\n"));
    return NULL;
  }
  VmEnabled = TRUE;

  return HypInfo;
}

/* From Linux Kernel asm/system.h */
#define __asmeq(x, y) ".ifnc " x "," y " ; .err ; .endif\n\t"

/**
 *
 * Control a pipe, including reset, ready and halt functionality.
 *
 * @param pipe_id
 *    The capability identifier of the pipe.
 * @param control
 *    The state control argument.
 *
 * @retval error
 *    The returned error code.
 *
 */
UINT32 HvcSysPipeControl (UINT32 PipeId, UINT32 Control)
{
#if TARGET_ARCH_ARM64
    register UINT32 x0 __asm__ ("x0") = (UINT32) PipeId;
    register UINT32 x1 __asm__ ("x1") = (UINT32) Control;
    __asm__ __volatile__ (
        __asmeq ("%0", "x0")
        __asmeq ("%1", "x1")
        "hvc #5146 \n\t"
        : "+r" (x0), "+r" (x1)
        :
        : "cc", "memory", "x2", "x3", "x4", "x5"
        );

    return (UINT32)x0;
#endif
    return 0;
}

/**
 *
 * Send a message to a microvisor pipe.
 *
 * @param pipe_id
 *    The capability identifier of the pipe.
 * @param size
 *    Size of the message to send.
 * @param data
 *    Pointer to the message payload to send.
 *
 * @retval error
 *    The returned error code.
 *
 */
UINT32 HvcSysPipeSend (UINT32 PipeId, UINT32 Size, CONST UINT8 *Data)
{
#if TARGET_ARCH_ARM64
    register UINT32 x0 __asm__ ("x0") = (UINT32) PipeId;
    register UINT32 x1 __asm__ ("x1") = (UINT32) Size;
    register UINT32 x2 __asm__ ("x2") = (UINT32)(UINTN) Data;
    __asm__ __volatile__ (
        __asmeq ("%0", "x0")
        __asmeq ("%1", "x1")
        __asmeq ("%2", "x2")
        "hvc #5148 \n\t"
        : "+r" (x0), "+r" (x1), "+r" (x2)
        :
        : "cc", "memory", "x3", "x4", "x5"
        );


    return (UINT32)x0;
#endif
    return 0;
}
