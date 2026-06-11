/** @file
 *
 *  Rockchip platform helper library — RK3326 (PX30) minimal variant.
 *
 *  On PX30, U-Boot already configures SDMMC/eMMC IOMUX during board_init.
 *  The SdmmcIoMux() function here is a placeholder.
 *
 *  Copyright (c) 2026, RK3326 EDK2 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/RockchipPlatformLib.h>

/**
  Configure SDMMC pin IOMUX.

  On PX30, the SDMMC pins (CLK, CMD, D0-D3, DET) are muxed by U-Boot
  during boot.  This function is a placeholder — add GRF register writes
  here if the board requires explicit re-muxing.
**/
VOID
EFIAPI
SdmmcIoMux (
  VOID
  )
{
  //
  // PX30: SDMMC iomux is already configured by U-Boot.
  // If board-specific re-muxing is needed, write the GPIO1 IOMUX
  // registers at GRF_BASE (0xFF140000):
  //
  //   GPIO1_A IOMUX: SDMMC_D0=fn1, SDMMC_D1=fn1, etc.
  //
  DEBUG ((DEBUG_INFO, "SdmmcIoMux: no-op (U-Boot already configured)\n"));
}

/**
  Configure eMMC pin IOMUX.

  On PX30, U-Boot already configures eMMC IOMUX during boot
  (needed to read the kernel/EDK2 image).
**/
VOID
EFIAPI
SdhciEmmcIoMux (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "SdhciEmmcIoMux: no-op (U-Boot already configured)\n"));
}
