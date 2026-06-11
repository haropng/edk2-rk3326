/** @file
 *
 *  Synopsys DesignWare MSHC platform driver for Rockchip eMMC.
 *
 *  Creates a NonDiscoverableDevice for the eMMC controller so the
 *  DwMmcHcDxe host controller driver can bind and produce
 *  EFI_SD_MMC_PASS_THRU_PROTOCOL.
 *
 *  Copyright (c) 2026, RK3326 EDK2 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/RkEmmcPlatformLib.h>

#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/PlatformDwMmc.h>

#define DW_MMC_BASE  FixedPcdGet32(PcdRkEmmcBaseAddress)
#define DW_MMC_SIZE  SIZE_16KB

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  UINT64                      BaseAddress;
  UINT8                       ResourceType;
  EFI_DEVICE_PATH_PROTOCOL    End;
} NON_DISCOVERABLE_DEVICE_PATH;

typedef struct {
  NON_DISCOVERABLE_DEVICE_PATH    DevicePath;
  NON_DISCOVERABLE_DEVICE         Device;
} DW_MMC_DEVICE;
#pragma pack ()

STATIC EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  mDwMmcDeviceDesc[] = {
  {
    ACPI_ADDRESS_SPACE_DESCRIPTOR,                    // Desc
    sizeof (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR) - 3,   // Len
    ACPI_ADDRESS_SPACE_TYPE_MEM,                      // ResType
    0,                                                // GenFlag
    0,                                                // SpecificFlag
    32,                                               // AddrSpaceGranularity
    DW_MMC_BASE,                                      // AddrRangeMin
    DW_MMC_SIZE + DW_MMC_SIZE - 1,                    // AddrRangeMax
    0,                                                // AddrTranslationOffset
    DW_MMC_SIZE                                       // AddrLen
  }
};

STATIC DW_MMC_DEVICE  mDwMmcDevice = {
  {
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_VENDOR_DP,
        {
          (UINT8)(OFFSET_OF (NON_DISCOVERABLE_DEVICE_PATH, End)),
          (UINT8)((OFFSET_OF (NON_DISCOVERABLE_DEVICE_PATH, End)) >> 8)
        }
      },
      EDKII_NON_DISCOVERABLE_DEVICE_PROTOCOL_GUID
    },
    DW_MMC_BASE,
    ACPI_ADDRESS_SPACE_TYPE_MEM,
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        sizeof (EFI_DEVICE_PATH_PROTOCOL),
        0
      }
    }
  },
  {
    &gDwMmcHcNonDiscoverableDeviceGuid,
    NonDiscoverableDeviceDmaTypeNonCoherent,
    NULL,
    mDwMmcDeviceDesc
  }
};

//
// eMMC capabilities: embedded, non-removable, 8-bit bus, high-speed.
//
STATIC DW_MMC_HC_SLOT_CAP  mDwMmcCapability = {
  .HighSpeed   = 1,
  .BusWidth    = 8,
  .SlotType    = EmbeddedSlot,
  .CardType    = EmmcCardType,
  .Voltage30   = 1,
  .Voltage18   = 1,
  .BaseClkFreq = 52000
};

STATIC
EFI_STATUS
EFIAPI
RkEmmcGetCapability (
  IN     EFI_HANDLE          Controller,
  IN     UINT8               Slot,
  OUT    DW_MMC_HC_SLOT_CAP  *Capability
  )
{
  if ((Controller != mDwMmcCapability.Controller) || (Capability == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Capability, &mDwMmcCapability, sizeof (DW_MMC_HC_SLOT_CAP));

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
EFIAPI
RkEmmcCardDetect (
  IN EFI_HANDLE  Controller,
  IN UINT8       Slot
  )
{
  RKEMMC_CARD_PRESENCE_STATE  PresenceState;

  if (Controller != mDwMmcCapability.Controller) {
    return FALSE;
  }

  PresenceState = RkEmmcGetCardPresenceState ();
  if (PresenceState == RkEmmcCardPresenceUnsupported) {
    return TRUE; // let the driver do software detection
  }

  return PresenceState == RkEmmcCardPresent;
}

STATIC PLATFORM_DW_MMC_PROTOCOL  mDwMmcDeviceProtocol = {
  RkEmmcGetCapability,
  RkEmmcCardDetect
};

EFI_STATUS
EFIAPI
RkEmmcDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  RkEmmcSetIoMux ();

  //
  // Do NOT reprogram CLKSEL20_CON — U-Boot already configured the optimal
  // eMMC clock before loading EDK2.  DwMmcHcDxe will handle the per-controller
  // CLKDIV for identification (400KHz) and data transfer speeds.
  //
  // RkEmmcSetClockRate (mDwMmcCapability.BaseClkFreq * 1000);

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mDwMmcCapability.Controller,
                  &gEfiDevicePathProtocolGuid,
                  &mDwMmcDevice.DevicePath,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  &mDwMmcDevice.Device,
                  &gPlatformDwMmcProtocolGuid,
                  &mDwMmcDeviceProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Trigger DXE dispatcher to connect DwMmcHcDxe to this handle.
  //
  gBS->ConnectController (mDwMmcCapability.Controller, NULL, NULL, TRUE);

  return EFI_SUCCESS;
}
