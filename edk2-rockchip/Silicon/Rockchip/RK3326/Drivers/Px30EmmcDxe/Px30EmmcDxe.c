/** @file
  PX30 DesignWare eMMC host controller driver.

  Direct DWMMC register access following the RK3399 DwEmmcDxe pattern.
  Produces EFI_MMC_HOST_PROTOCOL for the EmbeddedPkg MmcDxe to consume.

  Copyright (c) 2014-2017, Linaro Limited. All rights reserved.
  Copyright (c) 2017, Rockchip Inc. All rights reserved.
  Copyright (c) 2026, RK3326 EDK2 Port

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/MmcHost.h>
#include <Soc.h>

#include "Px30Emmc.h"

#define EMMC_BLOCK_SIZE     512
#define EMMC_DESC_PAGE      1
#define EMMC_DMA_BUF_SIZE   (512 * 8)
#define EMMC_MAX_DESC_PAGES 512

typedef struct {
  UINT32  Des0;
  UINT32  Des1;
  UINT32  Des2;
  UINT32  Des3;
} IDMAC_DESCRIPTOR;

STATIC IDMAC_DESCRIPTOR  *mIdmacDesc;
STATIC UINT32             mEmmcCommand;
STATIC UINT32             mEmmcArgument;
STATIC UINT64             mBase;

// ── Helpers ────────────────────────────────────────────────────────

STATIC
VOID
DwEmmcWrite (
  IN UINT32  Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 (mBase + Offset, Value);
}

STATIC
UINT32
DwEmmcRead (
  IN UINT32  Offset
  )
{
  return MmioRead32 (mBase + Offset);
}

// ── MMC Host Protocol ──────────────────────────────────────────────

EFI_STATUS
Px30EmmcReadBlockData (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN EFI_LBA                 Lba,
  IN UINTN                   Length,
  IN UINT32                 *Buffer
  );

EFI_STATUS
Px30EmmcWriteBlockData (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN EFI_LBA                 Lba,
  IN UINTN                   Length,
  IN UINT32                 *Buffer
  );

BOOLEAN
Px30EmmcIsCardPresent (
  IN EFI_MMC_HOST_PROTOCOL  *This
  )
{
  return TRUE;  // eMMC is always present
}

BOOLEAN
Px30EmmcIsReadOnly (
  IN EFI_MMC_HOST_PROTOCOL  *This
  )
{
  return FALSE;
}

BOOLEAN
Px30EmmcIsDmaSupported (
  IN EFI_MMC_HOST_PROTOCOL  *This
  )
{
  return TRUE;
}

EFI_STATUS
Px30EmmcBuildDevicePath (
  IN  EFI_MMC_HOST_PROTOCOL      *This,
  OUT EFI_DEVICE_PATH_PROTOCOL  **DevicePath
  )
{
  static EFI_GUID  mPx30EmmcGuid = {
    0xc7d5f9a3, 0x2e4b, 0x4f6c,
    { 0x8d, 0x1e, 0x0a, 0x5b, 0x7c, 0x3f, 0x9e, 0x6d }
  };
  VENDOR_DEVICE_PATH  *Node;

  Node = (VENDOR_DEVICE_PATH *)AllocatePool (sizeof (VENDOR_DEVICE_PATH));
  if (Node == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  Node->Header.Type    = HARDWARE_DEVICE_PATH;
  Node->Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&Node->Header, sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Node->Guid, &mPx30EmmcGuid);
  *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)Node;
  return EFI_SUCCESS;
}

// ── Clock control ──────────────────────────────────────────────────

STATIC
EFI_STATUS
DwEmmcUpdateClock (
  VOID
  )
{
  UINT32  Data;

  DwEmmcWrite (DWEMMC_CMD, CMD_UPDATE_CLK);
  while (TRUE) {
    Data = DwEmmcRead (DWEMMC_CMD);
    if ((Data & CMD_START_BIT) == 0) {
      break;
    }
    Data = DwEmmcRead (DWEMMC_RINTSTS);
    if (Data & DWEMMC_INT_HLE) {
      DEBUG ((DEBUG_ERROR, "DwEmmcUpdateClock: HLE error\n"));
      return EFI_DEVICE_ERROR;
    }
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DwEmmcSetClock (
  IN UINTN  ClockFreq
  )
{
  UINT32  Divider, Data;
  BOOLEAN Found;
  EFI_STATUS  Status;

  //
  // DWMMC clock:  CardClk = SourceClk / (2 * Divider)
  // PcdPx30EmmcClockFrequency is the source clock in Hz (typically 52MHz).
  //
  Found = FALSE;
  for (Divider = 1; Divider < 256; Divider++) {
    if ((FixedPcdGet32 (PcdPx30EmmcClockFrequency) / (2 * Divider)) <= ClockFreq) {
      Found = TRUE;
      break;
    }
  }

  if (!Found) {
    return EFI_NOT_FOUND;
  }

  //
  // Wait until MMC is idle.
  //
  do {
    Data = DwEmmcRead (DWEMMC_STATUS);
  } while (Data & DWEMMC_STS_DATA_BUSY);

  //
  // Disable clock, update, set divider, update, enable clock, update.
  //
  DwEmmcWrite (DWEMMC_CLKENA, 0);
  Status = DwEmmcUpdateClock ();
  if (EFI_ERROR (Status)) return Status;

  DwEmmcWrite (DWEMMC_CLKDIV, Divider);
  Status = DwEmmcUpdateClock ();
  if (EFI_ERROR (Status)) return Status;

  DwEmmcWrite (DWEMMC_CLKENA, 1);
  DwEmmcWrite (DWEMMC_CLKSRC, 0);
  Status = DwEmmcUpdateClock ();
  if (EFI_ERROR (Status)) return Status;

  DEBUG ((DEBUG_INFO, "Px30Emmc: clock=%d KHz, divider=%d\n", ClockFreq / 1000, Divider));
  return EFI_SUCCESS;
}

// ── Hardware Initialization (RK3399 MmcHwInitializationState pattern) ─

STATIC
EFI_STATUS
DwEmmcHwInit (
  VOID
  )
{
  UINT32  Data;
  EFI_STATUS  Status;

  //
  // 1. Power on.
  //
  DwEmmcWrite (DWEMMC_PWREN, 1);

  //
  // 2. Reset controller (all three: CTRL, FIFO, DMA).
  //
  DwEmmcWrite (DWEMMC_CTRL, DWEMMC_CTRL_RESET_ALL);
  do {
    Data = DwEmmcRead (DWEMMC_CTRL);
  } while (Data & DWEMMC_CTRL_RESET_ALL);
  // CTRL left at 0x00000000 at this point.

  //
  // 3. Initial clock ≤ 400 KHz.
  //
  Status = DwEmmcSetClock (400000);
  if (EFI_ERROR (Status)) return Status;
  MicroSecondDelay (100);

  //
  // 4. Clear interrupts, max timeout, disable IDMAC interrupts.
  //
  DwEmmcWrite (DWEMMC_RINTSTS,  ~0U);
  DwEmmcWrite (DWEMMC_INTMASK,   0);
  DwEmmcWrite (DWEMMC_TMOUT,    ~0U);
  DwEmmcWrite (DWEMMC_IDINTEN,   0);

  //
  // 5. Reset IDMAC via BMOD.
  //
  DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_SWRESET);
  do {
    Data = DwEmmcRead (DWEMMC_BMOD);
  } while (Data & DWEMMC_IDMAC_SWRESET);

  //
  // 6. Default block size.
  //
  DwEmmcWrite (DWEMMC_BLKSIZ, EMMC_BLOCK_SIZE);

  DEBUG ((DEBUG_INFO, "Px30Emmc: HW init done\n"));
  return EFI_SUCCESS;
}

// ── NotifyState ─────────────────────────────────────────────────────

EFI_STATUS
Px30EmmcNotifyState (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN MMC_STATE               State
  )
{
  switch (State) {
  case MmcHwInitializationState:
    return DwEmmcHwInit ();
  case MmcIdleState:
  case MmcReadyState:
  case MmcIdentificationState:
  case MmcStandByState:
  case MmcTransferState:
  case MmcSendingDataState:
  case MmcReceiveDataState:
  case MmcProgrammingState:
  case MmcDisconnectState:
    break;
  default:
    return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

// ── Send Command ────────────────────────────────────────────────────

STATIC
BOOLEAN
IsPendingReadCommand (
  IN UINT32  MmcCmd
  )
{
  return (MmcCmd & (BIT_CMD_DATA_EXPECTED | BIT6)) ==
                   (BIT_CMD_DATA_EXPECTED | BIT6);  // READ + DATA
}

STATIC
BOOLEAN
IsPendingWriteCommand (
  IN UINT32  MmcCmd
  )
{
  return (MmcCmd & (BIT_CMD_DATA_EXPECTED | BIT_CMD_WRITE)) ==
                   (BIT_CMD_DATA_EXPECTED | BIT_CMD_WRITE);
}

STATIC
EFI_STATUS
SendCommand (
  IN UINT32  MmcCmd,
  IN UINT32  Argument
  )
{
  UINT32  Data, ErrMask;

  //
  // Small delay before command (RK3399 uses 15ms).
  //
  MicroSecondDelay (15000);

  //
  // Wait until MMC is idle.
  //
  do {
    Data = DwEmmcRead (DWEMMC_STATUS);
  } while (Data & DWEMMC_STS_DATA_BUSY);

  DwEmmcWrite (DWEMMC_RINTSTS, ~0U);
  DwEmmcWrite (DWEMMC_CMDARG, Argument);
  DwEmmcWrite (DWEMMC_CMD, MmcCmd);

  ErrMask = DWEMMC_INT_EBE | DWEMMC_INT_HLE | DWEMMC_INT_RTO |
            DWEMMC_INT_RCRC | DWEMMC_INT_RE;
  ErrMask |= DWEMMC_INT_DCRC | DWEMMC_INT_DRT | DWEMMC_INT_SBE;

  do {
    MicroSecondDelay (500);
    Data = DwEmmcRead (DWEMMC_RINTSTS);

    if (Data & ErrMask) {
      DEBUG ((DEBUG_ERROR, "Px30Emmc: cmd error RINTSTS=0x%x cmd=%d arg=0x%x\n",
              Data, MmcCmd & 0x3F, Argument));
      return EFI_DEVICE_ERROR;
    }
    if (Data & DWEMMC_INT_DTO) {
      break;
    }
  } while ((Data & DWEMMC_INT_CMD_DONE) == 0);

  return EFI_SUCCESS;
}

EFI_STATUS
Px30EmmcSendCommand (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN MMC_CMD                 MmcCmd,
  IN UINT32                  Argument
  )
{
  UINT32  Cmd = 0;

  switch (MMC_INDX (MmcCmd)) {
  case MMC_INDX(0):
    Cmd = BIT_CMD_SEND_INIT;
    break;
  case MMC_INDX(1):
    Cmd = BIT_CMD_RESPONSE_EXPECT;
    break;
  case MMC_INDX(2):
    //
    // CMD2 (ALL_SEND_CID) returns a 136-bit R2 response with its own
    // internal CRC scheme.  Do NOT set CHECK_RESPONSE_CRC — the DWMMC
    // controller would incorrectly flag a CRC error.
    //
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_LONG_RESPONSE |
          BIT_CMD_SEND_INIT;
    break;
  case MMC_INDX(3):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_SEND_INIT;
    break;
  case MMC_INDX(6):
    if ((Argument >> 31) & 1) {
      Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
            BIT_CMD_DATA_EXPECTED | BIT6 |   // BIT6 = READ
            BIT_CMD_WAIT_PRVDATA_COMPLETE;
    } else {
      Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC;
    }
    break;
  case MMC_INDX(7):
    if (Argument) {
      Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC;
    }
    break;
  case MMC_INDX(8):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(9):
    //
    // CMD9 (SEND_CSD) is R2 — no CRC check (same reason as CMD2).
    //
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_LONG_RESPONSE;
    break;
  case MMC_INDX(12):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_STOP_ABORT_CMD;
    break;
  case MMC_INDX(13):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(16):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC;
    break;
  case MMC_INDX(17):
  case MMC_INDX(18):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_DATA_EXPECTED | BIT6 |  // READ
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(24):
  case MMC_INDX(25):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_DATA_EXPECTED | BIT_CMD_WRITE |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(41):
    Cmd = BIT_CMD_RESPONSE_EXPECT;
    break;
  case MMC_INDX(51):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_DATA_EXPECTED | BIT6 |  // READ
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  default:
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC;
    break;
  }

  Cmd |= MMC_GET_INDX (MmcCmd) | BIT_CMD_USE_HOLD_REG | BIT_CMD_START;

  if (IsPendingReadCommand (Cmd) || IsPendingWriteCommand (Cmd)) {
    mEmmcCommand = Cmd;
    mEmmcArgument = Argument;
  } else {
    return SendCommand (Cmd, Argument);
  }
  return EFI_SUCCESS;
}

// ── Receive Response ────────────────────────────────────────────────

EFI_STATUS
Px30EmmcReceiveResponse (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN MMC_RESPONSE_TYPE       Type,
  IN UINT32                 *Buffer
  )
{
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Type == MMC_RESPONSE_TYPE_R1)  || (Type == MMC_RESPONSE_TYPE_R1b) ||
      (Type == MMC_RESPONSE_TYPE_R3)  || (Type == MMC_RESPONSE_TYPE_R6)  ||
      (Type == MMC_RESPONSE_TYPE_R7)) {
    Buffer[0] = DwEmmcRead (DWEMMC_RESP0);
  } else if (Type == MMC_RESPONSE_TYPE_R2) {
    Buffer[0] = DwEmmcRead (DWEMMC_RESP0);
    Buffer[1] = DwEmmcRead (DWEMMC_RESP1);
    Buffer[2] = DwEmmcRead (DWEMMC_RESP2);
    Buffer[3] = DwEmmcRead (DWEMMC_RESP3);
  }
  return EFI_SUCCESS;
}

// ── DMA helpers ─────────────────────────────────────────────────────

STATIC
VOID
StartDma (
  IN UINTN  Length
  )
{
  UINT32  Data;

  Data = DwEmmcRead (DWEMMC_CTRL);
  Data |= DWEMMC_CTRL_INT_EN | DWEMMC_CTRL_DMA_EN | DWEMMC_CTRL_IDMAC_EN;
  DwEmmcWrite (DWEMMC_CTRL, Data);

  Data = DwEmmcRead (DWEMMC_BMOD);
  Data |= DWEMMC_IDMAC_ENABLE | DWEMMC_IDMAC_FB;
  DwEmmcWrite (DWEMMC_BMOD, Data);

  DwEmmcWrite (DWEMMC_BLKSIZ, EMMC_BLOCK_SIZE);
  DwEmmcWrite (DWEMMC_BYTCNT, Length);
}

STATIC
EFI_STATUS
PrepareDmaData (
  IN IDMAC_DESCRIPTOR  *IdmacDesc,
  IN UINTN              Length,
  IN UINT32            *Buffer
  )
{
  UINTN  Cnt, Blks, Idx, LastIdx;

  Cnt  = (Length + EMMC_DMA_BUF_SIZE - 1) / EMMC_DMA_BUF_SIZE;
  Blks = (Length + EMMC_BLOCK_SIZE - 1) / EMMC_BLOCK_SIZE;
  Length = EMMC_BLOCK_SIZE * Blks;

  for (Idx = 0; Idx < Cnt; Idx++) {
    (IdmacDesc + Idx)->Des0 = DWEMMC_IDMAC_DES0_OWN | DWEMMC_IDMAC_DES0_CH |
                               DWEMMC_IDMAC_DES0_DIC;
    (IdmacDesc + Idx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (EMMC_DMA_BUF_SIZE);
    (IdmacDesc + Idx)->Des2 = (UINT32)(UINTN)(Buffer + EMMC_DMA_BUF_SIZE / 4 * Idx);
    (IdmacDesc + Idx)->Des3 = (UINT32)(UINTN)(IdmacDesc +
                                sizeof (IDMAC_DESCRIPTOR) * (Idx + 1));
  }

  IdmacDesc->Des0 |= DWEMMC_IDMAC_DES0_FS;

  LastIdx = Cnt - 1;
  (IdmacDesc + LastIdx)->Des0 |= DWEMMC_IDMAC_DES0_LD;
  (IdmacDesc + LastIdx)->Des0 &= ~(DWEMMC_IDMAC_DES0_DIC | DWEMMC_IDMAC_DES0_CH);
  (IdmacDesc + LastIdx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (Length -
                                  LastIdx * EMMC_DMA_BUF_SIZE);
  (IdmacDesc + LastIdx)->Des3 = 0;

  DwEmmcWrite (DWEMMC_DBADDR, (UINT32)(UINTN)IdmacDesc);
  return EFI_SUCCESS;
}

// ── Read / Write Block Data ─────────────────────────────────────────

#define DWEMMC_MSHCI_FIFO  ((UINT32)mBase + 0x200)

EFI_STATUS
Px30EmmcReadBlockData (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN EFI_LBA                 Lba,
  IN UINTN                   Length,
  IN UINT32                 *Buffer
  )
{
  EFI_STATUS  Status;
  UINT32      Data;

  if (mEmmcCommand & BIT_CMD_WAIT_PRVDATA_COMPLETE) {
    do { Data = DwEmmcRead (DWEMMC_STATUS); }
    while (Data & DWEMMC_STS_DATA_BUSY);
  }

  PrepareDmaData (mIdmacDesc, Length, Buffer);
  StartDma (Length);

  Status = SendCommand (mEmmcCommand, mEmmcArgument);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Px30Emmc: read cmd failed cmd=%d\n",
            mEmmcCommand & 0x3F));
    return Status;
  }

  //
  // DMA complete → data is already in Buffer.
  //
  return EFI_SUCCESS;
}

EFI_STATUS
Px30EmmcWriteBlockData (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN EFI_LBA                 Lba,
  IN UINTN                   Length,
  IN UINT32                 *Buffer
  )
{
  EFI_STATUS  Status;
  UINT32      Data;

  if (mEmmcCommand & BIT_CMD_WAIT_PRVDATA_COMPLETE) {
    do { Data = DwEmmcRead (DWEMMC_STATUS); }
    while (Data & DWEMMC_STS_DATA_BUSY);
  }

  PrepareDmaData (mIdmacDesc, Length, Buffer);
  StartDma (Length);

  Status = SendCommand (mEmmcCommand, mEmmcArgument);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Px30Emmc: write cmd failed cmd=%d\n",
            mEmmcCommand & 0x3F));
    return Status;
  }

  return EFI_SUCCESS;
}

// ── Set I/O Settings ────────────────────────────────────────────────

EFI_STATUS
Px30EmmcSetIos (
  IN EFI_MMC_HOST_PROTOCOL  *This,
  IN UINT32                  BusClockFreq,
  IN UINT32                  BusWidth,
  IN UINT32                  TimingMode
  )
{
  EFI_STATUS  Status = EFI_SUCCESS;
  UINT32      Data;

  //
  // Timing mode (UHSREG).
  //
  if (TimingMode != EMMCBACKWARD) {
    Data = DwEmmcRead (DWEMMC_UHSREG);
    switch (TimingMode) {
    case EMMCHS52DDR1V2:
    case EMMCHS52DDR1V8:
      Data |= BIT16;
      break;
    case EMMCHS52:
    case EMMCHS26:
      Data &= ~BIT16;
      break;
    default:
      return EFI_UNSUPPORTED;
    }
    DwEmmcWrite (DWEMMC_UHSREG, Data);
  }

  //
  // Bus width.
  //
  switch (BusWidth) {
  case 1:
    DwEmmcWrite (DWEMMC_CTYPE, 0);
    break;
  case 4:
    DwEmmcWrite (DWEMMC_CTYPE, 1);
    break;
  case 8:
    DwEmmcWrite (DWEMMC_CTYPE, BIT16);
    break;
  default:
    return EFI_UNSUPPORTED;
  }

  //
  // Clock frequency.
  //
  if (BusClockFreq) {
    Status = DwEmmcSetClock (BusClockFreq);
  }

  return Status;
}

BOOLEAN
Px30EmmcIsMultiBlock (
  IN EFI_MMC_HOST_PROTOCOL  *This
  )
{
  return TRUE;
}

// ── Protocol instance ───────────────────────────────────────────────

STATIC EFI_MMC_HOST_PROTOCOL  mMmcHost = {
  MMC_HOST_PROTOCOL_REVISION,
  Px30EmmcIsCardPresent,
  Px30EmmcIsReadOnly,
  Px30EmmcBuildDevicePath,
  Px30EmmcNotifyState,
  Px30EmmcSendCommand,
  Px30EmmcReceiveResponse,
  Px30EmmcReadBlockData,
  Px30EmmcWriteBlockData,
  Px30EmmcSetIos,
  Px30EmmcIsMultiBlock
};

// ── Driver Entry Point ──────────────────────────────────────────────

EFI_STATUS
EFIAPI
Px30EmmcDxeInitialize (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  mBase = FixedPcdGet32 (PcdPx30EmmcBaseAddress);

  DEBUG ((DEBUG_INFO, "Px30EmmcDxe: base=0x%llx\n", mBase));

  mIdmacDesc = (IDMAC_DESCRIPTOR *)AllocatePages (EMMC_MAX_DESC_PAGES);
  if (mIdmacDesc == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEmbeddedMmcHostProtocolGuid, &mMmcHost,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
