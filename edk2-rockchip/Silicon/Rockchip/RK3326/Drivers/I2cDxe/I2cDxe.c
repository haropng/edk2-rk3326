/** @file
  I2C Host Driver for RK3326 (PX30)

  Minimal polled-mode I2C driver for the Rockchip I2C controller (DW IP).
  Supports I2C0 (PMIC bus, RK817 @ 0x20) with basic read/write operations.

  The Rockchip I2C controller registers (v1, PX30 = RK3288 compatible):
    Offset 0x00: CON        — control (start/stop/ack/irq/function select)
    Offset 0x04: CLKDIV     — clock divider (SCL = pclk / (8 * (divl+1 + divh+1)))
    Offset 0x08: MRXADDR    — slave address (7-bit, bit 0 = 0)
    Offset 0x0C: MRXRADDR   — slave address for RX mode
    Offset 0x10: MTXCNT     — TX data count
    Offset 0x14: MRXCNT     — RX data count
    Offset 0x18: IEN        — interrupt enable
    Offset 0x1C: IPD        — interrupt pending (write-1-to-clear)
    Offset 0x20: FCNT       — finished count
    Offset 0x100+: TXDATA   — TX FIFO (8 × 32-bit)
    Offset 0x200+: RXDATA   — RX FIFO (8 × 32-bit)

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Soc.h>

// ── I2C Controller Register Offsets ──────────────────────────────────

#define I2C_CON             0x000U
#define I2C_CLKDIV          0x004U
#define I2C_MRXADDR         0x008U
#define I2C_MRXRADDR        0x00CU
#define I2C_MTXCNT          0x010U
#define I2C_MRXCNT          0x014U
#define I2C_IEN             0x018U
#define I2C_IPD             0x01CU
#define I2C_FCNT            0x020U
#define I2C_TXDATA_BASE     0x100U
#define I2C_RXDATA_BASE     0x200U

// Control register bits
#define I2C_CON_EN           BIT0    // I2C enable
#define I2C_CON_MOD(M)       ((M) << 1) // mode: 0=TX, 1=TRX, 2=RX, 3=RXTX
#define I2C_CON_MODE_TX      0x00U
#define I2C_CON_MODE_TRX     0x01U
#define I2C_CON_MODE_RX      0x02U
#define I2C_CON_MODE_RXTX    0x03U
#define I2C_CON_START         BIT3    // issue START condition
#define I2C_CON_STOP          BIT4    // issue STOP condition
#define I2C_CON_ACK           BIT5    // ACK response
#define I2C_CON_NAK           BIT6    // NAK response
#define I2C_CON_START_SHIFT   3

// Interrupt register bits
#define I2C_IPD_ALL           0x7FU    // mask all interrupt bits

// Timing: SCL = pclk_i2c / (8 * (divl + 1 + divh + 1))
// For pclk=100MHz and SCL=100kHz: div≈125, so divl=62, divh=62
#define I2C_DIVL_SHIFT        0
#define I2C_DIVH_SHIFT        8

// Maximum wait for I2C operations (microseconds)
#define I2C_TIMEOUT_US        100000U

// ── I2C Bus Info ────────────────────────────────────────────────────
//
// RK3326 has 4 I2C controllers:
//   I2C0: 0xFF180000, I2C1: 0xFF190000, I2C2: 0xFF1A0000, I2C3: 0xFF1B0000
// I2C0 hosts the RK817 PMIC at address 0x20.
//
STATIC CONST UINT32  mI2cBases[] = {
  I2C0_BASE, I2C1_BASE, I2C2_BASE, I2C3_BASE
};

// ── Helper: Divider setup ────────────────────────────────────────────

/**
  Configure I2C clock divider for ~100 kHz SCL.

  Assumes pclk_i2c = 100 MHz (PCLK_BUS).
  SCL = pclk / (8 * (divl + 1 + divh + 1))
  For 100 kHz: div ≈ 125, we split into div_low = 40, div_high = 84.
**/
STATIC
VOID
I2cSetClkDiv (
  IN UINT32  Base
  )
{
  UINT32  DivL = 40U;    // low period
  UINT32  DivH = 84U;    // high period (total ~125 = ~100kHz)
  MmioWrite32 (Base + I2C_CLKDIV, (DivH << I2C_DIVH_SHIFT) | (DivL << I2C_DIVL_SHIFT));
}

// ── Helper: Wait for I2C idle / completion ───────────────────────────

STATIC
EFI_STATUS
I2cWaitComplete (
  IN UINT32  Base
  )
{
  UINT32  Timeout = I2C_TIMEOUT_US;

  //
  // Poll IPD: when any interrupt bit is set, the transfer is complete.
  //
  while ((MmioRead32 (Base + I2C_IPD) & I2C_IPD_ALL) == 0) {
    if (Timeout == 0) {
      DEBUG ((DEBUG_ERROR, "I2c: timeout waiting for completion (IPD=0x%08x)\n",
              MmioRead32 (Base + I2C_IPD)));
      return EFI_TIMEOUT;
    }
    MicroSecondDelay (10);
    Timeout -= 10;
  }
  return EFI_SUCCESS;
}

// ── Helper: Clear interrupts ────────────────────────────────────────

STATIC
VOID
I2cClearInt (
  IN UINT32  Base
  )
{
  MmioWrite32 (Base + I2C_IPD, I2C_IPD_ALL);
}

// ── Public I2C API ─────────────────────────────────────────────────

/**
  Write data to an I2C device.

  @param  Bus            I2C bus index (0-3)
  @param  SlaveAddr      7-bit slave address
  @param  RegAddr        Register address (0 if not used)
  @param  RegAddrBytes   Number of register address bytes (0, 1, or 2)
  @param  Data           Data buffer to write
  @param  DataLen        Length of data to write

  @retval EFI_SUCCESS    Write succeeded
  @retval EFI_TIMEOUT    I2C bus timeout
**/
EFI_STATUS
I2cWrite (
  IN UINT8   Bus,
  IN UINT8   SlaveAddr,
  IN UINT16  RegAddr,
  IN UINT8   RegAddrBytes,
  IN UINT8   *Data,
  IN UINTN   DataLen
  )
{
  UINT32  Base;
  UINT32  TotalLen;
  UINTN   i;

  if (Bus >= (sizeof (mI2cBases) / sizeof (mI2cBases[0]))) {
    return EFI_INVALID_PARAMETER;
  }
  Base = mI2cBases[Bus];

  TotalLen = RegAddrBytes + DataLen;
  if (TotalLen == 0) {
    return EFI_INVALID_PARAMETER;
  }
  if (TotalLen > 8) {  // TX FIFO depth = 8
    DEBUG ((DEBUG_ERROR, "I2c: TX too long (%u > 8)\n", TotalLen));
    return EFI_BAD_BUFFER_SIZE;
  }

  // 1. Set slave address
  MmioWrite32 (Base + I2C_MRXADDR, SlaveAddr << 1);  // 7-bit addr << 1

  // 2. Write TX FIFO data (register address + data)
  i = 0;

  // Register address bytes (MSB first for multi-byte registers)
  if (RegAddrBytes >= 2) {
    MmioWrite32 (Base + I2C_TXDATA_BASE + (i * 4), (RegAddr >> 8) & 0xFFU);
    i++;
  }
  if (RegAddrBytes >= 1) {
    MmioWrite32 (Base + I2C_TXDATA_BASE + (i * 4), RegAddr & 0xFFU);
    i++;
  }

  // Data bytes
  for (; i < TotalLen; i++) {
    MmioWrite32 (Base + I2C_TXDATA_BASE + (i * 4), Data[i - RegAddrBytes]);
  }

  // 3. Set TX count
  MmioWrite32 (Base + I2C_MTXCNT, TotalLen);

  // 4. Clear interrupts
  I2cClearInt (Base);

  // 5. Start transfer: enable + MODE_TX + START + STOP
  MmioWrite32 (Base + I2C_CON,
               I2C_CON_EN | I2C_CON_MODE_TX | I2C_CON_START | I2C_CON_STOP);

  // 6. Wait for completion
  return I2cWaitComplete (Base);
}

/**
  Read data from an I2C device.

  @param  Bus            I2C bus index (0-3)
  @param  SlaveAddr      7-bit slave address
  @param  RegAddr        Register address (0 if not used)
  @param  RegAddrBytes   Number of register address bytes (0, 1, or 2)
  @param  Data           Data buffer to read into
  @param  DataLen        Length of data to read

  @retval EFI_SUCCESS    Read succeeded
  @retval EFI_TIMEOUT    I2C bus timeout
**/
EFI_STATUS
I2cRead (
  IN UINT8   Bus,
  IN UINT8   SlaveAddr,
  IN UINT16  RegAddr,
  IN UINT8   RegAddrBytes,
  OUT UINT8  *Data,
  IN UINTN   DataLen
  )
{
  UINT32  Base;
  UINTN   i;

  if (Bus >= (sizeof (mI2cBases) / sizeof (mI2cBases[0]))) {
    return EFI_INVALID_PARAMETER;
  }
  Base = mI2cBases[Bus];

  if (DataLen == 0 || DataLen > 8) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // 1. Set slave address
  MmioWrite32 (Base + I2C_MRXADDR, SlaveAddr << 1);

  // 2. If register address is used:
  //    1) TX mode: send register address
  //    2) Then RX mode: read data
  if (RegAddrBytes > 0) {
    // Write register address in TX mode
    UINT32  TxCount = RegAddrBytes;
    for (i = 0; i < TxCount; i++) {
      MmioWrite32 (Base + I2C_TXDATA_BASE + (i * 4),
                   (RegAddr >> ((RegAddrBytes - 1 - i) * 8)) & 0xFFU);
    }
    MmioWrite32 (Base + I2C_MTXCNT, TxCount);
    I2cClearInt (Base);

    // Start TX (without STOP, so we can restart as RX)
    MmioWrite32 (Base + I2C_CON, I2C_CON_EN | I2C_CON_MODE_TX | I2C_CON_START);

    if (EFI_ERROR (I2cWaitComplete (Base))) {
      return EFI_TIMEOUT;
    }
  }

  // 3. Read data in RX mode with STOP
  MmioWrite32 (Base + I2C_MRXRADDR, SlaveAddr << 1);  // same addr for RX
  MmioWrite32 (Base + I2C_MRXCNT, DataLen);
  I2cClearInt (Base);

  // ReSTART + RX + NAK (last byte) + STOP
  MmioWrite32 (Base + I2C_CON,
               I2C_CON_EN | I2C_CON_MODE_RX | I2C_CON_START | I2C_CON_STOP | I2C_CON_NAK);

  if (EFI_ERROR (I2cWaitComplete (Base))) {
    return EFI_TIMEOUT;
  }

  // 4. Read RX FIFO
  for (i = 0; i < DataLen; i++) {
    Data[i] = (UINT8)(MmioRead32 (Base + I2C_RXDATA_BASE + (i * 4)) & 0xFFU);
  }

  return EFI_SUCCESS;
}

// ── Driver Initialization ────────────────────────────────────────────

/**
  Initialize I2C controllers. Configures I2C0 @ 100kHz for PMIC access.
**/
EFI_STATUS
EFIAPI
InitializeI2c (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "I2cDxe: Initializing I2C controllers\n"));
  DEBUG ((DEBUG_INFO, "  I2C0: 0x%08x (PMIC bus)\n", I2C0_BASE));
  DEBUG ((DEBUG_INFO, "  I2C1: 0x%08x\n", I2C1_BASE));
  DEBUG ((DEBUG_INFO, "  I2C2: 0x%08x\n", I2C2_BASE));
  DEBUG ((DEBUG_INFO, "  I2C3: 0x%08x\n", I2C3_BASE));

  //
  // Configure I2C0 for 100 kHz (needed for RK817 PMIC at 0x20)
  //
  I2cSetClkDiv (I2C0_BASE);

  //
  // Disable all interrupts (polled mode)
  //
  MmioWrite32 (I2C0_BASE + I2C_IEN, 0);

  //
  // Enable I2C0 controller
  //
  MmioWrite32 (I2C0_BASE + I2C_CON, I2C_CON_EN);
  MicroSecondDelay (100);

  DEBUG ((DEBUG_INFO, "I2cDxe: I2C0 initialized\n"));
  return EFI_SUCCESS;
}
