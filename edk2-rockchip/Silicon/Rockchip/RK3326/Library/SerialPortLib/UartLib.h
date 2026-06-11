/** @file
  DW APB UART Library API for RK3326.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __RK3326_UART_LIB_H__
#define __RK3326_UART_LIB_H__

#include <Uefi/UefiBaseType.h>
#include <Protocol/SerialIo.h>

RETURN_STATUS EFIAPI UartInitializePort (
  IN     UINTN UartBase, IN UINT32 UartClkInHz,
  IN OUT UINT64 *BaudRate, IN OUT UINT32 *ReceiveFifoDepth,
  IN OUT EFI_PARITY_TYPE *Parity, IN OUT UINT8 *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits);

RETURN_STATUS EFIAPI UartSetAttributes (
  IN     UINTN UartBase, IN UINT32 UartClkInHz,
  IN OUT UINT64 *BaudRate, IN OUT UINT32 *ReceiveFifoDepth,
  IN OUT EFI_PARITY_TYPE *Parity, IN OUT UINT8 *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits);

UINTN  EFIAPI UartWrite (IN UINTN UartBase, IN UINT8 *Buffer, IN UINTN NumberOfBytes);
UINTN  EFIAPI UartRead  (IN UINTN UartBase, OUT UINT8 *Buffer, IN UINTN NumberOfBytes);
BOOLEAN EFIAPI UartPoll  (IN UINTN UartBase);
RETURN_STATUS EFIAPI UartSetControl (IN UINTN UartBase, IN UINT32 Control);
RETURN_STATUS EFIAPI UartGetControl (IN UINTN UartBase, OUT UINT32 *Control);

#endif /* __RK3326_UART_LIB_H__ */
