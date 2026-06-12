/** @file
  USB HCD Init Driver for RK3326 (PX30)

  Initializes the USB2 OTG, EHCI, and OHCI controllers on RK3326.
  Configures clocks, resets, USB2 PHY via GRF, and registers
  EHCI + OHCI as non-discoverable devices for the EDK2 USB stack.

  RK3326 has no USB3 (no DWC3), only USB2 IP:
    - DWC2 OTG  @ 0xFF300000
    - EHCI      @ 0xFF340000
    - OHCI      @ 0xFF350000
    - USB2 PHY control via GRF_SOC_CON2/5

  Clock gate IDs from dt-bindings/clock/px30-cru.h:
    HCLK_USB=248, HCLK_HOST=259, HCLK_HOST_ARB=260, HCLK_OTG=258, PCLK_USB_GRF=327

  Soft reset IDs:
    SRST_USB2HOST_EHCI=72, SRST_USB2HOST=73, SRST_USBPHYPOR=74,
    SRST_USBPHY_OTG_PORT=75, SRST_USBPHY_HOST_PORT=76

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/TimerLib.h>
#include <Soc.h>

// ── CRU / GRF helpers ───────────────────────────────────────────────────
//
// PX30 CRU: clkgate_con[] @ CRU_BASE+0x100, softrst_con[] @ CRU_BASE+0x200
//           register index = id/16, bit = id%16
// Rockchip write convention: hi16 = write mask, lo16 = value

#define CRU_WRITE_MASK(val)  (((val) & 0xFFFFU) << 16 | ((val) & 0xFFFFU))

/**
 * Write a CRU clock gate or reset register using the Rockchip hi16-mask
 * convention: bits[31:16] = write-enable mask, bits[15:0] = value.
 */
STATIC
VOID
CruRegWrite (
  IN UINT32  RegAddr,
  IN UINT32  Val,
  IN UINT32  Mask
  )
{
  MmioWrite32 (RegAddr, ((Mask & 0xFFFFU) << 16) | (Val & Mask));
}

STATIC
VOID
CruGateEnable (
  IN UINT32  ClkId,
  IN BOOLEAN Enable
  )
{
  UINT32  Addr = CRU_BASE + 0x100U + ((ClkId) / 16U) * 4U;
  UINT32  Bit  = 1U << ((ClkId) % 16U);
  CruRegWrite (Addr, Enable ? Bit : 0, Bit);
}

STATIC
VOID
CruResetAssert (
  IN UINT32  RstId,
  IN BOOLEAN Assert
  )
{
  //
  // Soft reset registers are at CRU_BASE + 0x200 + (rstId/16)*4
  // Assert = 1 (put in reset), deassert = 0 (release from reset)
  //
  UINT32  Addr = CRU_BASE + 0x200U + ((RstId) / 16U) * 4U;
  UINT32  Bit  = 1U << ((RstId) % 16U);
  CruRegWrite (Addr, Assert ? Bit : 0, Bit);
}

// ── USB clock / reset IDs ──────────────────────────────────────────────

#define HCLK_USB          248
#define HCLK_HOST         259
#define HCLK_HOST_ARB     260
#define HCLK_OTG          258
#define PCLK_USB_GRF      327

#define SRST_USB_NIU_H         65
#define SRST_USB2OTG_H         66
#define SRST_USB2OTG           67
#define SRST_USB2HOST_H        69
#define SRST_USB2HOST_ARB_H    70
#define SRST_USB2HOST_AUX_H    71
#define SRST_USB2HOST_EHCI     72
#define SRST_USB2HOST          73
#define SRST_USBPHYPOR         74
#define SRST_USBPHY_OTG_PORT   75
#define SRST_USBPHY_HOST_PORT  76
#define SRST_USBPHY_GRF        77

// ── USB2 PHY GRF registers ─────────────────────────────────────────────
//
// GRF_SOC_CON2  @ GRF_BASE + 0x408 : USB2 PHY control
// GRF_SOC_CON5  @ GRF_BASE + 0x414 : USB2 PHY status
// USB2PHY_GRF   @ 0xFF2C0000       : alternative PHY register space
//
// Bits in GRF_SOC_CON2:
#define GRF_SOC_CON2          (GRF_BASE + 0x408U)
#define GRF_SOC_CON5          (GRF_BASE + 0x414U)

// USB2OTG PHY control bits (SOC_CON2)
#define OTG_PHY_SUS           BIT4    // suspend
#define OTG_PHY_BVALID        BIT3    // B-valid detect
#define OTG_PHY_IDDIG         BIT2    // ID detect
#define OTG_SEL_PHY_24MHZ     (0U << 0)
#define OTG_DRV_VBUS          BIT5    // drive VBUS

// USB2HOST PHY control bits (SOC_CON2 high part)
#define HOST_PHY_SUS          BIT12   // suspend
#define HOST_SEL_PHY_24MHZ    (0U << 8)

// SOC_CON5: USB status
#define OTG_BVALID_STATUS     BIT2
#define HOST_DM_LEVEL         BIT4
#define HOST_DP_LEVEL         BIT5

// GRF write-enable helpers
#define GRF_WRITE(addr, mask, val) \
  MmioWrite32 ((addr), (((mask) & 0xFFFFU) << 16) | ((val) & (mask)))

//
// USB Controller MMIO bases — already defined in Soc.h:
//   USB_OTG_BASE (0xFF300000), USB_EHCI_BASE (0xFF340000),
//   USB_OHCI_BASE (0xFF350000)
//

// ── USB2 PHY initialization ────────────────────────────────────────────

STATIC
VOID
Usb2PhyInit (
  VOID
  )
{
  UINT32  Reg;

  DEBUG ((DEBUG_INFO, "UsbHcdDxe: Initializing USB2 PHY\n"));

  //
  // 1. Power on USB PHY: deassert USBPHYPOR reset
  //
  CruResetAssert (SRST_USBPHYPOR, FALSE);
  MicroSecondDelay (10);

  //
  // 2. Deassert OTG port PHY reset and host port PHY reset
  //
  CruResetAssert (SRST_USBPHY_OTG_PORT, FALSE);
  CruResetAssert (SRST_USBPHY_HOST_PORT, FALSE);
  MicroSecondDelay (10);

  //
  // 3. Configure USB2 PHY via GRF:
  //    - Enable OTG PHY (clear suspend)
  //    - Enable HOST PHY (clear suspend)
  //    - Select 24MHz reference clock
  //
  Reg = MmioRead32 (GRF_SOC_CON2);

  // Clear suspend bits for both OTG and HOST PHY
  Reg &= ~(OTG_PHY_SUS | HOST_PHY_SUS);

  // Select 24MHz reference for OTG and HOST
  Reg &= ~0x0F0FU;  // Clear low nibbles for both PHY selects
  Reg |= OTG_SEL_PHY_24MHZ | HOST_SEL_PHY_24MHZ;

  GRF_WRITE (GRF_SOC_CON2, 0xFFFFU, Reg);
  MicroSecondDelay (100);

  //
  // 4. Wait for OTG PHY ready (BVALID or line state stable)
  //
  MicroSecondDelay (1000);

  DEBUG ((DEBUG_INFO, "UsbHcdDxe: USB2 PHY initialized (SOC_CON2=0x%08x)\n",
          MmioRead32 (GRF_SOC_CON2)));
}

// ── Driver Entry Point ─────────────────────────────────────────────────

EFI_STATUS
EFIAPI
InitializeUsbHcd (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "UsbHcdDxe: Initializing USB HCD for RK3326\n"));

  //
  // Step 1: Enable USB clock gates
  //
  CruGateEnable (HCLK_USB, TRUE);
  CruGateEnable (HCLK_HOST, TRUE);
  CruGateEnable (HCLK_HOST_ARB, TRUE);
  CruGateEnable (HCLK_OTG, TRUE);
  CruGateEnable (PCLK_USB_GRF, TRUE);
  MicroSecondDelay (10);

  DEBUG ((DEBUG_INFO, "UsbHcdDxe: USB clocks enabled\n"));

  //
  // Step 2: Deassert USB resets
  //
  CruResetAssert (SRST_USB_NIU_H, FALSE);
  CruResetAssert (SRST_USB2OTG_H, FALSE);
  CruResetAssert (SRST_USB2OTG, FALSE);
  CruResetAssert (SRST_USB2HOST_H, FALSE);
  CruResetAssert (SRST_USB2HOST_ARB_H, FALSE);
  CruResetAssert (SRST_USB2HOST_AUX_H, FALSE);
  CruResetAssert (SRST_USB2HOST, FALSE);
  CruResetAssert (SRST_USBPHY_GRF, FALSE);
  MicroSecondDelay (20);

  //
  // Step 2.5: Assert EHCI reset briefly to ensure clean state,
  //           then deassert.
  //
  CruResetAssert (SRST_USB2HOST_EHCI, TRUE);
  MicroSecondDelay (10);
  CruResetAssert (SRST_USB2HOST_EHCI, FALSE);
  MicroSecondDelay (10);

  DEBUG ((DEBUG_INFO, "UsbHcdDxe: USB resets released\n"));

  //
  // Step 3: Initialize USB2 PHY
  //
  Usb2PhyInit ();

  //
  // Step 4: Register EHCI as a non-discoverable device
  //
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeEhci,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,                        // InitFunc
             NULL,                        // Handle (EFI_HANDLE*)
             1,                           // NumMmioResources
             USB_EHCI_BASE,               // MMIO base
             SIZE_256KB                   // MMIO size
             );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbHcdDxe: Failed to register EHCI: %r\n", Status));
  } else {
    DEBUG ((DEBUG_INFO, "UsbHcdDxe: EHCI registered at 0x%08x\n", USB_EHCI_BASE));
  }

  //
  // Step 5: Register OHCI as a non-discoverable device
  //
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeOhci,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,                        // InitFunc
             NULL,                        // Handle (EFI_HANDLE*)
             1,                           // NumMmioResources
             USB_OHCI_BASE,               // MMIO base
             SIZE_256KB                   // MMIO size
             );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "UsbHcdDxe: Failed to register OHCI: %r\n", Status));
  } else {
    DEBUG ((DEBUG_INFO, "UsbHcdDxe: OHCI registered at 0x%08x\n", USB_OHCI_BASE));
  }

  DEBUG ((DEBUG_INFO, "UsbHcdDxe: Initialization complete\n"));
  return EFI_SUCCESS;
}
