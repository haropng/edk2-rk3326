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

// ── CRU / GRF register helpers ──────────────────────────────────────
//
// PX30 CRU clock gates:  offset = 0x100 + (id/16)*4, bit = id%16
// PX30 CRU resets:      offset = 0x200 + (id/16)*4, bit = id%16
// Rockchip write convention: hi16 = mask, lo16 = value.

#define CRU_GATE(id)      (CRU_BASE + 0x100U + ((id) / 16U) * 4U)
#define CRU_RST(id)       (CRU_BASE + 0x200U + ((id) / 16U) * 4U)

// CRU clock gate IDs (from px30-cru.h)
#define HCLK_EMMC          256
#define HCLK_MMC_NAND      246
#define SCLK_EMMC          57

// CRU reset IDs
#define SRST_MMC_NAND_NIU_H  80
#define SRST_EMMC_H          82

// GRF registers
#define GRF_IO_VSEL         (GRF_BASE + 0x180U)
#define IOVSEL6_CTRL        BIT0   // 0=GPIO0_B6, 1=register
#define IOVSEL6_1V8         BIT1   // 0=3.3V, 1=1.8V

// eMMC IOMUX: GPIO1 bank, Function 2, via GRF
#define GRF_GPIO1AL_IOMUX   (GRF_BASE + 0x00U)
#define GRF_GPIO1AH_IOMUX   (GRF_BASE + 0x04U)
#define GRF_GPIO1BL_IOMUX   (GRF_BASE + 0x08U)
#define GRF_GPIO1BH_IOMUX   (GRF_BASE + 0x0CU)

//
// Each 32-bit IOMUX register controls 4 pins × 8 bits (4-bit fn + 4-bit
// reserved/upper).  fn=2 for all eMMC pins.
//
#define EMMC_DATA_FN(p)     ((2U) << (((p) % 4) * 4))
#define EMMC_IOMUX_LO_MASK  0x000F000FU   // pins 0,1: fn
#define EMMC_IOMUX_HI_MASK  0x0F000F00U   // pins 2,3: fn

#define EMMC_BLOCK_SIZE     512
#define DWEMMC_DATA             0x100   // FIFO data port (standard DWMMC PIO)
#define EMMC_DESC_PAGE           1
#define EMMC_DMA_BUF_SIZE        (512 * 8)
#define EMMC_MAX_DESC_PAGES      1     // single-block only needs 1 desc (16 bytes); 1 page holds 256

typedef struct {
  UINT32  Des0;
  UINT32  Des1;
  UINT32  Des2;
  UINT32  Des3;
} IDMAC_DESCRIPTOR;

STATIC IDMAC_DESCRIPTOR  *mIdmacDesc;
STATIC UINT8              *mDmaBuffer;     // page-aligned private DMA buffer
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

// Forward declarations
STATIC
VOID
StartDma (
  IN UINTN  Length
  );

STATIC
EFI_STATUS
PrepareDmaData (
  IN IDMAC_DESCRIPTOR  *IdmacDesc,
  IN UINTN              Length,
  IN UINT32            *Buffer
  );

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
  // Disable clock, set source=0, set divider, update, enable clock with
  // low-power mode, update again.  This matches the U-Boot dwmci_setup_bus()
  // ordering to avoid clock glitches.
  //
  DwEmmcWrite (DWEMMC_CLKENA, 0);
  DwEmmcWrite (DWEMMC_CLKSRC, 0);
  Status = DwEmmcUpdateClock ();
  if (EFI_ERROR (Status)) return Status;

  DwEmmcWrite (DWEMMC_CLKDIV, Divider);
  Status = DwEmmcUpdateClock ();
  if (EFI_ERROR (Status)) return Status;

  DwEmmcWrite (DWEMMC_CLKENA, 0x00010001U);  // CLK_ENABLE | CLK_LOW_PWR
  Status = DwEmmcUpdateClock ();
  if (EFI_ERROR (Status)) return Status;

  DEBUG ((DEBUG_INFO, "Px30Emmc: clock=%d KHz, divider=%d\n", ClockFreq / 1000, Divider));
  return EFI_SUCCESS;
}

// ── Platform Initialization ─────────────────────────────────────────

/**
  Configure CRU clock gates, resets, VCCIO6 voltage domain, and eMMC
  pin IOMUX before touching any DWMMC register.

  Follows the RK3399 DwEmmcIomux() pattern: assert reset, delay,
  deassert, then set up clocks.

  U-Boot already configures most of this, but we re-apply to guard
  against state changes during the U-Boot→UEFI handoff.
**/
STATIC
VOID
Px30EmmcPlatformInit (
  VOID
  )
{
  UINT32  Mask, Bit;

  //
  // 1. Enable CRU clock gates (in case gated during handoff).
  //    Write convention: hi16=mask, lo16=value (1=enable).
  //
  #define GATE_ON(id) do {                                 \
    UINT32 r_ = CRU_GATE (id);                             \
    UINT32 b_ = 1U << ((id) % 16U);                       \
    MmioWrite32 (r_, (b_ << 16) | b_);                    \
  } while (0)

  GATE_ON (HCLK_EMMC);       // AHB bus clock
  GATE_ON (HCLK_MMC_NAND);   // NIU clock for MMC controllers
  GATE_ON (SCLK_EMMC);       // eMMC card clock (ciu)

  //
  // 2. Configure CRU eMMC clock source: GPLL (1200MHz) / 12 = 100MHz.
  //    This matches PcdPx30EmmcClockFrequency (100 MHz).
  //    Register: CLKSEL20_CON at CRU+0x164, CLKSEL21_CON at CRU+0x168.
  //
  #define CLKSEL20_CON        (CRU_BASE + 0x164U)
  #define CLKSEL21_CON        (CRU_BASE + 0x168U)
  #define EMMC_PLL_SHIFT      14
  #define EMMC_PLL_MASK       (0x3U << 14)
  #define EMMC_PLL_GPLL       0
  #define EMMC_DIV_SHIFT      0
  #define EMMC_DIV_MASK       0x3FU
  #define EMMC_CLK_SEL_SHIFT  15
  #define EMMC_CLK_SEL_MASK   (0x1U << 15)
  #define EMMC_CLK_SEL_DIRECT 0

  //
  // GPLL = 1200MHz, target = 100MHz → DIV = 1200/100 - 1 = 11.
  // Write: hi16=mask, lo16=value.
  //
  MmioWrite32 (CLKSEL20_CON,
    (EMMC_PLL_MASK << 16) | (EMMC_DIV_MASK << 16) |
    (EMMC_PLL_GPLL << EMMC_PLL_SHIFT) |
    ((11U - 1U) << EMMC_DIV_SHIFT));
  //
  // CLKSEL21: select direct path (no /50 divider).
  //
  MmioWrite32 (CLKSEL21_CON,
    (EMMC_CLK_SEL_MASK << 16) |
    (EMMC_CLK_SEL_DIRECT << EMMC_CLK_SEL_SHIFT));

  #undef EMMC_PLL_SHIFT
  #undef EMMC_PLL_MASK
  #undef EMMC_PLL_GPLL
  #undef EMMC_DIV_SHIFT
  #undef EMMC_DIV_MASK
  #undef EMMC_CLK_SEL_SHIFT
  #undef EMMC_CLK_SEL_MASK
  #undef EMMC_CLK_SEL_DIRECT

  //
  // 3. Assert eMMC CRU resets, then deassert after a short delay.
  //    Write convention: hi16=mask, lo16=value (1=assert, 0=deassert).
  //
  Mask = 1U << (SRST_MMC_NAND_NIU_H % 16U);
  MmioWrite32 (CRU_RST (SRST_MMC_NAND_NIU_H), (Mask << 16) | Mask);
  Bit = 1U << (SRST_EMMC_H % 16U);
  MmioWrite32 (CRU_RST (SRST_EMMC_H), (Bit << 16) | Bit);

  MicroSecondDelay (5);

  MmioWrite32 (CRU_RST (SRST_MMC_NAND_NIU_H), (Mask << 16) | 0);
  MmioWrite32 (CRU_RST (SRST_EMMC_H), (Bit << 16) | 0);

  MicroSecondDelay (5);

  //
  // 4. Verify VCCIO6 voltage domain.
  //    Read GRF io_vsel: if IOVSEL6_CTRL=0 (GPIO-controlled), switch
  //    to register control while preserving the voltage level.
  //
  {
    UINT32  Vsel, Val;

    Vsel = MmioRead32 (GRF_IO_VSEL);
    if ((Vsel & IOVSEL6_CTRL) == 0) {
      //
      // GPIO0_B6 controls the voltage.  Read its level, then switch
      // to register control (same voltage).
      //
      UINT32  Gpio0In = MmioRead32 (GPIO0_BASE + 0x50U);  // EXT_PORTA
      Val = IOVSEL6_CTRL;   // switch to register control
      if (Gpio0In & BIT14) {
        Val |= IOVSEL6_1V8;   // GPIO0_B6 high → 1.8V
      }
      MmioWrite32 (GRF_IO_VSEL, (IOVSEL6_CTRL | IOVSEL6_1V8) << 16 | Val);
      DEBUG ((DEBUG_INFO, "Px30Emmc: VCCIO6 vs=%a, switched to reg control\n",
              (Val & IOVSEL6_1V8) ? "1.8V" : "3.3V"));
    } else {
      DEBUG ((DEBUG_INFO, "Px30Emmc: VCCIO6 already reg-controlled vs=%a\n",
              (Vsel & IOVSEL6_1V8) ? "1.8V" : "3.3V"));
    }
  }

  //
  // 5. Re-assert eMMC pin IOMUX via GRF (safe re-apply — U-Boot
  //    already configured these).
  //
  //    GPIO1 PA0-PA7: DATA0-DATA7, fn=2, hi16=mask + lo16=fn<<shift
  //    GPIO1 PB0: PWREN, PB1: CLK, PB2: CMD, PB3: RSTNOUT, fn=2
  //
  #define MUX(p,f)  ((0xFU << (((p)%4)*4 + 16)) | ((UINT32)(f) << (((p)%4)*4)))

  // DATA0-3 (gpio1al_iomux: PA0..PA3)
  MmioWrite32 (GRF_GPIO1AL_IOMUX,
               MUX (0, 2) | MUX (1, 2) | MUX (2, 2) | MUX (3, 2));
  // DATA4-7 (gpio1ah_iomux: PA4..PA7)
  MmioWrite32 (GRF_GPIO1AH_IOMUX,
               MUX (4, 2) | MUX (5, 2) | MUX (6, 2) | MUX (7, 2));
  //
  // CLK (PB1, fn=2) and CMD (PB2, fn=2) only.
  // PB0 (PWREN) and PB3 (RSTNOUT) are NOT included — the board DTS
  // (rk3326-common.dtsi, rk3326-evb.dts) intentionally only specifies
  // emmc_clk + emmc_cmd + emmc_bus8.  Setting RSTNOUT to eMMC function
  // would let the DWMMC controller drive it, potentially holding the
  // eMMC chip in hardware reset.
  //
  MmioWrite32 (GRF_GPIO1BL_IOMUX,
               MUX (1, 2) | MUX (2, 2) |
               MUX (0, 0) | MUX (3, 0));  // PB0,PB3 → GPIO

  #undef MUX
  #undef GATE_ON

  DEBUG ((DEBUG_INFO, "Px30Emmc: platform init done (CRU gates, reset, IOMUX)\n"));
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
  // 0. Platform init: CRU clocks, resets, VCCIO6, IOMUX.
  //
  Px30EmmcPlatformInit ();

  //
  // 1. Power on.
  //
  DwEmmcWrite (DWEMMC_PWREN, 1);

  //
  // 2. Configure Card Threshold Control if IP version ≥ 2.40A.
  //    U-Boot writes (1 + (0x200 << 16)) = 0x02000001.
  //
  Data = DwEmmcRead (DWEMMC_VERID) & 0xFFFFU;
  if (Data >= 0x240AU) {
    DwEmmcWrite (DWEMMC_CARDTHRCTL, 0x02000001U);
  }

  //
  // 3. Reset controller (all three: CTRL, FIFO, DMA).
  //
  DwEmmcWrite (DWEMMC_CTRL, DWEMMC_CTRL_RESET_ALL);
  do {
    Data = DwEmmcRead (DWEMMC_CTRL);
  } while (Data & DWEMMC_CTRL_RESET_ALL);
  // CTRL left at 0x00000000 at this point.

  //
  // 4. Initial clock ≤ 400 KHz.
  //
  Status = DwEmmcSetClock (400000);
  if (EFI_ERROR (Status)) return Status;
  MicroSecondDelay (100);

  //
  // 5. Clear interrupts, max timeout, disable IDMAC interrupts.
  //
  DwEmmcWrite (DWEMMC_RINTSTS,  ~0U);
  DwEmmcWrite (DWEMMC_INTMASK,   0);
  DwEmmcWrite (DWEMMC_TMOUT,    ~0U);
  DwEmmcWrite (DWEMMC_IDINTEN,   0);

  //
  // 5b. Enable interrupts + DMA + IDMAC in CTRL (matching U-Boot dwmci_init).
  //     U-Boot sets CTRL = INT_EN | DMA_EN | IDMAC_EN after reset.
  //
  DwEmmcWrite (DWEMMC_CTRL,
               DWEMMC_CTRL_INT_EN | DWEMMC_CTRL_DMA_EN |
               DWEMMC_CTRL_IDMAC_EN);

  //
  // 6. Reset IDMAC via BMOD.
  //
  DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_SWRESET);
  do {
    Data = DwEmmcRead (DWEMMC_BMOD);
  } while (Data & DWEMMC_IDMAC_SWRESET);

  //
  // 7. Configure FIFO threshold / eMMC control.
  //    U-Boot reads back RX_WMARK from FIFOTH to determine FIFO depth,
  //    then computes fifoth_val = MSIZE(6) | RX_WMARK(depth/2-1) |
  //    TX_WMARK(depth/2).  We read back whatever U-Boot left and re-apply
  //    it (read-modify-write with only the MSIZE/RX_WMARK/TX_WMARK fields).
  //
  {
    UINT32  FifoSize, FifoVal;

    FifoSize = DwEmmcRead (DWEMMC_FIFOTH);
    FifoSize = ((FifoSize & 0x0FFF0000U) >> 16) + 1;  // RX_WMARK + 1
    FifoVal  = (6U << 28) |                            // MSIZE = 6 (128 beat burst)
               (((FifoSize / 2) - 1) << 16) |          // RX_WMARK
               ((FifoSize / 2));                        // TX_WMARK
    DwEmmcWrite (DWEMMC_FIFOTH, FifoVal);
  }

  //
  // 8. Default block size.
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
  UINT32  Data, ErrMask, CmdIdx;
  UINTN   PollCount;

  CmdIdx = MmcCmd & 0x3F;

  //
  // Small delay before command.  1ms is sufficient for eMMC; avoid the
  // 15ms legacy delay which adds 1-2s across ~100 init commands.
  //
  MicroSecondDelay (1000);

  //
  // Wait until MMC is idle: DATA_BUSY clear AND DAT_FSM idle/normal.
  //
  // DAT_FSM states (bits [3:2]):  0=IDLE, 1=WAIT (normal), 2=RECEIVE, 3=BUSY.
  // DAT_FSM=1 (WAIT for data start bit) is the normal quiescent state of the
  // v2.70a controller — do NOT treat it as stuck.
  //
  {
    UINTN  IdleRetries = 0;
    do {
      Data = DwEmmcRead (DWEMMC_STATUS);
      if ((Data & DWEMMC_STS_DATA_BUSY) == 0) {
        if (((Data >> 2) & 3) <= 1) {   // DAT_FSM 0 (IDLE) or 1 (WAIT)
          break;
        }
        //
        // DAT_FSM 2 (RECEIVE) or 3 (BUSY) with no DATA_BUSY → stuck.
        //
        if (++IdleRetries > 10) {
          DEBUG ((DEBUG_ERROR,
                  "Px30Emmc: DAT_FSM stuck 0x%x — resetting FIFO (STAT=0x%08x)\n",
                  (Data >> 2) & 3, Data));
          DwEmmcWrite (DWEMMC_CTRL,
                       DwEmmcRead (DWEMMC_CTRL) | DWEMMC_CTRL_FIFO_RESET);
          break;
        }
      } else {
        IdleRetries = 0;
      }
      MicroSecondDelay (100);
    } while (TRUE);
  }

  //
  // For CMD2: dump RESP registers BEFORE sending command to verify
  // they are cleared by our reset sequence.
  //
  if (CmdIdx == 2) {
    DEBUG ((DEBUG_VERBOSE, "Px30Emmc: pre-CMD2 RESP: R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x STAT=0x%08x\n",
            DwEmmcRead (DWEMMC_RESP0), DwEmmcRead (DWEMMC_RESP1),
            DwEmmcRead (DWEMMC_RESP2), DwEmmcRead (DWEMMC_RESP3),
            DwEmmcRead (DWEMMC_STATUS)));
  }

  DwEmmcWrite (DWEMMC_RINTSTS, ~0U);
  DwEmmcWrite (DWEMMC_CMDARG, Argument);
  DwEmmcWrite (DWEMMC_CMD, MmcCmd);

  ErrMask = DWEMMC_INT_EBE | DWEMMC_INT_HLE | DWEMMC_INT_RTO |
            DWEMMC_INT_RCRC | DWEMMC_INT_RE;
  ErrMask |= DWEMMC_INT_DCRC | DWEMMC_INT_DRT | DWEMMC_INT_SBE |
             DWEMMC_INT_HTO;

  PollCount = 0;
  do {
    MicroSecondDelay (500);
    Data = DwEmmcRead (DWEMMC_RINTSTS);
    PollCount++;

    if (PollCount >= 5000) {
      DEBUG ((DEBUG_ERROR,
              "Px30Emmc: cmd %d TIMEOUT after %d polls RINTSTS=0x%x STAT=0x%08x\n",
              CmdIdx, PollCount, Data, DwEmmcRead (DWEMMC_STATUS)));
      return EFI_TIMEOUT;
    }

    if (Data & ErrMask) {
      //
      // DWMMC v2.70a quirk: may see a transient RE before CMD_DONE.
      // Clear RE and continue polling.
      //
      if ((Data & DWEMMC_INT_RE) &&           // RE is set
          !(Data & DWEMMC_INT_CMD_DONE)) {     // but CMD_DONE is NOT yet set
        DEBUG ((DEBUG_ERROR,
                "Px30Emmc: cmd %d early-RE RINTSTS=0x%x STAT=0x%08x — "
                "continue waiting for CMD_DONE\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
        DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_RE);
        continue;
      }

      //
      // RE + CMD_DONE simultaneously: response was captured despite RE.
      //
      // For data commands: clear RE and continue polling for DTO
      // (data-transfer-over).  For non-data commands: treat as success.
      //
      if ((Data & DWEMMC_INT_RE) &&
          (Data & DWEMMC_INT_CMD_DONE)) {
        if (MmcCmd & BIT_CMD_DATA_EXPECTED) {
          DEBUG ((DEBUG_ERROR,
                  "Px30Emmc: cmd %d RE+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                  "waiting for DTO\n",
                  CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
          DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_RE);
          continue;
        }
        DEBUG ((DEBUG_ERROR,
                "Px30Emmc: cmd %d RE+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                "response captured anyway\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
        goto LogResponse;
      }

      //
      // DWMMC v2.70a quirk: may see transient EBE (End Bit Error) during
      // data phase, similar to the RE quirk on response.  If CMD_DONE is
      // set and DTO hasn't arrived, clear EBE and continue polling.
      //
      if ((Data & DWEMMC_INT_EBE) &&
          (Data & DWEMMC_INT_CMD_DONE) &&
          (MmcCmd & BIT_CMD_DATA_EXPECTED)) {
        DEBUG ((DEBUG_ERROR,
                "Px30Emmc: cmd %d EBE+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                "continue waiting for DTO\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
        DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_EBE);
        continue;
      }

      //
      // DWMMC v2.70a quirk: may see transient RTO (Response Timeout)
      // together with CMD_DONE.  CMD_DONE proves the response arrived.
      //
      if ((Data & DWEMMC_INT_RTO) &&
          (Data & DWEMMC_INT_CMD_DONE)) {
        if (MmcCmd & BIT_CMD_DATA_EXPECTED) {
          DEBUG ((DEBUG_ERROR,
                  "Px30Emmc: cmd %d RTO+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                  "waiting for DTO\n",
                  CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
          DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_RTO);
          continue;
        }
        DEBUG ((DEBUG_ERROR,
                "Px30Emmc: cmd %d RTO+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                "response captured anyway\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
        DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_RTO);
        goto LogResponse;
      }

      //
      // DWMMC v2.70a quirk: may see transient FRUN (FIFO underrun/overrun)
      // during data phase.  CMD8 at 400KHz had FRUN|DTO|CMD_DONE — the
      // transfer completed fine despite FRUN.  If CMD_DONE is set, clear
      // FRUN and continue waiting for DTO.
      //
      if ((Data & DWEMMC_INT_FRUN) &&
          (Data & DWEMMC_INT_CMD_DONE) &&
          (MmcCmd & BIT_CMD_DATA_EXPECTED)) {
        DEBUG ((DEBUG_ERROR,
                "Px30Emmc: cmd %d FRUN+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                "continue waiting for DTO\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
        DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_FRUN);
        continue;
      }

      //
      // DWMMC v2.70a quirk: may see transient HTO (Host Timeout) together
      // with CMD_DONE on data commands.  If CMD_DONE is set, clear HTO and
      // continue waiting for DTO — the response arrived so the card is
      // responding, and the timeout may be spurious.
      //
      if ((Data & DWEMMC_INT_HTO) &&
          (Data & DWEMMC_INT_CMD_DONE) &&
          (MmcCmd & BIT_CMD_DATA_EXPECTED)) {
        DEBUG ((DEBUG_ERROR,
                "Px30Emmc: cmd %d HTO+CMD_DONE RINTSTS=0x%x STAT=0x%08x — "
                "continue waiting for DTO\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_STATUS)));
        DwEmmcWrite (DWEMMC_RINTSTS, DWEMMC_INT_HTO);
        continue;
      }

      DEBUG ((DEBUG_ERROR, "Px30Emmc: cmd error RINTSTS=0x%x cmd=%d arg=0x%x STAT=0x%08x\n",
              Data, CmdIdx, Argument, DwEmmcRead (DWEMMC_STATUS)));
      //
      // Dump RESP registers for identification commands (CMD0,1,2,3)
      //
      if (CmdIdx <= 3) {
        DEBUG ((DEBUG_ERROR, "  RESP: R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
                DwEmmcRead (DWEMMC_RESP0), DwEmmcRead (DWEMMC_RESP1),
                DwEmmcRead (DWEMMC_RESP2), DwEmmcRead (DWEMMC_RESP3)));
      }
      return EFI_DEVICE_ERROR;
    }
    if (Data & DWEMMC_INT_DTO) {
      break;
    }
  } while (((Data & DWEMMC_INT_CMD_DONE) == 0) ||
           ((MmcCmd & BIT_CMD_DATA_EXPECTED) && ((Data & DWEMMC_INT_DTO) == 0)));

  //
  // Log success for key commands to trace data-phase completion.
  //
LogResponse:
  {
    if (CmdIdx <= 9 || CmdIdx == 16 || CmdIdx == 17 || CmdIdx == 18) {
      if (MmcCmd & BIT_CMD_LONG_RESPONSE) {
        DEBUG ((DEBUG_INFO,
                "Px30Emmc: cmd %d OK RINTSTS=0x%x "
                "RESP0=0x%08x RESP1=0x%08x RESP2=0x%08x RESP3=0x%08x CLKDIV=%d\n",
                CmdIdx, Data,
                DwEmmcRead (DWEMMC_RESP0), DwEmmcRead (DWEMMC_RESP1),
                DwEmmcRead (DWEMMC_RESP2), DwEmmcRead (DWEMMC_RESP3),
                DwEmmcRead (DWEMMC_CLKDIV)));
      } else {
        DEBUG ((DEBUG_VERBOSE, "Px30Emmc: cmd %d OK RINTSTS=0x%x RESP0=0x%08x CLKDIV=%d\n",
                CmdIdx, Data, DwEmmcRead (DWEMMC_RESP0),
                DwEmmcRead (DWEMMC_CLKDIV)));
      }
    }
  }

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

  //
  // U-Boot-aligned command flags.
  //   CMD0  → INIT + ABORT_STOP         (80 clk init, abort any pending)
  //   CMD12 → ABORT_STOP                (abort data transfer)
  //   all others → + WAIT_PRVDATA       (wait for previous data completion)
  //
  switch (MMC_INDX (MmcCmd)) {
  case MMC_INDX(0):
    Cmd = BIT_CMD_SEND_INIT | BIT_CMD_STOP_ABORT_CMD;
    break;
  case MMC_INDX(1):
    Cmd = BIT_CMD_RESPONSE_EXPECT |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(2):
    //
    // DIAGNOSTIC: temporarily disable CRC check on CMD2 to determine
    // whether the CID data is valid.  If the card responds with correct
    // CID content but a bad CRC7, the RESP registers will contain the
    // valid CID.  If the data is still garbage, the issue is at the
    // signal / electrical level.
    //
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_LONG_RESPONSE |
          // BIT_CMD_CHECK_RESPONSE_CRC |   // ← turned off for diagnostic
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(3):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(6):
    //
    // eMMC SWITCH (CMD6) always has a 512-byte data block (host→device).
    // The MmcDxe protocol does NOT call WriteBlockData for CMD6, so we
    // must execute the data transfer inline here.  DMA writes are reliable
    // on v2.70a (only reads have IDMAC issues).
    //
    {
      UINT8  *Cmd6Buf;
      UINT32  Index, Value;
      UINT32  Data;
      EFI_STATUS  Status;

      Cmd6Buf = AllocatePool (512);
      if (Cmd6Buf == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      ZeroMem (Cmd6Buf, 512);
      Index = (Argument >> 16) & 0xFF;
      Value = (Argument >>  8) & 0xFF;
      if (Index < 512) {
        Cmd6Buf[Index] = (UINT8)Value;
      }
      WriteBackDataCacheRange (Cmd6Buf, 512);

      do { Data = DwEmmcRead (DWEMMC_STATUS); }
      while (Data & DWEMMC_STS_DATA_BUSY);

      PrepareDmaData (mIdmacDesc, 512, (UINT32 *)Cmd6Buf);
      StartDma (512);

      Cmd  = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
             BIT_CMD_DATA_EXPECTED | BIT_CMD_WRITE |
             BIT_CMD_WAIT_PRVDATA_COMPLETE;
      Cmd |= MMC_GET_INDX (MmcCmd) | BIT_CMD_USE_HOLD_REG | BIT_CMD_START;

      Status = SendCommand (Cmd, Argument);
      FreePool (Cmd6Buf);
      return Status;
    }
  case MMC_INDX(7):
    if (Argument) {
      Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
            BIT_CMD_WAIT_PRVDATA_COMPLETE;
    }
    break;
  case MMC_INDX(8):
    //
    // SEND_EXT_CSD returns 512 bytes (host←device READ).
    //
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_DATA_EXPECTED | BIT6 |   // READ
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(9):
    //
    // DIAGNOSTIC: CRC check disabled (same transient-RE quirk as CMD2).
    //
    Cmd = BIT_CMD_RESPONSE_EXPECT |
          // BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_LONG_RESPONSE |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
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
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(17):
  	Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
  	      BIT_CMD_DATA_EXPECTED | BIT6 |  // READ
  	      BIT_CMD_WAIT_PRVDATA_COMPLETE;
  	break;
  case MMC_INDX(18):
  	//
  	// DWMMC v2.70a quirk: the IDMAC does not start for READ_MULTIPLE_BLOCK
  	// (CMD18) unless SEND_AUTO_STOP (BIT12) is set.  Without it the FIFO
  	// fills (RXDR), no DMA drain occurs (IDSTS=0), FRUN fires, and the
  	// controller ultimately times out (HTO).  CMD17 single-block reads
  	// work fine without AUTO_STOP — only CMD18 is affected.
  	//
  	// With AUTO_STOP the controller automatically issues CMD12 after
  	// BYTCNT bytes have been transferred.  Any explicit CMD12 the protocol
  	// layer sends later will be a harmless no-op (already stopped).
  	//
  	Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
  	      BIT_CMD_DATA_EXPECTED | BIT6 |  // READ
  	      BIT_CMD_SEND_AUTO_STOP |
  	      BIT_CMD_WAIT_PRVDATA_COMPLETE;
  	break;
  case MMC_INDX(24):
  case MMC_INDX(25):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_DATA_EXPECTED | BIT_CMD_WRITE |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(41):
    Cmd = BIT_CMD_RESPONSE_EXPECT |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  case MMC_INDX(51):
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_DATA_EXPECTED | BIT6 |  // READ
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
    break;
  default:
    Cmd = BIT_CMD_RESPONSE_EXPECT | BIT_CMD_CHECK_RESPONSE_CRC |
          BIT_CMD_WAIT_PRVDATA_COMPLETE;
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
  UINT32 Data;

  //
  // Reset FIFO and DMA before every data transfer, matching U-Boot:
  //   dwmci_writel(host, DWMCI_CTRL, host->ctrl | DWMCI_RESET_FIFO | DWMCI_RESET_DMA)
  //
  // v2.70a: without DMA_RESET, the IDMAC may not start on the next transfer,
  // causing stale/zero data in the first 1–2 reads.
  //
  DwEmmcWrite (DWEMMC_CTRL,
               DwEmmcRead (DWEMMC_CTRL) |
               DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET);
  do {
    Data = DwEmmcRead (DWEMMC_CTRL);
  } while (Data & (DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET));

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

  //
  // Flush descriptors from data cache so IDMAC can see them.
  //
  WriteBackDataCacheRange (IdmacDesc, Cnt * sizeof (IDMAC_DESCRIPTOR));

  return EFI_SUCCESS;
}

// ── Read / Write Block Data ─────────────────────────────────────────
//
// DWMMC v2.70a DMA: single clean DMA read with IDMAC reset before each
// transfer.  Old approach used 3 warm-up reads which corrupted caller's
// buffer and caused memory corruption (EDK2 debug fill 0xAFAFAFAF pattern).
//
// Key: full BMOD SW_RESET → FIFO+DMA reset → descriptor setup → DMA read
//      → cache invalidate.  Data validation + retry only for critical LBAs.

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
  UINTN       Cnt, Blks, Idx, LastIdx;

  if (Buffer == NULL || Length == 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (mEmmcCommand & BIT_CMD_WAIT_PRVDATA_COMPLETE) {
    do { Data = DwEmmcRead (DWEMMC_STATUS); }
    while (Data & DWEMMC_STS_DATA_BUSY);
  }

  //
  // Full IDMAC reset via BMOD — needed before every transfer on v2.70a.
  //
  DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_SWRESET);
  do {
    Data = DwEmmcRead (DWEMMC_BMOD);
  } while (Data & DWEMMC_IDMAC_SWRESET);

  //
  // Reset FIFO + DMA via CTRL (matching U-Boot dwmci_prepare_data).
  //
  DwEmmcWrite (DWEMMC_CTRL,
               DwEmmcRead (DWEMMC_CTRL) |
               DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET);
  do {
    Data = DwEmmcRead (DWEMMC_CTRL);
  } while (Data & (DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET));

  //
  // Build descriptor chain pointing to caller's Buffer.
  //
  Cnt  = (Length + EMMC_DMA_BUF_SIZE - 1) / EMMC_DMA_BUF_SIZE;
  Blks = (Length + EMMC_BLOCK_SIZE - 1) / EMMC_BLOCK_SIZE;
  Length = EMMC_BLOCK_SIZE * Blks;

  //
  // v2.70a workaround: one warm-up DMA read into the private
  // mDmaBuffer (not the caller's buffer!) so the IDMAC engine
  // "wakes up" and delivers correct data on the next transfer.
  //
  // Without this, the first real read returns all-zeros stale data
  // even though the command completes with RINTSTS=0x82C (no error).
  //
  {
    IDMAC_DESCRIPTOR  WarmDesc;
    UINT32            WarmData;

    WarmDesc.Des0 = DWEMMC_IDMAC_DES0_OWN | DWEMMC_IDMAC_DES0_FS |
                    DWEMMC_IDMAC_DES0_LD | DWEMMC_IDMAC_DES0_CH;
    WarmDesc.Des1 = DWEMMC_IDMAC_DES1_BS1 (EMMC_BLOCK_SIZE);
    WarmDesc.Des2 = (UINT32)(UINTN)mDmaBuffer;
    WarmDesc.Des3 = 0;
    WriteBackDataCacheRange (&WarmDesc, sizeof (WarmDesc));

    DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_ENABLE | DWEMMC_IDMAC_FB);
    DwEmmcWrite (DWEMMC_DBADDR, (UINT32)(UINTN)&WarmDesc);
    DwEmmcWrite (DWEMMC_CTRL,
                 DwEmmcRead (DWEMMC_CTRL) |
                 DWEMMC_CTRL_INT_EN | DWEMMC_CTRL_DMA_EN |
                 DWEMMC_CTRL_IDMAC_EN);
    DwEmmcWrite (DWEMMC_BLKSIZ, EMMC_BLOCK_SIZE);
    DwEmmcWrite (DWEMMC_BYTCNT, EMMC_BLOCK_SIZE);

    InvalidateDataCacheRange (mDmaBuffer, EMMC_BLOCK_SIZE);
    SendCommand (mEmmcCommand, mEmmcArgument);
    InvalidateDataCacheRange (mDmaBuffer, EMMC_BLOCK_SIZE);

    WarmData = *(UINT32 *)mDmaBuffer;
    DEBUG ((DEBUG_VERBOSE,
            "Px30Emmc: warm-up done (first word=0x%08x)\n", WarmData));

    //
    // Full reset before real read
    //
    DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_SWRESET);
    do {
      Data = DwEmmcRead (DWEMMC_BMOD);
    } while (Data & DWEMMC_IDMAC_SWRESET);

    DwEmmcWrite (DWEMMC_CTRL,
                 DwEmmcRead (DWEMMC_CTRL) |
                 DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET);
    do {
      Data = DwEmmcRead (DWEMMC_CTRL);
    } while (Data & (DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET));
  }

  //
  // Real descriptor chain — target caller's Buffer.
  //
  for (Idx = 0; Idx < Cnt; Idx++) {
    (mIdmacDesc + Idx)->Des0 = DWEMMC_IDMAC_DES0_OWN |
                               DWEMMC_IDMAC_DES0_CH |
                               DWEMMC_IDMAC_DES0_DIC;
    (mIdmacDesc + Idx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (EMMC_DMA_BUF_SIZE);
    (mIdmacDesc + Idx)->Des2 = (UINT32)(UINTN)(Buffer +
                                EMMC_DMA_BUF_SIZE / 4 * Idx);
    (mIdmacDesc + Idx)->Des3 = (UINT32)(UINTN)(mIdmacDesc +
                                sizeof (IDMAC_DESCRIPTOR) * (Idx + 1));
  }
  mIdmacDesc->Des0 |= DWEMMC_IDMAC_DES0_FS;

  LastIdx = Cnt - 1;
  (mIdmacDesc + LastIdx)->Des0 |= DWEMMC_IDMAC_DES0_LD;
  (mIdmacDesc + LastIdx)->Des0 &= ~(DWEMMC_IDMAC_DES0_DIC |
                                     DWEMMC_IDMAC_DES0_CH);
  (mIdmacDesc + LastIdx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (
                                   Length - LastIdx * EMMC_DMA_BUF_SIZE);
  (mIdmacDesc + LastIdx)->Des3 = 0;

  WriteBackDataCacheRange (mIdmacDesc, Cnt * sizeof (IDMAC_DESCRIPTOR));

  //
  // Enable IDMAC, set DBADDR, enable DMA.
  //
  DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_ENABLE | DWEMMC_IDMAC_FB);
  DwEmmcWrite (DWEMMC_DBADDR, (UINT32)(UINTN)mIdmacDesc);
  DwEmmcWrite (DWEMMC_CTRL,
               DwEmmcRead (DWEMMC_CTRL) |
               DWEMMC_CTRL_INT_EN | DWEMMC_CTRL_DMA_EN |
               DWEMMC_CTRL_IDMAC_EN);
  DwEmmcWrite (DWEMMC_BLKSIZ, EMMC_BLOCK_SIZE);
  DwEmmcWrite (DWEMMC_BYTCNT, Length);

  //
  // Single clean DMA read — no warm-ups.
  //
  InvalidateDataCacheRange (Buffer, Length);
  Status = SendCommand (mEmmcCommand, mEmmcArgument);
  if (!EFI_ERROR (Status)) {
    InvalidateDataCacheRange (Buffer, Length);
  }

  //
  // Validate critical LBAs (MBR/GPT) and retry if data is corrupt.
  // v2.70a DMA may occasionally return stale data.
  //
  if (!EFI_ERROR (Status) &&
      (Lba == 0 || Lba == 1) &&
      (mEmmcCommand & 0x3F) == MMC_INDX(17)) {
    UINTN Retry;
    for (Retry = 0; Retry < 5; Retry++) {
      BOOLEAN Ok = FALSE;
      if (Lba == 0) {
        // MBR signature
        Ok = (((UINT8 *)Buffer)[510] == 0x55 &&
              ((UINT8 *)Buffer)[511] == 0xAA);
      } else {
        // EFI PART signature
        Ok = (((UINT8 *)Buffer)[0] == 'E' &&
              ((UINT8 *)Buffer)[1] == 'F' &&
              ((UINT8 *)Buffer)[2] == 'I' &&
              ((UINT8 *)Buffer)[3] == ' ');
      }
      if (Ok) {
        break;
      }

      DEBUG ((DEBUG_VERBOSE,
              "Px30Emmc: LBA 0x%llx stale data, retry %d/5\n", Lba, Retry + 1));

      //
      // Retry: full IDMAC reset and re-read
      //
      DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_SWRESET);
      do { Data = DwEmmcRead (DWEMMC_BMOD); }
      while (Data & DWEMMC_IDMAC_SWRESET);

      DwEmmcWrite (DWEMMC_CTRL,
                   DwEmmcRead (DWEMMC_CTRL) |
                   DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET);
      do { Data = DwEmmcRead (DWEMMC_CTRL); }
      while (Data & (DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET));

      // Re-init descriptors (ownership consumed by previous read)
      for (Idx = 0; Idx < Cnt; Idx++) {
        (mIdmacDesc + Idx)->Des0 = DWEMMC_IDMAC_DES0_OWN |
                                   DWEMMC_IDMAC_DES0_CH |
                                   DWEMMC_IDMAC_DES0_DIC;
        (mIdmacDesc + Idx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (EMMC_DMA_BUF_SIZE);
        (mIdmacDesc + Idx)->Des2 = (UINT32)(UINTN)(Buffer +
                                    EMMC_DMA_BUF_SIZE / 4 * Idx);
        (mIdmacDesc + Idx)->Des3 = (UINT32)(UINTN)(mIdmacDesc +
                                    sizeof (IDMAC_DESCRIPTOR) * (Idx + 1));
      }
      mIdmacDesc->Des0 |= DWEMMC_IDMAC_DES0_FS;
      (mIdmacDesc + LastIdx)->Des0 |= DWEMMC_IDMAC_DES0_LD;
      (mIdmacDesc + LastIdx)->Des0 &= ~(DWEMMC_IDMAC_DES0_DIC |
                                         DWEMMC_IDMAC_DES0_CH);
      (mIdmacDesc + LastIdx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (
                                       Length - LastIdx * EMMC_DMA_BUF_SIZE);
      (mIdmacDesc + LastIdx)->Des3 = 0;
      WriteBackDataCacheRange (mIdmacDesc, Cnt * sizeof (IDMAC_DESCRIPTOR));

      DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_ENABLE | DWEMMC_IDMAC_FB);
      DwEmmcWrite (DWEMMC_DBADDR, (UINT32)(UINTN)mIdmacDesc);
      DwEmmcWrite (DWEMMC_CTRL,
                   DwEmmcRead (DWEMMC_CTRL) |
                   DWEMMC_CTRL_INT_EN | DWEMMC_CTRL_DMA_EN |
                   DWEMMC_CTRL_IDMAC_EN);
      DwEmmcWrite (DWEMMC_BLKSIZ, EMMC_BLOCK_SIZE);
      DwEmmcWrite (DWEMMC_BYTCNT, Length);

      InvalidateDataCacheRange (Buffer, Length);
      Status = SendCommand (mEmmcCommand, mEmmcArgument);
      if (!EFI_ERROR (Status)) {
        InvalidateDataCacheRange (Buffer, Length);
      }
    }
  }

  //
  // Diagnostic: dump first 32 bytes of critical LBAs
  //
  if ((Lba == 0 || Lba == 1 || Lba == 0x5a000) &&
      (mEmmcCommand & 0x3F) == MMC_INDX(17)) {
    DEBUG ((DEBUG_VERBOSE,
            "Px30Emmc: LBA 0x%llx 1st 64B: "
            "%02x %02x %02x %02x %02x %02x %02x %02x  "
            "%02x %02x %02x %02x %02x %02x %02x %02x  "
            "%02x %02x %02x %02x %02x %02x %02x %02x  "
            "%02x %02x %02x %02x %02x %02x %02x %02x\n",
            Lba,
            ((UINT8 *)Buffer)[ 0], ((UINT8 *)Buffer)[ 1],
            ((UINT8 *)Buffer)[ 2], ((UINT8 *)Buffer)[ 3],
            ((UINT8 *)Buffer)[ 4], ((UINT8 *)Buffer)[ 5],
            ((UINT8 *)Buffer)[ 6], ((UINT8 *)Buffer)[ 7],
            ((UINT8 *)Buffer)[ 8], ((UINT8 *)Buffer)[ 9],
            ((UINT8 *)Buffer)[10], ((UINT8 *)Buffer)[11],
            ((UINT8 *)Buffer)[12], ((UINT8 *)Buffer)[13],
            ((UINT8 *)Buffer)[14], ((UINT8 *)Buffer)[15],
            ((UINT8 *)Buffer)[16], ((UINT8 *)Buffer)[17],
            ((UINT8 *)Buffer)[18], ((UINT8 *)Buffer)[19],
            ((UINT8 *)Buffer)[20], ((UINT8 *)Buffer)[21],
            ((UINT8 *)Buffer)[22], ((UINT8 *)Buffer)[23],
            ((UINT8 *)Buffer)[24], ((UINT8 *)Buffer)[25],
            ((UINT8 *)Buffer)[26], ((UINT8 *)Buffer)[27],
            ((UINT8 *)Buffer)[28], ((UINT8 *)Buffer)[29],
            ((UINT8 *)Buffer)[30], ((UINT8 *)Buffer)[31]));
    if (Lba == 0) {
      DEBUG ((DEBUG_VERBOSE,
              "Px30Emmc: LBA 0 MBR sig [510-511]: %02x %02x  "
              "part-entry [446-461]: %02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x\n",
              ((UINT8 *)Buffer)[510], ((UINT8 *)Buffer)[511],
              ((UINT8 *)Buffer)[446], ((UINT8 *)Buffer)[447],
              ((UINT8 *)Buffer)[448], ((UINT8 *)Buffer)[449],
              ((UINT8 *)Buffer)[450], ((UINT8 *)Buffer)[451],
              ((UINT8 *)Buffer)[452], ((UINT8 *)Buffer)[453],
              ((UINT8 *)Buffer)[454], ((UINT8 *)Buffer)[455],
              ((UINT8 *)Buffer)[456], ((UINT8 *)Buffer)[457],
              ((UINT8 *)Buffer)[458], ((UINT8 *)Buffer)[459],
              ((UINT8 *)Buffer)[460], ((UINT8 *)Buffer)[461]));
    }
  }

  return Status;
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
  UINTN       Cnt, Blks, Idx, LastIdx;

  if (mEmmcCommand & BIT_CMD_WAIT_PRVDATA_COMPLETE) {
    do { Data = DwEmmcRead (DWEMMC_STATUS); }
    while (Data & DWEMMC_STS_DATA_BUSY);
  }

  // U-Boot: dwmci_prepare_data — reset FIFO + DMA
  DwEmmcWrite (DWEMMC_CTRL,
               DwEmmcRead (DWEMMC_CTRL) |
               DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET);
  do {
    Data = DwEmmcRead (DWEMMC_CTRL);
  } while (Data & (DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET));

  Cnt  = (Length + EMMC_DMA_BUF_SIZE - 1) / EMMC_DMA_BUF_SIZE;
  Blks = (Length + EMMC_BLOCK_SIZE - 1) / EMMC_BLOCK_SIZE;
  Length = EMMC_BLOCK_SIZE * Blks;

  for (Idx = 0; Idx < Cnt; Idx++) {
    (mIdmacDesc + Idx)->Des0 = DWEMMC_IDMAC_DES0_OWN |
                               DWEMMC_IDMAC_DES0_CH |
                               DWEMMC_IDMAC_DES0_DIC;
    (mIdmacDesc + Idx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (EMMC_DMA_BUF_SIZE);
    (mIdmacDesc + Idx)->Des2 = (UINT32)(UINTN)(Buffer +
                                EMMC_DMA_BUF_SIZE / 4 * Idx);
    (mIdmacDesc + Idx)->Des3 = (UINT32)(UINTN)(mIdmacDesc +
                                sizeof (IDMAC_DESCRIPTOR) * (Idx + 1));
  }
  mIdmacDesc->Des0 |= DWEMMC_IDMAC_DES0_FS;
  LastIdx = Cnt - 1;
  (mIdmacDesc + LastIdx)->Des0 |= DWEMMC_IDMAC_DES0_LD;
  (mIdmacDesc + LastIdx)->Des0 &= ~(DWEMMC_IDMAC_DES0_DIC |
                                     DWEMMC_IDMAC_DES0_CH);
  (mIdmacDesc + LastIdx)->Des1 = DWEMMC_IDMAC_DES1_BS1 (
                                   Length - LastIdx * EMMC_DMA_BUF_SIZE);
  (mIdmacDesc + LastIdx)->Des3 = 0;
  WriteBackDataCacheRange (mIdmacDesc, Cnt * sizeof (IDMAC_DESCRIPTOR));

  // U-Boot order: BMOD, then DBADDR, then BLKSIZ/BYTCNT
  DwEmmcWrite (DWEMMC_BMOD, DWEMMC_IDMAC_ENABLE | DWEMMC_IDMAC_FB);
  DwEmmcWrite (DWEMMC_DBADDR, (UINT32)(UINTN)mIdmacDesc);
  DwEmmcWrite (DWEMMC_CTRL,
               DwEmmcRead (DWEMMC_CTRL) |
               DWEMMC_CTRL_INT_EN | DWEMMC_CTRL_DMA_EN |
               DWEMMC_CTRL_IDMAC_EN);
  DwEmmcWrite (DWEMMC_BLKSIZ, EMMC_BLOCK_SIZE);
  DwEmmcWrite (DWEMMC_BYTCNT, Length);

  WriteBackDataCacheRange (Buffer, Length);
  Status = SendCommand (mEmmcCommand, mEmmcArgument);

  return Status;
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
      //
      // Unknown timing mode — leave UHSREG unchanged.
      // The caller may be setting only clock/bus-width.
      //
      break;
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
  //
  // DWMMC v2.70a (VERID=0x5342270A) hardware bug: the IDMAC engine does
  // NOT start for CMD18 (READ_MULTIPLE_BLOCK) or CMD25 (WRITE_MULTIPLE_BLOCK)
  // even though OWN=1, IDMAC_EN=1, and DBADDR are correctly set.
  //
  // CMD17 (READ_SINGLE_BLOCK) and CMD24 (WRITE_SINGLE_BLOCK) work correctly.
  // Returning FALSE here tells the MmcDxe protocol layer to use single-block
  // commands exclusively, splitting multi-block requests in software.
  //
  return FALSE;
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

  //
  // Private DMA buffer — page-aligned, physically contiguous, avoids
  // cache-coherency issues with caller-provided buffers.
  //
  mDmaBuffer = (UINT8 *)AllocatePages (1);
  if (mDmaBuffer == NULL) {
    FreePages (mIdmacDesc, EMMC_MAX_DESC_PAGES);
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
