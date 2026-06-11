/** @file
 *
 *  RkEmmcDxe platform helper library — RK3326 (PX30) variant.
 *
 *  eMMC is a non-removable embedded device.  PX30's eMMC controller
 *  shares CRU CLKSEL20/21 registers with SDMMC.  U-Boot already
 *  configures the clock and IOMUX before loading EDK2.
 *
 *  Copyright (c) 2026, RK3326 EDK2 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/RkEmmcPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Soc.h>

//
// PX30 CRU register offsets — shared with SDMMC (see RkSdmmcPlatformLib.c).
//
#define CLKSEL_OFFSET         0x114U
#define CLKSEL20_CON          (CRU_BASE + CLKSEL_OFFSET + 20 * 4)
#define CLKSEL21_CON          (CRU_BASE + CLKSEL_OFFSET + 21 * 4)

#define EMMC_SRC_SEL_SHIFT    14
#define EMMC_SRC_SEL_MASK     (3U << EMMC_SRC_SEL_SHIFT)
#define EMMC_SRC_SEL_GPLL     0
#define EMMC_SRC_SEL_CPLL     1
#define EMMC_SRC_SEL_NPLL     2
#define EMMC_SRC_SEL_24M      3
#define EMMC_DIV_SHIFT        0
#define EMMC_DIV_MASK         0x3FFFU

#define EMMC_CLK_SEL_SHIFT    15
#define EMMC_CLK_SEL_MASK     (1U << EMMC_CLK_SEL_SHIFT)
#define EMMC_CLK_SEL_DIRECT   0
#define EMMC_CLK_SEL_DIV50    1

#define GPLL_HZ               (1200 * 1000 * 1000U)
#define NPLL_HZ               (1188 * 1000 * 1000U)
#define CPLL_HZ               (1000 * 1000 * 1000U)
#define XIN24M_HZ             (24 * 1000 * 1000U)

/**
  Set the eMMC controller clock rate via CRU CLKSEL registers.

  Note: PX30's SDMMC and eMMC share the same clock source register
  (CLKSEL20/21_CON).  Changing one affects the other.  U-Boot typically
  configures a common frequency; we preserve that unless explicitly
  asked to change.

  @param  Frequency   Desired base clock frequency in Hz.

  @retval EFI_SUCCESS           Clock configured.
  @retval EFI_INVALID_PARAMETER Frequency is 0.
**/
EFI_STATUS
EFIAPI
RkEmmcSetClockRate (
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

  Val = (EMMC_SRC_SEL_MASK << 16) | (EMMC_DIV_MASK << 16) |
        (Sel << EMMC_SRC_SEL_SHIFT) |
        ((Div - 1) << EMMC_DIV_SHIFT);
  MmioWrite32 (CLKSEL20_CON, Val);

  Val = (EMMC_CLK_SEL_MASK << 16) |
        (EMMC_CLK_SEL_DIRECT << EMMC_CLK_SEL_SHIFT);
  MmioWrite32 (CLKSEL21_CON, Val);

  DEBUG ((
    DEBUG_INFO,
    "RkEmmcSetClockRate: req=%lu Hz -> parent=%u Hz / %u (sel=%u)\n",
    (UINT64)Frequency, Parent, Div, Sel
    ));

  return EFI_SUCCESS;
}

/**
  Configure eMMC pin IOMUX.

  U-Boot already configures eMMC IOMUX during boot (needed to read
  the kernel/EDK2 image from eMMC).

  Also enables CRU clock gates and deasserts resets for the eMMC
  controller — these may get gated during the UEFI handoff.
**/
VOID
EFIAPI
RkEmmcSetIoMux (
  VOID
  )
{
  UINT32  Cru;

  Cru = CRU_BASE;

  //
  // Enable eMMC CRU clock gates.
  // Gate register: CRU + 0x100 + (id/16)*4, bit = id%16.
  // Rockchip write convention: hi16=mask, lo16=value.
  //
  #define GATE(b,id) do {                                         \
    UINT32 r_ = (b) + 0x100U + ((id) / 16U) * 4U;                \
    UINT32 m_ = 1U << ((id) % 16U);                               \
    MmioWrite32 (r_, (m_ << 16) | m_);                            \
  } while (0)

  #define RST(b,id) do {                                          \
    UINT32 r_ = (b) + 0x200U + ((id) / 16U) * 4U;                \
    UINT32 m_ = 1U << ((id) % 16U);                               \
    MmioWrite32 (r_, (m_ << 16) | 0);                             \
  } while (0)

  //
  // CRU clock IDs from px30-cru.h:
  //   HCLK_EMMC = 256, HCLK_MMC_NAND = 246 (NIU),
  //   SCLK_EMMC = 57
  //
  GATE (Cru, 256);   // HCLK_EMMC
  GATE (Cru, 246);   // HCLK_MMC_NAND (NIU for MMC controllers)
  GATE (Cru, 57);    // SCLK_EMMC

  //
  // Reset deassert:
  //   SRST_EMMC_H = 82, SRST_MMC_NAND_NIU_H = 80
  //
  RST (Cru, 80);    // SRST_MMC_NAND_NIU_H
  RST (Cru, 82);    // SRST_EMMC_H

  #undef GATE
  #undef RST

  SdhciEmmcIoMux ();
}

/**
  Return eMMC card presence state.

  eMMC is always present (embedded, non-removable).
**/
RKEMMC_CARD_PRESENCE_STATE
EFIAPI
RkEmmcGetCardPresenceState (
  VOID
  )
{
  return RkEmmcCardPresent;
}
