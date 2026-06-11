/** @file
  GpioLib for RK3326 — GPIO direction, read, write.

  Rockchip GPIO register layout (32-bit MMIO):
    SWPORTA_DR  = base + 0x00 (output data)
    SWPORTA_DDR = base + 0x04 (direction: 1=output, 0=input)
    EXT_PORTA   = base + 0x50 (input data)

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

#define GPIO_SWPORTA_DR    0x00
#define GPIO_SWPORTA_DDR   0x04
#define GPIO_EXT_PORTA     0x50

VOID
EFIAPI
GpioPinSetDirection (IN UINT32 Bank, IN UINT32 Pin, IN UINT32 Dir)
{
  UINT32 Val;
  if (Pin > 31) return;
  Val = MmioRead32 (Bank + GPIO_SWPORTA_DDR);
  Val = Dir ? (Val | (1U << Pin)) : (Val & ~(1U << Pin));
  MmioWrite32 (Bank + GPIO_SWPORTA_DDR, Val);
}

VOID
EFIAPI
GpioPinWrite (IN UINT32 Bank, IN UINT32 Pin, IN UINT32 Val)
{
  UINT32 D;
  if (Pin > 31) return;
  D = MmioRead32 (Bank + GPIO_SWPORTA_DR);
  D = Val ? (D | (1U << Pin)) : (D & ~(1U << Pin));
  MmioWrite32 (Bank + GPIO_SWPORTA_DR, D);
}

UINT32
EFIAPI
GpioPinRead (IN UINT32 Bank, IN UINT32 Pin)
{
  if (Pin > 31) return 0;
  return (MmioRead32 (Bank + GPIO_EXT_PORTA) >> Pin) & 1;
}
