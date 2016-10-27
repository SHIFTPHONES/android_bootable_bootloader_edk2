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

#ifndef __MDTP_INTERNAL_H__
#define __MDTP_INTERNAL_H__

#include <Protocol/Hash.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include "MdtpError.h"
#include "MdtpQsee.h"
#include "MdtpRecoveryDialog.h"

#define MDTP_FWLOCK_BLOCK_SIZE      (1024*1024*16)
#define MDTP_PARTITION_START        (0)

/*-------------------------------------------------------------------------
 * Definitions
/*-------------------------------------------------------------------------*/

typedef enum {
	MDTP_BOOT_MODE_BOOT,
	MDTP_BOOT_MODE_RECOVERY,
	MDTP_BOOT_MODE_MAX,
} MdtpBootMode;

typedef struct {
	EFI_DISK_IO_PROTOCOL      *DiskIo;
	EFI_BLOCK_IO_PROTOCOL     *BlockIo;
} MdtpPartitionHandle;

typedef enum {
	KEY_VOLUME_UP,
	KEY_VOLUME_DOWN,
	KEY_NONE
} MdtpKeyStroke;

/*-------------------------------------------------------------------------
 * MDTP Partition Functions
/*-------------------------------------------------------------------------*/

/**
 * MdtpPartitionGetHandle
 *
 * Returns a partition handle for the required partition.
 *
 * @param[in]   PartitionName - A string that contains the partition name.
 * @param[out]  PartitionHandle - A handle for the required partition.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionGetHandle(CHAR8 *PartitionName, MdtpPartitionHandle *PartitionHandle);

/**
 * MdtpPartitionRead
 *
 * Reads bytes from partition.
 *
 * @param[in]   PartitionHandle - A handle for the required partition.
 * @param[out]  Buffer - A buffer to store the read data.
 * @param[in]   BufferSize - Size of Buffer.
 * @param[in]   Offset - The offset to read from.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionRead(MdtpPartitionHandle *PartitionHandle, UINT8* Buffer, UINTN BufferSize, UINT64 Offset);

/**
 * MdtpPartitionWrite
 *
 * Writes bytes from partition.
 *
 * @param[in]   PartitionHandle - A handle for the required partition.
 * @param[out]  Buffer - A buffer that contains the data to write.
 * @param[in]   BufferSize - Size of Buffer.
 * @param[in]   Offset - The offset to write to.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionWrite(MdtpPartitionHandle *PartitionHandle, UINT8* Buffer, UINTN BufferSize, UINT64 Offset);

/**
 * MdtpPartitionReadDIP
 *
 * Read the DIP from EMMC.
 *
 * @param[out]  DipBuffer - A structure to store the DIP data.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionReadDIP(mdtp_dip_buf_t *DipBuffer);

/**
 * MdtpPartitionWriteDIP
 *
 * Writes the DIP the EMMC.
 *
 * @param[out]  DipBuffer - A structure that contains the DIP data to write.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPartitionWriteDIP(mdtp_dip_buf_t *DipBuffer);

/*-------------------------------------------------------------------------
 * MDTP Crypto Functions
/*-------------------------------------------------------------------------*/

/**
 * MdtpCryptoHash
 *
 * Calculatse Hash value (SHA256).
 *
 * @param[in]   Message - A message to calculate hash for.
 * @param[in]   MessegeSize - Size of Message.
 * @param[out]  Digest - A buffer to hold the hash result.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpCryptoHash(UINT8* Message, UINT64 MessegeSize, EFI_SHA256_HASH *Digest);

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
MdtpStatus MdtpCryptoRng(UINT8 *RngValue, UINTN RngLength);

/*-------------------------------------------------------------------------
 * MDTP Fuse Functions
/*-------------------------------------------------------------------------*/

/**
 * MdtpGetVfuse
 *
 * Returns the value of the virtual eFuse stored in the MDTP partition.
 *
 * @param[out] Enabled - Set to true if MDTP is enabled, false otherwise.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpGetVfuse(BOOLEAN *Enabled);

/*-------------------------------------------------------------------------
 * MDTP Recovery Dialog Services Functions
/*-------------------------------------------------------------------------*/

/**
 * MdtpGetScreenResolution
 *
 * Returns the screen resolution (width and height).
 *
 * @param[out] Width - Set to screen width.
 * @param[out] Height - Set to screen height.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpGetScreenResolution(UINT32 *Width, UINT32 *Height);

/**
 * MdtpGetScreenLayout
 *
 * Returns the screen layout (number of rows and columns)
 *
 * @param[out] Rows - Set to number of rows.
 * @param[out] Height - Set to number of columns.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpGetScreenLayout(UINTN *Rows, UINTN *Columns);

/**
 * MdtpDisplayBltImage
 *
 * Displays a Blt Buffer at (x,y) coordinates on the screen.
 *
 * @param[out] BltBuffer - A Blt image to display.
 * @param[out] x - x coordinate of the (x,y) location.
 * @param[out] y - y coordinate of the (x,y) location.
 * @param[out] Width - Image width.
 * @param[out] Height - Image height.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpDisplayBltImage(UINT8 *BltBuffer, UINTN x, UINTN y, UINTN Width, UINTN Height);

/**
 * MdtpClearScreen
 *
 * Clears the screen.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpClearScreen();

/**
 * MdtpClearRectangleSection
 *
 * Clears a rectangle section on the screen.
 * The section is of SectionWidth and SectionHeight dimensions, and will
 * be cleared from the given (x,y) coordinates and down.
 *
 * @param[out] x - x coordinate of the (x,y) location from which the section will be cleared.
 * @param[out] y - y coordinate of the (x,y) location from which the section will be cleared.
 * @param[out] SectionWidth - Width of the section to clear.
 * @param[out] SectionHeight - Height of the section to clear.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpClearRectangleSection(UINTN x, UINTN y, UINTN SectionWidth, UINTN SectionHeight);

/**
 * MdtpReadKeyStroke
 *
 * Returns the latest key stroke.
 *
 * @param[out] KeyStroke - Set to the latest key stroke
 * (only volume up and volume down interest us).
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpReadKeyStroke(MdtpKeyStroke *KeyStroke);

/**
 * MdtpPrintString
 *
 * Prints a string at current cursor position.
 *
 * @param[in] String - String to print.
 * @param[in] Length - String length.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPrintString(CHAR8* String, UINTN Length);

/**
 * MdtpPrintStringInCoordinates
 *
 * Prints a string at (x,y) coordinates on the screen.
 *
 * @param[in] String - String to print.
 * @param[in] Length - String length.
 * @param[in] x - x coordinate of the (x,y) location.
 * @param[in] y - y coordinate of the (x,y) location.
 *
 * @return - MDTP_STATUS_SUCCESS in case of success, error code otherwise.
 */
MdtpStatus MdtpPrintStringInCoordinates(CHAR8* String, UINTN Length, UINTN x, UINTN y);


#endif /* __MDTP_INTERNAL_H__ */

