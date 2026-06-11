/** @file
  OtpLib for RK3326 — OTP chip version read.

  OTP base: nvmem@ff290000

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Soc.h>

UINT32
EFIAPI
OtpRead32 (IN UINTN Offset)
{
  return MmioRead32 (OTP_BASE + Offset);
}

UINT32
EFIAPI
OtpGetCpuChipVersion (VOID)
{
  return (OtpRead32 (0x00) >> 8) & 0xFF;
}

UINT32
EFIAPI
OtpGetCpuVersion (VOID)
{
  return OtpGetCpuChipVersion ();
}
