/** @file
  RK3326 SoC DXE driver — early platform initialization.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Soc.h>

EFI_STATUS
EFIAPI
RK3326DxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "RK3326Dxe: SoC init\n"));
  DEBUG ((DEBUG_INFO, "  CRU:    0x%08x\n", CRU_BASE));
  DEBUG ((DEBUG_INFO, "  PMUCRU: 0x%08x\n", PMUCRU_BASE));
  DEBUG ((DEBUG_INFO, "  GICD:   0x%08x\n", GICD_BASE));
  DEBUG ((DEBUG_INFO, "  UART2:  0x%08x\n", UART2_BASE));
  DEBUG ((DEBUG_INFO, "  eMMC:   0x%08x  SDMMC: 0x%08x\n", EMMC_BASE, SDMMC_BASE));

  return EFI_SUCCESS;
}
