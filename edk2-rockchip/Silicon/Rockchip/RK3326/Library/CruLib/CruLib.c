/** @file
  CruLib for RK3326 — CRU clock gate and soft reset control.

  Rockchip write-enable convention: bits[31:16] = write mask, bits[15:0] = value.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Soc.h>

STATIC
VOID
CruWriteWithMask (IN UINT32 RegAddr, IN UINT32 Mask, IN UINT32 Val)
{
  MmioWrite32 (RegAddr, ((Mask & 0xFFFF) << 16) | (Val & 0xFFFF));
}

VOID
EFIAPI
CruSetClkGate (IN UINT32 Base, IN UINT32 Offset, IN UINT32 Val)
{
  UINT32 RegAddr = (Base == PMUCRU_BASE) ? PMUCRU_BASE : CRU_BASE;
  RegAddr += 0x0100 + (Offset * 4);
  CruWriteWithMask (RegAddr, 1U << (Offset & 0xF), Val << (Offset & 0xF));
}

UINT32
EFIAPI
CruGetClkGate (IN UINT32 Base, IN UINT32 Offset)
{
  UINT32 RegAddr = (Base == PMUCRU_BASE) ? PMUCRU_BASE : CRU_BASE;
  RegAddr += 0x0100 + (Offset * 4);
  return (MmioRead32 (RegAddr) >> (Offset & 0xF)) & 1;
}

VOID
EFIAPI
CruSetSoftReset (IN UINT32 Base, IN UINT32 Offset, IN UINT32 Val)
{
  UINT32 RegAddr = (Base == PMUCRU_BASE) ? PMUCRU_BASE : CRU_BASE;
  RegAddr += 0x0200 + (Offset * 4);
  CruWriteWithMask (RegAddr, 1U << (Offset & 0xF), Val << (Offset & 0xF));
}
