/** @file

  Copyright (c) 2013-2014, ARM Ltd. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

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

#include <Uefi.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/EFIUsbDevice.h>
#include <Library/DebugLib.h>
#include <Library/LinuxLoaderLib.h>

#include "UsbDescriptors.h"
#include "FastbootCmds.h"
#include "FastbootMain.h"

#define USB_BUFF_SIZE 1024*1024*1

/* Global fastboot data */
static FastbootDeviceData Fbd;

FastbootDeviceData GetFastbootDeviceData()
{
	return Fbd;
}

/* Dummy function needed for event notification callback */
VOID DummyNotify (IN EFI_EVENT Event,IN VOID *Context)
{
}

EFI_STATUS
FastbootUsbDeviceStart(VOID)
{
  EFI_STATUS                    Status;
  USB_DEVICE_DESCRIPTOR         *DevDesc;
  VOID                          *Descriptors;
  EFI_EVENT                     UsbConfigEvt;
  EFI_GUID                      UsbDeviceProtolGuid =
                                { 0xd9d9ce48, 0x44b8, 0x4f49,
                                { 0x8e, 0x3e, 0x2a, 0x3b, 0x92, 0x7d, 0xc6, 0xc1 } };
  EFI_GUID                      InitUsbControllerGuid =
                                { 0x1c0cffce, 0xfc8d, 0x4e44,
                                { 0x8c, 0x78, 0x9c, 0x9e, 0x5b, 0x53, 0xd,  0x36 } };

  Status = gBS->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DummyNotify, NULL, &InitUsbControllerGuid, &UsbConfigEvt);
  if (EFI_ERROR(Status))
  {
    DEBUG((EFI_D_ERROR, "Usb controller init event not signaled: %r\n", Status));
    return Status;
  }
  else
  {
    gBS->SignalEvent(UsbConfigEvt);
    gBS->CloseEvent(UsbConfigEvt);
  }

  /* Locate the USBFastboot  Protocol from DXE */
  Status = gBS->LocateProtocol( &UsbDeviceProtolGuid, 
                                NULL,
                                (VOID **) &Fbd.UsbDeviceProtocol);
  if (Status != EFI_SUCCESS)
  {
    DEBUG((EFI_D_ERROR, "couldnt find USB device protocol, exiting now"));
    return Status;
  }

  /* Build the descriptor for fastboot */
  BuildDefaultDescriptors(&DevDesc, &Descriptors);
  
  /* Start the usb device */
  Status = Fbd.UsbDeviceProtocol->Start(DevDesc, &Descriptors, &DeviceQualifier, NULL, 5, StrDescriptors);

  /* Allocate buffers required to receive the data from Host*/
  Status = Fbd.UsbDeviceProtocol->AllocateTransferBuffer(USB_BUFF_SIZE, &Fbd.gRxBuffer);
  if (EFI_ERROR(Status))
  {
     DEBUG((EFI_D_ERROR, "Error Allocate RX buffer, cannot enter fastboot mode\n"));
     return EFI_OUT_OF_RESOURCES;
  }

  /* Allocate buffers required to send data from device to Host*/
  Status = Fbd.UsbDeviceProtocol->AllocateTransferBuffer(USB_BUFF_SIZE, &Fbd.gTxBuffer);
  if (EFI_ERROR(Status))
  {
     DEBUG((EFI_D_ERROR, "Error Allocate TX buffer, cannot enter fastboot mode\n"));
     return EFI_OUT_OF_RESOURCES;
  }

  DEBUG((EFI_D_ERROR, "fastboot: processing commands\n"));

  return Status;
}

/* API to stop USB device when booting to kernel, used for "fastboot boot" */
EFI_STATUS
FastbootUsbDeviceStop( VOID )
{
  EFI_STATUS     Status;

  /* Free the Rx & Tx Buffers */
  Status = Fbd.UsbDeviceProtocol->FreeTransferBuffer(Fbd.gTxBuffer);
  ASSERT(!EFI_ERROR(Status));
  Status = Fbd.UsbDeviceProtocol->FreeTransferBuffer(Fbd.gRxBuffer);
  ASSERT(!EFI_ERROR(Status));
  return Status;
}

/* Process bulk transfer out come for Rx */
EFI_STATUS
ProcessBulkXfrCompleteRx(
  IN  USB_DEVICE_TRANSFER_OUTCOME *Uto
)
{
  EFI_STATUS Status = EFI_SUCCESS;
  
  // switch on the transfer status
  switch (Uto->Status) 
  {
    case UsbDeviceTransferStatusCompleteOK:
      DataReady(Uto->BytesCompleted, Fbd.gRxBuffer);
      break;

    case UsbDeviceTransferStatusCancelled:
      //if usb connected, retry, otherwise wait to get connected, then retry
      DEBUG((EFI_D_ERROR, "Bulk in XFR aborted\n"));
      Status = EFI_ABORTED;
      break;

    default: // Other statuses should not occur
      Status = EFI_DEVICE_ERROR;
      break;
  }
  return Status;
}

/* Process bulk transfer out come for Tx */
EFI_STATUS
ProcessBulkXfrCompleteTx(
  IN  USB_DEVICE_TRANSFER_OUTCOME *Uto
)
{
  EFI_STATUS  Status = EFI_SUCCESS;

  // Switch on the transfer status
  switch (Uto->Status) {
    case UsbDeviceTransferStatusCompleteOK:
      DEBUG((EFI_D_ERROR, "UsbDeviceTransferStatusCompleteOK\n"));
      /* Just Queue the next recieve, must be a Command */
      Status = Fbd.UsbDeviceProtocol->Send(ENDPOINT_IN, GetXfrSize() , Fbd.gRxBuffer);
      break;

    case UsbDeviceTransferStatusCancelled:
      DEBUG((EFI_D_ERROR, "Bulk in xfr aborted"));
      Status = EFI_DEVICE_ERROR;
      break;

    default: // Other statuses should not occur
      DEBUG((EFI_D_ERROR, "unhandled trasnfer status"));
      Status = EFI_DEVICE_ERROR;
      break;
  }
  return Status;
}

/* Handle USB events, this will keep looking for events from USB protocol */
EFI_STATUS HandleUsbEvents()
{
  EFI_STATUS                      Status     = EFI_SUCCESS;
  USB_DEVICE_EVENT                Msg;
  USB_DEVICE_EVENT_DATA           Payload;
  UINTN                           PayloadSize;

  /* Look for Event from Usb device protocol */
  Fbd.UsbDeviceProtocol->HandleEvent(&Msg, &PayloadSize, &Payload);
  if (UsbDeviceEventDeviceStateChange == Msg) 
  {
    if (UsbDeviceStateConnected == Payload.DeviceState) {
      DEBUG ((EFI_D_VERBOSE, "Fastboot Device connected\n"));
      /* Queue receive buffer */
      Status = Fbd.UsbDeviceProtocol->Send(0x1, 511, Fbd.gRxBuffer);
    }
    if (UsbDeviceStateDisconnected == Payload.DeviceState) {
      DEBUG ((EFI_D_VERBOSE, "Fastboot Device disconnected\n"));
    }
  }
  else if (UsbDeviceEventTransferNotification == Msg) {
    /* Check if the transfer notification is on the Bulk EP and process it*/
    if (1 == USB_INDEX_TO_EP(Payload.TransferOutcome.EndpointIndex)) {
      /* If the direction is from host to device then process RX */
      if (USB_ENDPOINT_DIRECTION_OUT == USB_INDEX_TO_EPDIR(Payload.TransferOutcome.EndpointIndex)) {

        Status = ProcessBulkXfrCompleteRx(&Payload.TransferOutcome);
        if (EFI_ERROR(Status))
        {
          /* Should not happen, even if it happens we keep waiting for USB to be connected */
          DEBUG((EFI_D_ERROR, "Error, should not happen! Check your USB connection"));
        }
      } 
      else {
        /* Else the direction is from device to host,  process TX */
        Status = ProcessBulkXfrCompleteTx(&Payload.TransferOutcome);
        if (EFI_ERROR(Status))
        {
          /* Should not happen, even if it happens we keep waiting for USB to be connected */
          DEBUG((EFI_D_ERROR, "Error, should not happen! Check your USB connection"));
        }
      }
    }
  }
  return Status;
}

/* Entry point for Fastboot App */
EFI_STATUS
EFIAPI
FastbootAppEntryPoint(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
 )
{
  EFI_STATUS                      Status     = EFI_SUCCESS;

  /* Any fastboot initiazation if needed */
  Status = FastbootAppInit();
  if (Status  != EFI_SUCCESS)
  {
    DEBUG((EFI_D_ERROR, "couldnt init fastboot , exiting"));
    return Status;
  }
  /* Start the USB device enumeration */
  Status = FastbootUsbDeviceStart();
  if (Status  != EFI_SUCCESS)
  {
    DEBUG((EFI_D_ERROR, "couldnt Start fastboot usb device, exiting"));
    return Status;
  }

  /* Wait for USB events in tight loop */
  while (1)
  {
    Status = HandleUsbEvents();

    if (FastbootFatal()) 
    {
      DEBUG((EFI_D_ERROR, "Continue detected, Exiting App...\n"));
      break;
    }
  }

  /* Close the fastboot app and stop USB device */
  Status = FastbootAppUnInit();
  if (Status  != EFI_SUCCESS)
  {
    DEBUG((EFI_D_ERROR, "couldnt uninit fastboot\n"));
    return Status;
  }

  Status = FastbootUsbDeviceStop();
  return Status;
}
