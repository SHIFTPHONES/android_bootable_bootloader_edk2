/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
 * 
 *@file  EFIChargerEx.h
 *@brief Charger Extended Protocol for UEFI open source.
 */

#ifndef __EFICHARGEREX_H__
#define __EFICHARGEREX_H__

/*=============================================================================
EDIT HISTORY


when       who     what, where, why
--------   ---     ----------------------------------------------------------- 
05/04/16   sm      New file.

=============================================================================*/

/*===========================================================================
MACRO DECLARATIONS
===========================================================================*/
/** @addtogroup efi_ChargerEx_constants 
@{ */
/**
Protocol version.
*/

#define CHARGER_EX_REVISION 0x0000000000010002
/** @} */ /* end_addtogroup efi_ChargerEx_constants */

/*  Protocol GUID definition */
/** @ingroup efi_ChargerEx_protocol */
#define EFI_CHARGER_EX_PROTOCOL_GUID \
    { 0x6edc8a6d, 0x2663, 0x43cd, { 0x90, 0xff, 0x46, 0x21, 0xff, 0xd1, 0x0d, 0xf5 } } 

/** @cond */
/*===========================================================================
EXTERNAL VARIABLES
===========================================================================*/
/**
External reference to the Charger Extended Protocol GUID.
*/
extern EFI_GUID gQcomChargerExProtocolGuid;

/*===========================================================================
TYPE DEFINITIONS
===========================================================================*/
/**
Protocol declaration.
*/
typedef struct _EFI_QCOM_CHARGER_EX_PROTOCOL   EFI_QCOM_CHARGER_EX_PROTOCOL;
/** @endcond */

/** @addtogroup efi_ChargerEx_data_types 
@{ */


/*===========================================================================
FUNCTION DEFINITIONS
===========================================================================*/

/* EFI_CHARGER_EX_GET_CHARGER_PRESENCE */
/**@ingroup efi_charger_ex_get_charger_presence
  @par Summary
  This API returns charger presence status

  @param[out]  pChargerPresent   TRUE:Charger Present, FALSE:Charger not present

  @return
  EFI_SUCCESS            -- Function completed successfully.
  EFI_DEVICE_ERROR       -- Physical device reported an error.
*/
typedef
EFI_STATUS (EFIAPI *EFI_CHARGER_EX_GET_CHARGER_PRESENCE)
(
  OUT BOOLEAN  *pChargerPresent
);


/* EFI_CHARGER_EX_GET_BATTERY_VOLTAGE */
/**@ingroup efi_charger_ex_get_battery_voltage
  @par Summary
  This API returns battery voltage in mV

  @param[out]  pBatteryVoltagemV   Battery voltage in mV

  @return
  EFI_SUCCESS            -- Function completed successfully.
  EFI_DEVICE_ERROR       -- Physical device reported an error.
*/
typedef
EFI_STATUS (EFIAPI *EFI_CHARGER_EX_GET_BATTERY_VOLTAGE)
(
  OUT UINT32  *pBatteryVoltagemV
);

/* EFI_CHARGER_EX_GET_BATTERY_PRESENCE */
/**@ingroup efi_charger_ex_get_battery_presence
  @par Summary
  This API returns battery presence status

  @param[out]  pBatteryPresent   TRUE:Battery Present, FALSE:Battery not present

  @return
  EFI_SUCCESS            -- Function completed successfully.
  EFI_DEVICE_ERROR       -- Physical device reported an error.
*/
typedef
EFI_STATUS (EFIAPI *EFI_CHARGER_EX_GET_BATTERY_PRESENCE)
(
  OUT BOOLEAN  *pBatteryPresent
);


/*===========================================================================
PROTOCOL INTERFACE
===========================================================================*/
/** @ingroup efi_pmicCharger_protocol
@par Summary
PMIC Charger Protocol interface.

@par Parameters
@inputprotoparams{pmic_charger_proto_params.tex} 
*/

struct _EFI_QCOM_CHARGER_EX_PROTOCOL {
  UINT64                                Revision;
  EFI_CHARGER_EX_GET_CHARGER_PRESENCE   GetChargerPresence;    // Returns if charger is present
  EFI_CHARGER_EX_GET_BATTERY_VOLTAGE    GetBatteryVoltage;     // Returns battery voltage
  EFI_CHARGER_EX_GET_BATTERY_PRESENCE   GetBatteryPresence;    // Returns if battery is present
};


#endif  /* __EFICHARGEREX_H__ */
