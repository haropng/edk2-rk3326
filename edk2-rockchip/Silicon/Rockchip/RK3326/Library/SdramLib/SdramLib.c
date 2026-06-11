/** @file
  SdramLib for RK3326 — DRAM size detection via DMC registers.

  Probes the DMC (DDR Memory Controller) at 0xFF2A0000 to decode
  DDR geometry (rows, columns, banks, bus width) and determine
  total installed DRAM.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/SdramLib.h>
#include <Soc.h>

// DMC registers (simplified — match RK3326 TRM)
#define DMC_REG_BASE(base, n)  ((base) + 0x0 + (n) * 0x20)

UINT64
EFIAPI
SdramGetSize (VOID)
{
  UINT32 DmcReg;
  UINT64 TotalMb = 0;
  UINTN  Ch;

  for (Ch = 0; Ch < 2; Ch++) {
    DmcReg = MmioRead32 (DMC_REG_BASE (DMC_BASE, Ch));

    if (DmcReg == 0 || DmcReg == 0xFFFFFFFF) {
      // Channel not populated or DMC firewalled
      continue;
    }

    // Try to decode from DMC status register
    // On RK3326, the DMC status encodes:
    //   bits[3:0]   = column bits (typically 9-12)
    //   bits[7:4]   = row bits (typically 13-16)
    //   bits[11:8]  = bank bits (typically 2-3)
    //   bits[13:12] = bus width (0=16bit, 1=32bit, 2=64bit)
    UINT32 ColBits  = (DmcReg & 0xF) + 7;           // columns = encoded + 7
    UINT32 RowBits  = ((DmcReg >> 4) & 0xF) + 10;   // rows = encoded + 10
    UINT32 BankBits = ((DmcReg >> 8) & 0x3) + 2;    // banks = encoded + 2
    UINT32 BusWidth = ((DmcReg >> 12) & 0x3);
    UINT32 ChMb;

    // Cap to realistic values for LPDDR3 on RK3326
    if (ColBits  > 12) ColBits = 12;
    if (RowBits  > 16) RowBits = 16;
    if (BankBits > 3)  BankBits = 3;

    // Capacity = 2^(row+col+bank) * bus_width/8 bytes
    ChMb = (1ULL << (RowBits + ColBits + BankBits - 20)) * (1U << BusWidth);

    DEBUG ((DEBUG_INFO, "Sdram: Ch%d: rows=%d cols=%d banks=%d bw=%d -> %d MB\n",
            Ch, RowBits, ColBits, BankBits, 8 << BusWidth, ChMb));

    TotalMb += ChMb;
  }

  if (TotalMb == 0) {
    // Fallback to PCD size if DMC probe failed (e.g., firewalled by BL31)
    TotalMb = FixedPcdGet64 (PcdSystemMemorySize) >> 20;
    DEBUG ((DEBUG_WARN, "Sdram: DMC probe failed, using PCD default: %lld MB\n", TotalMb));
  }

  DEBUG ((DEBUG_INFO, "Sdram: Total detected: %lld MB (%lld bytes)\n",
          TotalMb, TotalMb << 20));

  return TotalMb << 20;
}
