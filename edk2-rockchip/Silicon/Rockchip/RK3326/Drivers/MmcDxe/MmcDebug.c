/** @file
  MMC debugging helpers — print CID/CSD/OCR/RCA info

  Copyright (c) 2011-2013, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Mmc.h"

CONST CHAR8 *mStrUnit[] = {
  "100kbit/s", "1Mbit/s", "10Mbit/s", "100Mbit/s",
  "Unknown", "Unknown", "Unknown", "Unknown"
};
CONST CHAR8 *mStrValue[] = {
  "1.0", "1.2", "1.3", "1.5", "2.0", "2.5", "3.0", "3.5",
  "4.0", "4.5", "5.0", "Unknown", "Unknown", "Unknown", "Unknown"
};

VOID
PrintCID (
  IN UINT32 *Cid
  )
{
  DEBUG ((DEBUG_INFO, "  CID: MID=0x%02X OID=%c%c PNM=%c%c%c%c%c%c PRV=%d.%d PSN=0x%08X MDT=%d/%d\n",
    Cid[0] & 0xFF,
    (Cid[3] >> 8) & 0xFF, (Cid[3] >> 16) & 0xFF,
    (Cid[2] >> 24) & 0xFF, (Cid[2] >> 16) & 0xFF, (Cid[2] >> 8) & 0xFF,
    Cid[2] & 0xFF, (Cid[1] >> 24) & 0xFF, (Cid[1] >> 16) & 0xFF,
    Cid[1] >> 24,
    Cid[1] & 0xFFFFFF,
    (Cid[0] >> 8) & 0xF, (Cid[0] >> 12) & 0xFF));
}

VOID
PrintCSD (
  IN UINT32 *Csd
  )
{
  UINTN Value;

  DEBUG ((DEBUG_INFO, "  CSD: ver=%d, speed=%a %a, read_bl=%d, write_bl=%d\n",
    (Csd[2] >> 30) & 3,
    mStrValue[((Csd[0] & 0xFF) >> 3) & 0xF],
    mStrUnit[Csd[0] & 7],
    2 << (((Csd[1] >> 16) & 0xF) - 1),
    2 << (((Csd[0] >> 22) & 0xF) - 1)));

  if (!((Csd[0] >> 10) & 1)) {
    Value = (Csd[3] >> 10) & 3;
    if (Value == 0) {
      DEBUG ((DEBUG_INFO, "  Format: HDD with partition table\n"));
    } else if (Value == 1) {
      DEBUG ((DEBUG_INFO, "  Format: DOS FAT with boot sector\n"));
    } else {
      DEBUG ((DEBUG_INFO, "  Format: Universal/Unknown\n"));
    }
  }
}

VOID
PrintRCA (
  IN UINT32 Rca
  )
{
  DEBUG ((DEBUG_INFO, "  RCA: 0x%04X\n", (Rca >> 16) & 0xFFFF));
}

VOID
PrintOCR (
  IN UINT32 Ocr
  )
{
  UINTN MinV = 36;
  UINTN MaxV = 20;
  UINTN Loop;

  for (Loop = 8; Loop < 24; Loop++) {
    if (Ocr & (1 << Loop)) {
      if (MinV > (Loop - 7 + 20)) {
        MinV = Loop - 7 + 20;
      }
      if (MaxV < (Loop - 7 + 20)) {
        MaxV = Loop - 7 + 20;
      }
    }
  }
  DEBUG ((DEBUG_INFO, "  OCR: %d.%d - %d.%d V, Access=%a, PowerUp=%a\n",
    MinV / 10, MinV % 10, MaxV / 10, MaxV % 10,
    ((Ocr >> 29) & 2) ? "Sector" : "Byte",
    (Ocr & BIT31) ? "Yes" : "No"));
}

VOID
PrintResponseR1 (
  IN UINT32 R1
  )
{
  DEBUG ((DEBUG_INFO, "  R1=0x%08X: State=%d, Ready=%a\n",
    R1, (R1 >> 9) & 0xF,
    (R1 & MMC_R0_READY_FOR_DATA) ? "Yes" : "No"));
}
