/** @file
  MMC Block I/O protocol implementation

  Copyright (c) 2011-2015, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseMemoryLib.h>
#include "Mmc.h"

EFI_STATUS
MmcNotifyState (
  IN MMC_HOST_INSTANCE *MmcHostInstance,
  IN MMC_STATE         State
  )
{
  MmcHostInstance->State = State;
  return MmcHostInstance->MmcHost->NotifyState (MmcHostInstance->MmcHost, State);
}

EFI_STATUS
EFIAPI
MmcGetCardStatus (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  EFI_STATUS             Status;
  UINT32                 Response[4];
  UINTN                  CmdArg;
  EFI_MMC_HOST_PROTOCOL *MmcHost;

  MmcHost = MmcHostInstance->MmcHost;
  if (MmcHost == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (MmcHostInstance->State != MmcHwInitializationState) {
    CmdArg = MmcHostInstance->CardInfo.RCA << 16;
    Status = MmcHost->SendCommand (MmcHost, MMC_CMD13, CmdArg);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MmcGetCardStatus(CMD13): %r\n", Status));
      return Status;
    }
    MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_R1, Response);
    PrintResponseR1 (Response[0]);
  }
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MmcReset (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN BOOLEAN               ExtendedVerification
  )
{
  MMC_HOST_INSTANCE *MmcHostInstance;

  MmcHostInstance = MMC_HOST_INSTANCE_FROM_BLOCK_IO_THIS (This);

  if (MmcHostInstance->MmcHost == NULL) {
    return EFI_SUCCESS;
  }

  if (!MmcHostInstance->MmcHost->IsCardPresent (MmcHostInstance->MmcHost)) {
    MmcHostInstance->BlockIo.Media->MediaPresent = FALSE;
    MmcHostInstance->BlockIo.Media->LastBlock    = 0;
    MmcHostInstance->BlockIo.Media->BlockSize    = 512;
    MmcHostInstance->BlockIo.Media->ReadOnly     = FALSE;
    MmcHostInstance->State = MmcHwInitializationState;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MmcDetectCard (
  IN EFI_MMC_HOST_PROTOCOL *MmcHost
  )
{
  if (!MmcHost->IsCardPresent (MmcHost)) {
    return EFI_NO_MEDIA;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
MmcStopTransmission (
  IN EFI_MMC_HOST_PROTOCOL *MmcHost
  )
{
  EFI_STATUS Status;
  UINT32     Response[4];

  Status = MmcHost->SendCommand (MmcHost, MMC_CMD12, 0);
  if (!EFI_ERROR (Status)) {
    MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_R1b, Response);
  }
  return Status;
}

#define MMCI0_BLOCKLEN 512
#define MMCI0_TIMEOUT  10000

STATIC
EFI_STATUS
MmcTransferBlock (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINTN                  Cmd,
  IN UINTN                  Transfer,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                 *Buffer
  )
{
  EFI_STATUS             Status;
  UINTN                  CmdArg;
  INTN                   Timeout;
  UINT32                 Response[4];
  MMC_HOST_INSTANCE     *MmcHostInstance;
  EFI_MMC_HOST_PROTOCOL *MmcHost;

  MmcHostInstance = MMC_HOST_INSTANCE_FROM_BLOCK_IO_THIS (This);
  MmcHost         = MmcHostInstance->MmcHost;

  if (MmcHostInstance->BlockIo.Media->MediaId != MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if (MmcHost == NULL) {
    return EFI_DEVICE_ERROR;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((MmcHostInstance->CardInfo.OCRData & MMC_OCR_ACCESS_MASK) ==
      MMC_OCR_ACCESS_SECTOR) {
    CmdArg = (UINTN)Lba;
  } else {
    CmdArg = (UINTN)(Lba * This->Media->BlockSize);
  }

  Status = MmcHost->SendCommand (MmcHost, Cmd, CmdArg);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a(CMD%d): %r\n", __func__, Cmd, Status));
    return Status;
  }

  if (Transfer == MMC_IOBLOCKS_READ) {
    Status = MmcHost->ReadBlockData (MmcHost, Lba, BufferSize, Buffer);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a(): ReadBlockData error: %r\n", __func__, Status));
      MmcStopTransmission (MmcHost);
      return Status;
    }
    MmcNotifyState (MmcHostInstance, MmcProgrammingState);
  } else {
    Status = MmcHost->WriteBlockData (MmcHost, Lba, BufferSize, Buffer);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a(): WriteBlockData error: %r\n", __func__, Status));
      MmcStopTransmission (MmcHost);
      return Status;
    }
  }

  // Wait for programming to complete
  Timeout = MMCI0_TIMEOUT;
  CmdArg  = MmcHostInstance->CardInfo.RCA << 16;
  Response[0] = 0;
  while (!(Response[0] & MMC_R0_READY_FOR_DATA)
         && (MMC_R0_CURRENTSTATE (Response) != MMC_R0_STATE_TRAN)
         && Timeout--) {
    Status = MmcHost->SendCommand (MmcHost, MMC_CMD13, CmdArg);
    if (!EFI_ERROR (Status)) {
      MmcHost->ReceiveResponse (MmcHost, MMC_RESPONSE_TYPE_R1, Response);
      if (Response[0] & MMC_R0_READY_FOR_DATA) {
        break;
      }
    }
  }

  if (Timeout == 0) {
    DEBUG ((DEBUG_ERROR, "%a(): CMD13 timeout\n", __func__));
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MmcReadBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                 *Buffer
  )
{
  return MmcTransferBlock (This, MMC_CMD17, MMC_IOBLOCKS_READ,
                           MediaId, Lba, BufferSize, Buffer);
}

EFI_STATUS
EFIAPI
MmcWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                  *Buffer
  )
{
  return MmcTransferBlock (This, MMC_CMD24, MMC_IOBLOCKS_WRITE,
                           MediaId, Lba, BufferSize, Buffer);
}

// Disk IO Protocol — simple passthrough to Block IO
EFI_STATUS
EFIAPI
MmcReadDisk (
  IN EFI_DISK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN UINT64                 Offset,
  IN UINTN                  BufferSize,
  OUT VOID                 *Buffer
  )
{
  MMC_HOST_INSTANCE *MmcHostInstance;
  EFI_LBA            Lba;

  MmcHostInstance = MMC_HOST_INSTANCE_FROM_DISK_IO_THIS (This);
  Lba = (EFI_LBA)(Offset / MmcHostInstance->BlockIo.Media->BlockSize);

  return MmcReadBlocks (
           &MmcHostInstance->BlockIo,
           MediaId,
           Lba,
           BufferSize,
           Buffer
           );
}

EFI_STATUS
EFIAPI
MmcWriteDisk (
  IN EFI_DISK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN UINT64                 Offset,
  IN UINTN                  BufferSize,
  IN VOID                  *Buffer
  )
{
  MMC_HOST_INSTANCE *MmcHostInstance;
  EFI_LBA            Lba;

  MmcHostInstance = MMC_HOST_INSTANCE_FROM_DISK_IO_THIS (This);
  Lba = (EFI_LBA)(Offset / MmcHostInstance->BlockIo.Media->BlockSize);

  return MmcWriteBlocks (
           &MmcHostInstance->BlockIo,
           MediaId,
           Lba,
           BufferSize,
           Buffer
           );
}
