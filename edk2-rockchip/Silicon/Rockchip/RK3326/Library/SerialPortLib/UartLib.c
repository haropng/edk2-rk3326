/** @file
  DW APB UART HAL for RK3326 — register-level driver.

  The DW APB UART is the standard Rockchip debug UART IP.
  RK3326 uses UART2 @ 0xFF160000, 115200 8N1, 24MHz clock.

  Copyright (c) 2017, Rockchip Inc. All rights reserved.
  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include "UartLib.h"

/* DW APB UART register offsets (32-bit MMIO, reg-shift=2) */
#define UART_RBR  0x00
#define UART_THR  0x00
#define UART_DLL  0x00
#define UART_DLH  0x04
#define UART_IER  0x04
#define UART_FCR  0x08
#define UART_LCR  0x0C
#define UART_MCR  0x10
#define UART_LSR  0x14
#define UART_USR  0x7C
#define UART_SRR  0x88
#define UART_SFE  0x90
#define UART_SRT  0x94
#define UART_STET 0x98

#define LSR_TX_EMPTY  (1 << 5)
#define LSR_RX_READY  (1 << 0)
#define USR_BUSY      (1 << 0)
#define USR_TX_FULL   (1 << 1)

RETURN_STATUS
EFIAPI
UartInitializePort (
  IN     UINTN               UartBase,
  IN     UINT32              UartClkInHz,
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  UINT32 Divisor;
  UINT32 LcrValue;

  // Reset UART
  MmioWrite32 (UartBase + UART_SRR, 0);

  // Wait for reset to complete
  while (MmioRead32 (UartBase + UART_USR) & USR_BUSY);

  // 8N1, DLAB=0
  LcrValue = 0x03;
  MmioWrite32 (UartBase + UART_LCR, LcrValue);

  // Enable FIFO
  MmioWrite32 (UartBase + UART_FCR, 0x01);

  // Baud rate = UartClkInHz / (16 * BaudRate)
  Divisor = (UartClkInHz / 16) / (UINT32)*BaudRate;

  // Set DLAB=1 to access DLL/DLH
  MmioWrite32 (UartBase + UART_LCR, LcrValue | (1 << 7));
  MmioWrite32 (UartBase + UART_DLL, Divisor & 0xFF);
  MmioWrite32 (UartBase + UART_DLH, (Divisor >> 8) & 0xFF);
  // Restore DLAB=0
  MmioWrite32 (UartBase + UART_LCR, LcrValue);

  *ReceiveFifoDepth = 32;
  *Parity    = 0;
  *DataBits  = 8;
  *StopBits  = 0;

  DEBUG ((DEBUG_INFO, "UartInit: base=0x%llx baud=%lld divisor=%d\n",
          (UINT64)UartBase, *BaudRate, Divisor));

  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
UartSetAttributes (
  IN     UINTN               UartBase,
  IN     UINT32              UartClkInHz,
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return UartInitializePort (UartBase, UartClkInHz, BaudRate,
         ReceiveFifoDepth, Parity, DataBits, StopBits);
}

UINTN
EFIAPI
UartWrite (IN UINTN UartBase, IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINTN Count;
  for (Count = 0; Count < NumberOfBytes; Count++) {
    // Wait while TX FIFO is full (TFNF=0). USR[1]=0 → full, wait.
    while (!(MmioRead32 (UartBase + UART_USR) & USR_TX_FULL));
    MmioWrite32 (UartBase + UART_THR, Buffer[Count]);
  }
  return Count;
}

UINTN
EFIAPI
UartRead (IN UINTN UartBase, OUT UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINTN Count;
  for (Count = 0; Count < NumberOfBytes; Count++) {
    while ((MmioRead32 (UartBase + UART_LSR) & LSR_RX_READY) == 0);
    Buffer[Count] = MmioRead32 (UartBase + UART_RBR) & 0xFF;
  }
  return Count;
}

BOOLEAN
EFIAPI
UartPoll (IN UINTN UartBase)
{
  return (MmioRead32 (UartBase + UART_LSR) & LSR_RX_READY) != 0;
}

RETURN_STATUS
EFIAPI
UartSetControl (IN UINTN UartBase, IN UINT32 Control)
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
UartGetControl (IN UINTN UartBase, OUT UINT32 *Control)
{
  *Control = 0;
  return RETURN_SUCCESS;
}
