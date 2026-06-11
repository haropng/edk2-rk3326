/** @file
 *
 *  RkSdmmcDxe platform helper library — RK3326 (PX30) variant.
 *
 *  Differences from the RK3588 version:
 *    - PX30 has no SCMI clock provider (no SCP/MHU mailbox).  The
 *      SD card clock is programmed by writing CLKSEL registers
 *      directly through the CRU.
 *    - Card detect reads the DW MMC CDETECT hardware register (offset 0x050,
 *      bit0 = cdetect_n).  PX30 SDMMC card-detect pin is muxed by U-Boot
 *      and feeds directly into the DW MMC controller.
 *
 *  PX30 CRU SDMMC clock tree (from px30-cru.h / clk_px30.c):
 *    CLKSEL20_CON (CRU + 0x164):
 *      bits[15:14] = SRC_SEL: 0=GPLL(1200MHz), 1=CPLL, 2=NPLL(1188MHz),
 *                               3=XIN24M
 *      bits[13:0]  = DIVIDER (1..16383, programmed as N-1)
 *    CLKSEL21_CON (CRU + 0x168):
 *      bit[15]     = DIV_SEL: 0=CLK_EMMC (direct), 1=CLK_EMMC_DIV50
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2026, RK3326 EDK2 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/RkSdmmcPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Soc.h>

//
// PX30 CRU register offsets relative to CRU_BASE (0xFF2B0000).
// clksel_con[0] starts at CRU_BASE + 0x114.
//
#define CLKSEL_OFFSET         0x114U
#define CLKSEL20_CON          (CRU_BASE + CLKSEL_OFFSET + 20 * 4)  // 0x164
#define CLKSEL21_CON          (CRU_BASE + CLKSEL_OFFSET + 21 * 4)  // 0x168

//
// CLKSEL20_CON bit fields — shared by SDMMC and EMMC on PX30
//
#define EMMC_SRC_SEL_SHIFT    14
#define EMMC_SRC_SEL_MASK     (3U << EMMC_SRC_SEL_SHIFT)
#define EMMC_SRC_SEL_GPLL     0
#define EMMC_SRC_SEL_CPLL     1
#define EMMC_SRC_SEL_NPLL     2
#define EMMC_SRC_SEL_24M      3
#define EMMC_DIV_SHIFT        0
#define EMMC_DIV_MASK         0x3FFFU

//
// CLKSEL21_CON bit fields
//
#define EMMC_CLK_SEL_SHIFT    15
#define EMMC_CLK_SEL_MASK     (1U << EMMC_CLK_SEL_SHIFT)
#define EMMC_CLK_SEL_DIRECT   0
#define EMMC_CLK_SEL_DIV50    1

//
// CRU clock gate registers
//
#define CLKGATE_OFFSET        0x100U   // Relative to clkgate_con start
                                       // clkgate_con[0] = CRU + 0x244

//
// DW MMC CDETECT register offset
//
#define DW_MMC_CDETECT_OFF    0x050U

//
// Available PLL rates for SDMMC source selection
//
#define GPLL_HZ               (1200 * 1000 * 1000U)
#define NPLL_HZ               (1188 * 1000 * 1000U)
#define CPLL_HZ               (1000 * 1000 * 1000U)  // typically 1GHz
#define XIN24M_HZ             (24 * 1000 * 1000U)

/**
  Set the SDMMC controller clock rate by programming the CRU CLKSEL registers.

  Picks the best PLL source that can produce >= Frequency and derives an
  integer divider.  The card clock is further divided by the DW MMC controller
  itself, so coarse accuracy is acceptable.

  @param  Frequency   Desired base clock frequency in Hz.

  @retval EFI_SUCCESS           Clock configured.
  @retval EFI_INVALID_PARAMETER Frequency is 0.
**/
EFI_STATUS
EFIAPI
RkSdmmcSetClockRate (
  IN UINTN  Frequency
  )
{
  UINT32  Sel;
  UINT32  Parent;
  UINT32  Div;
  UINT32  Val;

  if (Frequency == 0) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Pick the lowest PLL parent that can produce >= Frequency.
  //
  if (Frequency <= XIN24M_HZ) {
    Sel    = EMMC_SRC_SEL_24M;
    Parent = XIN24M_HZ;
  } else if (Frequency <= CPLL_HZ) {
    Sel    = EMMC_SRC_SEL_CPLL;
    Parent = CPLL_HZ;
  } else if (Frequency <= NPLL_HZ) {
    Sel    = EMMC_SRC_SEL_NPLL;
    Parent = NPLL_HZ;
  } else {
    Sel    = EMMC_SRC_SEL_GPLL;
    Parent = GPLL_HZ;
  }

  Div = (Parent + Frequency - 1) / Frequency;
  if (Div < 1)  { Div = 1; }
  if (Div > 16383) { Div = 16383; }

  //
  // CLKSEL20_CON: set PLL source + divider.
  // Rockchip write convention: hi16 = write mask, lo16 = value.
  //
  Val = (EMMC_SRC_SEL_MASK << 16) | (EMMC_DIV_MASK << 16) |
        (Sel << EMMC_SRC_SEL_SHIFT) |
        ((Div - 1) << EMMC_DIV_SHIFT);
  MmioWrite32 (CLKSEL20_CON, Val);

  //
  // CLKSEL21_CON: use direct divider (not DIV50).
  //
  Val = (EMMC_CLK_SEL_MASK << 16) |
        (EMMC_CLK_SEL_DIRECT << EMMC_CLK_SEL_SHIFT);
  MmioWrite32 (CLKSEL21_CON, Val);

  DEBUG ((
    DEBUG_INFO,
    "RkSdmmcSetClockRate: req=%lu Hz -> parent=%u Hz / %u (sel=%u)\n",
    (UINT64)Frequency, Parent, Div, Sel
    ));

  return EFI_SUCCESS;
}

/**
  Configure SDMMC pin IOMUX.

  On PX30, SDMMC iomux is already set up by U-Boot during SPL/board_init.
  We call through to the board library for any additional setup needed.
**/
VOID
EFIAPI
RkSdmmcSetIoMux (
  VOID
  )
{
  SdmmcIoMux ();
}

/**
  Return card presence state by reading the DW MMC CDETECT hardware register.

  The PX30 SDMMC card-detect pin (GPIO1 A0 or similar, function SDMMC_DET)
  feeds directly into the DW MMC controller's CDETECT register.

  @retval RkSdmmcCardPresent     Card is inserted.
  @retval RkSdmmcCardNotPresent  No card.
**/
RKSDMMC_CARD_PRESENCE_STATE
EFIAPI
RkSdmmcGetCardPresenceState (
  VOID
  )
{
  UINT32  Cdetect;
  UINT32  Base;

  Base = FixedPcdGet32 (PcdRkSdmmcBaseAddress);
  Cdetect = MmioRead32 (Base + DW_MMC_CDETECT_OFF);

  //
  // CDETECT bit 0 = cdetect_n: 0 → card present, 1 → no card.
  //
  return (Cdetect & BIT0) ? RkSdmmcCardNotPresent : RkSdmmcCardPresent;
}
