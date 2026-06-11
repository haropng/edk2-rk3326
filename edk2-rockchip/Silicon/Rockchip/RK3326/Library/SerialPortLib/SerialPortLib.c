/** @file
  SerialPortLib for RK3326 — thin wrapper around DW APB UART HAL.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/SerialPortLib.h>
#include "UartLib.h"
#include <Soc.h>

RETURN_STATUS
EFIAPI
SerialPortInitialize (VOID)
{
  // UART already configured by U-Boot at 1.5Mbaud 8N1. Don't touch.
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  return UartWrite (UART2_BASE, Buffer, NumberOfBytes);
}

UINTN
EFIAPI
SerialPortRead (OUT UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  return UartRead (UART2_BASE, Buffer, NumberOfBytes);
}

BOOLEAN
EFIAPI
SerialPortPoll (VOID)
{
  return UartPoll (UART2_BASE);
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (IN UINT32 Control)
{
  return UartSetControl (UART2_BASE, Control);
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (OUT UINT32 *Control)
{
  return UartGetControl (UART2_BASE, Control);
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Control,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  // Don't reset UART — U-Boot already configured correctly
  return RETURN_SUCCESS;
}
