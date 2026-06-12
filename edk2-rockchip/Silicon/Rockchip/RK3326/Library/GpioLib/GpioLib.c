/** @file
  GpioLib for RK3326 (PX30) — full GPIO + IOMUX via GRF/PMUGRF.

  GPIO register layout (same as other Rockchip SoCs):
    SWPORTA_DR  = base + 0x00 (output data)
    SWPORTA_DDR = base + 0x04 (direction: 1=out, 0=in)
    EXT_PORTA   = base + 0x50 (input data)

  IOMUX registers live in the GRF / PMUGRF blocks (see grf_px30.h).
  Each pin's iomux function is 2 bits in a gpio<i><abcd>_iomux register.
  Pull / drive / schmitt are per-sub-bank registers.

  Copyright (c) 2021, Jared McNeill <jmcneill@invisible.ca>
  Copyright (c) 2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/GpioLib.h>
#include <Soc.h>

// ── GPIO controller MMIO helpers ───────────────────────────────────

#define GPIO_BASE(g)   (GPIO0_BASE + ((UINTN)(g) * 0x10000))
  // PX30: GPIO0=0xFF040000, GPIO1=0xFF250000, GPIO2=0xFF260000,
  //       GPIO3=0xFF270000 — not equally spaced!

STATIC
EFI_PHYSICAL_ADDRESS
GpioBase (
  IN UINT8  Group
  )
{
  // PX30 GPIO addresses are not evenly spaced
  switch (Group) {
  case 0: return GPIO0_BASE;  // 0xFF040000
  case 1: return GPIO1_BASE;  // 0xFF250000
  case 2: return GPIO2_BASE;  // 0xFF260000
  case 3: return GPIO3_BASE;  // 0xFF270000
  default: ASSERT (FALSE); return 0;
  }
}

#define GPIO_SWPORT_DR         0x00
#define GPIO_SWPORT_DDR        0x04
#define GPIO_EXT_PORTA         0x50

#define GPIO_WRITE_MASK(p)     (1U << (((p) % 16) + 16))
#define GPIO_VALUE_MASK(p,v)   ((UINT32)(v) << ((p) % 16))
#define GPIO_SWPORT_REG(p,r)   ((r) + (((p) < 16) ? 0x00 : 0x04))

// ── IOMUX register helpers (GRF / PMUGRF) ──────────────────────────
//
// PX30 IOMUX layout (from grf_px30.h / pmugrf header):
//   GPIO0: PMUGRF gpio0{a,b,c}_iomux at 0x00, 0x04, 0x08
//   GPIO1: GRF gpio1{a,b,c,d}{l,h}_iomux at 0x00..0x1C
//   GPIO2: GRF gpio2{a,b,c,d}{l,h}_iomux at 0x20..0x3C
//   GPIO3: GRF gpio3{a,b,c,d}{l,h}_iomux at 0x40..0x5C
//
// Each iomux register controls 4 pins × 2 bits.
// Sub-bank letter: A=0, B=1, C=2, D=3.  Low nibble = pins 0-3, high = 4-7.

STATIC
EFI_PHYSICAL_ADDRESS
GpioIomuxBase (
  IN UINT8  Group
  )
{
  if (Group == 0) {
    return PMUGRF_BASE;   // GPIO0 uses PMUGRF
  }
  return GRF_BASE;        // GPIO1-3 use GRF
}

STATIC
UINT32
GpioIomuxOffset (
  IN UINT8  Group,
  IN UINT8  Pin
  )
{
  UINT32  BankOff;
  UINT32  PinOff;

  if (Group == 0) {
    // PMUGRF: gpio0a_iomux(0x00), gpio0b_iomux(0x04), gpio0c_iomux(0x08)
    return (Pin / 8) * 4;  // 0x00, 0x04, 0x08
  }
  // GRF: gpio<i>{a,b,c,d}{l,h}_iomux
  // Each bank has 8 iomux regs (4 sub-banks × 2 halves) = 0x20 per bank
  BankOff = (Group - 1) * 0x20;       // GPIO1=0x00, GPIO2=0x20, GPIO3=0x40
  PinOff  = ((Pin / 4) % 2) * 4;      // low/high half
  return BankOff + PinOff + ((Pin / 8) * 8);
}

// ── Pull / Drive / Input enable offsets ────────────────────────────
//
// PX30 GRF: after iomux regs (0x60 bytes), the p/sr/smt/e regs start:
//   gpio1a_p   = 0x60, gpio1b_p = 0x64, ...
//   PMUGRF: after gpio0c_iomux (0x08): gpio0a_p = 0x10, gpio0b_p = 0x14, ...

STATIC
EFI_PHYSICAL_ADDRESS
GpioPullBase (
  IN UINT8  Group
  )
{
  if (Group == 0) {
    return PMUGRF_BASE + 0x10;  // gpio0a_p
  }
  return GRF_BASE + 0x60;       // gpio1a_p (after all iomux regs)
}

STATIC
EFI_PHYSICAL_ADDRESS
GpioSlewBase (
  IN UINT8  Group
  )
{
  if (Group == 0) {
    return PMUGRF_BASE + 0x1C;  // gpio0l_sr
  }
  return GRF_BASE + 0x100;       // gpio1a_sr (after all pull regs)
}

STATIC
EFI_PHYSICAL_ADDRESS
GpioSmtBase (
  IN UINT8  Group
  )
{
  if (Group == 0) {
    return PMUGRF_BASE + 0x24;  // gpio0l_smt
  }
  return GRF_BASE + 0x140;      // gpio1a_smt (after all slew regs)
}

STATIC
EFI_PHYSICAL_ADDRESS
GpioDriveBase (
  IN UINT8  Group
  )
{
  if (Group == 0) {
    return PMUGRF_BASE + 0x2C;  // gpio0a_e (after smt regs)
  }
  return GRF_BASE + 0x180;      // gpio1a_e (after all smt regs)
}

// ── Pin register offsets within a sub-bank group ───────────────────
// Each sub-bank (A,B,C,D) has one register for 8 pins.
// pull/slew/smt registers: 2 bits per pin, so 8 pins = 16 bits per register.
// drive registers: 4 bits per pin × 8 pins = 32 bits, one register per sub-bank.

#define SUB_OFF(p)  (((p) / 8) * 4)   // A=0, B=4, C=8, D=12
#define PIN_SHIFT(p) (((p) % 8) * 2)   // 2 bits per pin for pull/smt
#define DRV_SHIFT(p) (((p) % 8) * 4)   // 4 bits per pin for drive

// ── Basic GPIO ─────────────────────────────────────────────────────

VOID
GpioPinSetDirection (
  IN UINT8              Group,
  IN UINT8              Pin,
  IN GPIO_PIN_DIRECTION Direction
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32  Reg;

  if (Pin > 31) return;
  Base = GpioBase (Group);
  Reg  = GPIO_SWPORT_REG (Pin, GPIO_SWPORT_DDR);
  MmioWrite32 (
    Base + Reg,
    GPIO_WRITE_MASK (Pin) | GPIO_VALUE_MASK (Pin, Direction)
    );
}

VOID
GpioPinWrite (
  IN UINT8    Group,
  IN UINT8    Pin,
  IN BOOLEAN  Value
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32  Reg;

  if (Pin > 31) return;
  Base = GpioBase (Group);
  Reg  = GPIO_SWPORT_REG (Pin, GPIO_SWPORT_DR);
  MmioWrite32 (
    Base + Reg,
    GPIO_WRITE_MASK (Pin) | GPIO_VALUE_MASK (Pin, Value)
    );
}

BOOLEAN
GpioPinRead (
  IN UINT8  Group,
  IN UINT8  Pin
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32  Reg;

  if (Pin > 31) return FALSE;
  Base = GpioBase (Group);
  Reg  = GPIO_SWPORT_REG (Pin, GPIO_SWPORT_DR);
  return (MmioRead32 (Base + Reg) & (1U << (Pin % 16))) != 0;
}

BOOLEAN
GpioPinReadActual (
  IN UINT8  Group,
  IN UINT8  Pin
  )
{
  // Read the physical pin level via EXT_PORTA
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32  Reg;

  if (Pin > 31) return FALSE;
  Base = GpioBase (Group);
  Reg  = GPIO_SWPORT_REG (Pin, GPIO_EXT_PORTA);
  return (MmioRead32 (Base + Reg) & (1U << (Pin % 16))) != 0;
}

// ── IOMUX ──────────────────────────────────────────────────────────

VOID
GpioPinSetFunction (
  IN UINT8  Group,
  IN UINT8  Pin,
  IN UINT8  Function
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32                Off;
  UINT32                Shift;
  UINT32                Value;

  if (Pin > 31) return;

  Base  = GpioIomuxBase (Group);
  Off   = GpioIomuxOffset (Group, Pin);

  // Within each register, 4 consecutive pins × 2 bits each.
  // Low half = pins 0-3, High half = pins 4-7.
  Shift = ((Pin % 4) * 2) + (((Pin % 8) >= 4) ? 16 : 0);

  // Rockchip write convention: hi16 = write mask, lo16 = value.
  Value = (0x3U << (Shift + 16)) | ((UINT32)Function << Shift);

  MmioWrite32 (Base + Off, Value);
}

VOID
GpioPinSetPull (
  IN UINT8         Group,
  IN UINT8         Pin,
  IN GPIO_PIN_PULL Pull
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32                Shift;
  UINT32                Value;

  if (Pin > 31) return;

  Base  = GpioPullBase (Group) + SUB_OFF (Pin);
  Shift = PIN_SHIFT (Pin);

  Value = (0x3U << (Shift + 16)) | ((UINT32)Pull << Shift);
  MmioWrite32 (Base, Value);
}

VOID
GpioPinSetDrive (
  IN UINT8          Group,
  IN UINT8          Pin,
  IN GPIO_PIN_DRIVE Drive
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32                Shift;
  UINT32                Value;

  if (Pin > 31) return;
  if (Drive == GPIO_PIN_DRIVE_DEFAULT) return;

  Base  = GpioDriveBase (Group) + SUB_OFF (Pin);
  Shift = DRV_SHIFT (Pin);

  // Clean value to 3 bits for PX30 (4 possible drive levels * 2mA)
  Value = (0x7U << (Shift + 16)) | (((UINT32)Drive & 0x7) << Shift);
  MmioWrite32 (Base, Value);
}

VOID
GpioPinSetInput (
  IN UINT8                 Group,
  IN UINT8                 Pin,
  IN GPIO_PIN_INPUT_ENABLE InputEnable
  )
{
  EFI_PHYSICAL_ADDRESS  Base;
  UINT32                Shift;
  UINT32                Value;

  if (Pin > 31) return;
  if (InputEnable == GPIO_PIN_INPUT_DEFAULT) return;

  Base  = GpioSmtBase (Group) + SUB_OFF (Pin);
  Shift = PIN_SHIFT (Pin);

  Value = (0x3U << (Shift + 16)) | ((UINT32)InputEnable << Shift);
  MmioWrite32 (Base, Value);
}

// ── Batch IOMUX ────────────────────────────────────────────────────

VOID
GpioSetIomuxConfig (
  IN CONST GPIO_IOMUX_CONFIG  *Configs,
  IN UINT32                    NumConfigs
  )
{
  UINT32  Index;

  for (Index = 0; Index < NumConfigs; Index++) {
    CONST GPIO_IOMUX_CONFIG  *Mux = &Configs[Index];
    DEBUG ((DEBUG_INFO, "GPIO: IOMUX '%a' G%d P%d F%d\n",
            Mux->Name, Mux->Group, Mux->Pin, Mux->Function));
    GpioPinSetFunction (Mux->Group, Mux->Pin, Mux->Function);
    GpioPinSetPull (Mux->Group, Mux->Pin, Mux->Pull);
    if (Mux->Drive != GPIO_PIN_DRIVE_DEFAULT) {
      GpioPinSetDrive (Mux->Group, Mux->Pin, Mux->Drive);
    }
  }
}
