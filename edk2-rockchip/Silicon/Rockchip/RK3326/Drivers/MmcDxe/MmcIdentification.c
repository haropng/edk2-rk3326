/** @file
  MMC Card Identification and Initialization

  Copyright (c) 2011-2015, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseMemoryLib.h>
#include <Library/TimerLib.h>
#include "Mmc.h"

#define MAX_RETRY_COUNT         1000
#define RCA_SHIFT_OFFSET        16
#define EMMC_CARD_SIZE          512

#define MMC_STATUS_APP_CMD      BIT5

#define EMMC_CMD1_CAPACITY_LESS_THAN_2GB    0x00FF8080U
#define EMMC_CMD1_CAPACITY_GREATER_THAN_2GB 0x40FF8080U

// MMC_CSD helper macros
#define MMC_CSD_GET_CCC(x)          (((x)[3] >> 20) & 0xFFF)
#define MMC_CSD_GET_TRANSPEED(x)    ((x)[0] & 0xFF)
#define MMC_CSD_GET_READBLLEN(x)    (((x)[1] >> 16) & 0xF)
#define MMC_CSD_GET_WRITEBLLEN(x)   (((x)[0] >> 22) & 0xF)
#define MMC_CSD_GET_FILEFORMATGRP(x) (((x)[0] >> 10) & 1)
#define MMC_CSD_GET_FILEFORMAT(x)   (((x)[3] >> 10) & 3)
#define MMC_CSD_GET_DEVICESIZE(x)   ((((x)[1] >> 8) & 0x3FF) | (((x)[2] & 0x3F) << 2) | (((x)[2] >> 30) & 3) << 10)
#define MMC_CSD_GET_DEVICESIZEMULT(x) (((x)[2] >> 15) & 7)
#define HC_MMC_CSD_GET_DEVICESIZE(x)  ((((x)[1] >> 8) & 0x3F) << 16 | (((x)[2] & 0xFF) << 8) | ((x)[2] >> 24))

typedef union {
  UINT32 Raw;
  OCR    Ocr;
} OCR_RESPONSE;

static UINT32 mEmmcRcaCount = 1;

// ── eMMC helpers ────────────────────────────────────────────────────

typedef enum {
  EMMC_IDLE_STATE = 0,
  EMMC_READY_STATE,
  EMMC_IDENT_STATE,
  EMMC_STBY_STATE,
  EMMC_TRAN_STATE,
  EMMC_DATA_STATE,
  EMMC_RCV_STATE,
  EMMC_PRG_STATE,
  EMMC_DIS_STATE,
} EMMC_DEVICE_STATE;

#define DEVICE_STATE(x)  (((x) >> 9) & 0xF)

STATIC EFI_STATUS
EmmcGetDeviceState (
  IN  MMC_HOST_INSTANCE *MmcHostInstance,
  OUT EMMC_DEVICE_STATE *State
  )
{
  EFI_MMC_HOST_PROTOCOL *Host;
  EFI_STATUS Status;
  UINT32 Data, RCA;

  Host = MmcHostInstance->MmcHost;
  RCA = MmcHostInstance->CardInfo.RCA << RCA_SHIFT_OFFSET;
  Status = Host->SendCommand (Host, MMC_CMD13, RCA);
  if (EFI_ERROR (Status)) return Status;

  Status = Host->ReceiveResponse (Host, MMC_RESPONSE_TYPE_R1, &Data);
  if (EFI_ERROR (Status)) return Status;

  *State = (EMMC_DEVICE_STATE)DEVICE_STATE (Data);
  return EFI_SUCCESS;
}

// ── eMMC Identification ─────────────────────────────────────────────

STATIC EFI_STATUS
EmmcIdentificationMode (
  IN MMC_HOST_INSTANCE *MmcHostInstance,
  IN OCR_RESPONSE       Response
  )
{
  EFI_MMC_HOST_PROTOCOL *Host = MmcHostInstance->MmcHost;
  EFI_BLOCK_IO_MEDIA    *Media = MmcHostInstance->BlockIo.Media;
  EFI_STATUS Status;
  EMMC_DEVICE_STATE State;
  UINT32 RCA;
  UINT8  EcsdBuf[512];

  // Fetch CID
  Status = Host->SendCommand (Host, MMC_CMD2, 0);
  if (EFI_ERROR (Status)) return Status;
  Status = Host->ReceiveResponse (Host, MMC_RESPONSE_TYPE_R2, (UINT32 *)&MmcHostInstance->CardInfo.CIDData);
  if (EFI_ERROR (Status)) return Status;

  // Assign RCA
  MmcHostInstance->CardInfo.RCA = ++mEmmcRcaCount;
  RCA = MmcHostInstance->CardInfo.RCA << RCA_SHIFT_OFFSET;
  Status = Host->SendCommand (Host, MMC_CMD3, RCA);
  if (EFI_ERROR (Status)) return Status;

  // Fetch CSD
  Status = Host->SendCommand (Host, MMC_CMD9, RCA);
  if (EFI_ERROR (Status)) return Status;
  Status = Host->ReceiveResponse (Host, MMC_RESPONSE_TYPE_R2, (UINT32 *)&MmcHostInstance->CardInfo.CSDData);
  if (EFI_ERROR (Status)) return Status;

  // Select card
  Status = Host->SendCommand (Host, MMC_CMD7, RCA);
  if (EFI_ERROR (Status)) return Status;

  // Fetch ECSD
  Status = Host->SendCommand (Host, MMC_CMD8, 0);
  if (EFI_ERROR (Status)) return Status;
  ZeroMem (EcsdBuf, sizeof (EcsdBuf));
  Status = Host->ReadBlockData (Host, 0, 512, (UINT32 *)EcsdBuf);
  if (EFI_ERROR (Status)) return Status;

  // Wait to exit data state
  do {
    Status = EmmcGetDeviceState (MmcHostInstance, &State);
    if (EFI_ERROR (Status)) return Status;
  } while (State == EMMC_DATA_STATE);

  // Set up media
  Media->BlockSize    = EMMC_CARD_SIZE;
  Media->MediaId      = MmcHostInstance->CardInfo.CIDData.Psn;
  Media->ReadOnly     = MmcHostInstance->CardInfo.CSDData.PermWriteProtect;
  Media->LastBlock    = (EcsdBuf[215] << 24) | (EcsdBuf[214] << 16) |
                        (EcsdBuf[213] << 8)  |  EcsdBuf[212];
  if (Media->LastBlock > 0) Media->LastBlock -= 1;
  Media->MediaPresent = TRUE;
  Media->IoAlign      = 4;

  MmcHostInstance->CardInfo.OCRData  = Response.Raw;

  DEBUG ((DEBUG_INFO, "MmcDxe: eMMC detected, LastBlock=0x%llx, BlockSize=%d\n",
          Media->LastBlock, Media->BlockSize));
  return EFI_SUCCESS;
}

// ── SD Card Identification ──────────────────────────────────────────

STATIC EFI_STATUS
InitializeSdMmcDevice (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  EFI_MMC_HOST_PROTOCOL *MmcHost = MmcHostInstance->MmcHost;
  EFI_STATUS Status;
  UINT32 Response[4];
  UINTN  CmdArg;
  UINTN  BlockSize, CardSize, NumBlocks;

  // Fetch CSD
  CmdArg = MmcHostInstance->CardInfo.RCA << 16;
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD9, CmdArg);
  if (EFI_ERROR (Status)) return Status;
  Status = MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_R2, Response);
  if (EFI_ERROR (Status)) return Status;

  PrintCSD (Response);

  CardSize  = MMC_CSD_GET_DEVICESIZE (Response);
  NumBlocks = (CardSize + 1) * (1U << (MMC_CSD_GET_DEVICESIZEMULT (Response) + 2));
  BlockSize = 1U << MMC_CSD_GET_READBLLEN (Response);

  if (BlockSize > 512) {
    NumBlocks = NumBlocks * (BlockSize / 512);
    BlockSize = 512;
  }

  MmcHostInstance->BlockIo.Media->LastBlock    = NumBlocks - 1;
  MmcHostInstance->BlockIo.Media->BlockSize    = BlockSize;
  MmcHostInstance->BlockIo.Media->ReadOnly     = MmcHost->IsReadOnly (MmcHost);
  MmcHostInstance->BlockIo.Media->MediaPresent = TRUE;
  MmcHostInstance->BlockIo.Media->MediaId++;

  // Select card
  CmdArg = MmcHostInstance->CardInfo.RCA << 16;
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD7, CmdArg);
  if (EFI_ERROR (Status)) return Status;

  DEBUG ((DEBUG_INFO, "MmcDxe: SD card detected, LastBlock=0x%llx, BlockSize=%d\n",
          MmcHostInstance->BlockIo.Media->LastBlock, BlockSize));
  return EFI_SUCCESS;
}

// ── Main Identification Entry ───────────────────────────────────────

STATIC EFI_STATUS
MmcIdentificationMode (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  EFI_STATUS            Status;
  UINT32                Response[4];
  UINTN                 Timeout;
  UINTN                 CmdArg;
  BOOLEAN               IsHCS = FALSE;
  EFI_MMC_HOST_PROTOCOL *MmcHost;
  OCR_RESPONSE          OcrResponse;

  MmcHost = MmcHostInstance->MmcHost;
  if (MmcHost == NULL) return EFI_INVALID_PARAMETER;

  // HW init
  if (MmcHostInstance->State == MmcHwInitializationState) {
    Status = MmcNotifyState (MmcHostInstance, MmcHwInitializationState);
    if (EFI_ERROR (Status)) return Status;
  }

  // CMD0: Reset to idle
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD0, 0);
  if (EFI_ERROR (Status)) return Status;
  MmcNotifyState (MmcHostInstance, MmcIdleState);

  // CMD1: Try eMMC first
  Timeout = MAX_RETRY_COUNT;
  OcrResponse.Raw = 0;
  do {
    Status = MmcHost->SendCommand (MmcHost, MMC_CMD1, EMMC_CMD1_CAPACITY_GREATER_THAN_2GB);
    if (EFI_ERROR (Status)) break;
    Status = MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_OCR, (UINT32 *)&OcrResponse);
    if (EFI_ERROR (Status)) return Status;
    Timeout--;
  } while (!OcrResponse.Ocr.PowerUp && (Timeout > 0));

  // eMMC
  if (!EFI_ERROR (Status) && OcrResponse.Ocr.PowerUp) {
    return EmmcIdentificationMode (MmcHostInstance, OcrResponse);
  }

  // SDIO? Not supported
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD5, 0);
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MmcDxe: SDIO not supported\n"));
    return EFI_UNSUPPORTED;
  }

  // CMD8: Check SD 2.0
  CmdArg = (0x0UL << 12 | BIT8 | 0xCEUL);
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD8, CmdArg);
  if (!EFI_ERROR (Status)) {
    IsHCS = TRUE;
    Status = MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_R7, Response);
    if (EFI_ERROR (Status) || Response[0] != CmdArg) {
      return EFI_UNSUPPORTED;
    }
  }

  // ACMD41: Initialize SD
  Timeout = MAX_RETRY_COUNT;
  while (Timeout > 0) {
    Status = MmcHost->SendCommand (MmcHost, MMC_CMD55, 0);
    if (Status == EFI_SUCCESS) {
      MmcHostInstance->CardInfo.CardType = IsHCS ? 2 : 1; // SD_CARD_2 : SD_CARD

      CmdArg = (UINTN)MmcHostInstance->CardInfo.OCRData;
      if (IsHCS) CmdArg |= BIT30;
      Status = MmcHost->SendCommand (MmcHost, MMC_ACMD41, CmdArg);
      if (!EFI_ERROR (Status)) {
        Status = MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_OCR, Response);
        if (!EFI_ERROR (Status)) {
          MmcHostInstance->CardInfo.OCRData = Response[0];
        }
      }
    }
    if (MmcHostInstance->CardInfo.OCRData & BIT31) break;
    Timeout--;
    MicroSecondDelay (1000);
  }

  if (Timeout == 0) return EFI_TIMEOUT;

  // Get RCA
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD2, 0);
  if (EFI_ERROR (Status)) return Status;
  Status = MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_R2, (UINT32 *)&MmcHostInstance->CardInfo.CIDData);
  if (EFI_ERROR (Status)) return Status;

  MmcHostInstance->CardInfo.RCA = 1;
  CmdArg = MmcHostInstance->CardInfo.RCA << 16;
  Status = MmcHost->SendCommand (MmcHost, MMC_CMD3, CmdArg);
  if (EFI_ERROR (Status)) return Status;

  return InitializeSdMmcDevice (MmcHostInstance);
}

// ── Public Entry Point ──────────────────────────────────────────────

EFI_STATUS
InitializeMmcDevice (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  EFI_STATUS Status;

  // Check card present
  if (!MmcHostInstance->MmcHost->IsCardPresent (MmcHostInstance->MmcHost)) {
    DEBUG ((DEBUG_WARN, "MmcDxe: No card present\n"));
    return EFI_NO_MEDIA;
  }

  // Init MMC host hardware
  Status = MmcNotifyState (MmcHostInstance, MmcHwInitializationState);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MmcDxe: Failed to init HW: %r\n", Status));
    return Status;
  }

  // Run card identification sequence
  Status = MmcIdentificationMode (MmcHostInstance);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MmcDxe: Card identification failed: %r\n", Status));
    return Status;
  }

  // Notify transfer state
  MmcNotifyState (MmcHostInstance, MmcTransferState);

  DEBUG ((DEBUG_INFO, "MmcDxe: Device initialized OK\n"));
  PrintCID ((UINT32 *)&MmcHostInstance->CardInfo.CIDData);
  PrintCSD ((UINT32 *)&MmcHostInstance->CardInfo.CSDData);

  return EFI_SUCCESS;
}
