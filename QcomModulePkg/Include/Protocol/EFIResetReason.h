/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef __EFIRESETREASON_H__
#define __EFIRESETREASON_H__

/** @cond */
typedef struct _EFI_RESETREASON_PROTOCOL EFI_RESETREASON_PROTOCOL;
/** @endcond */

/** @addtogroup efi_resetReason_constants 
@{ */
/**
  Protocol version. 
*/
#define EFI_RESETREASON_PROTOCOL_REVISION 0x0000000000010000
/** @} */ /* end_addtogroup efi_resetReason_constants */

/*  Protocol GUID definition */
/** @ingroup efi_resetReason_protocol */
#define EFI_RESETREASON_PROTOCOL_GUID \
   { 0xA022155A, 0x4828, 0x4535, { 0xA4, 0x99, {0x11, 0xF1, 0x52, 0x40, 0xB9, 0x1B} } }
  
/** @cond */
/**
  External reference to the RESETREASON Protocol GUID defined 
  in the .dec file. 
*/
extern EFI_GUID gEfiResetReasonProtocolGuid;
/** @endcond */

/** @} */ /* end_addtogroup efi_resetReason_data_types */

/*==============================================================================

                             API IMPLEMENTATION

==============================================================================*/

/* ============================================================================
**  Function : EFI_ResetReason_GetResetReason
** ============================================================================
*/
/** @ingroup efi_resetReason_getResetReason
  @par Summary
  Gets the reset reason
    
  @param[in]     This                     Pointer to the EFI_RESETREASON_PROTOCOL instance.
  @param[in out] ResetReasonData          Pointer to Reset Data, allocated by caller
  @param[in out] ResetReasonDataSize      Pointer to UINTN that has size of ResetReasonData on 
                                          output, and size of input ResetReasonData buffer on input

  @return
  EFI_SUCCESS           -- Function completed successfully. \n
  EFI_BUFFER_TOO_SMALL  -- ResetReason Data Buffer is not large enough for reason
  EFI_INVALID_PARAMETER -- Input parameter is INVALID. \n
  EFI_PROTOCOL_ERROR    -- Error occurred during the operation.
*/
typedef 
EFI_STATUS
(EFIAPI *EFI_RESETREASON_GETRESETREASON)(
   IN EFI_RESETREASON_PROTOCOL *This,
   IN OUT  VOID                *ResetReasonData,
   IN OUT  UINTN               *ResetReasonDataSize
   );

/*===========================================================================
  PROTOCOL INTERFACE
===========================================================================*/
/** @ingroup efi_resetReason_protocol 
  @par Summary
  Ram Information Protocol interface.

  @par Parameters
*/
struct _EFI_RESETREASON_PROTOCOL {
   UINT64                                  Revision;
   EFI_RESETREASON_GETRESETREASON          GetResetReasons;
}; 

#endif /* __EFIRESETREASON_H__ */

