/** @file
  RK3326 ACPI Platform Driver — ARM ACPI tables: FADT, MADT, GTDT, DSDT.
  Copyright (c) 2024-2026, RK3326 EDK2 Port. SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <IndustryStandard/Acpi.h>
#include <Protocol/AcpiTable.h>

#include <Protocol/AcpiSystemDescriptionTable.h>

#define GICD_BASE   0xFF131000ULL
#define GICC_BASE   0xFF132000ULL
#define TIMER_GSIV  30
#define UART2_BASE  0xFF160000ULL

// ── DSDT (must be first — FADT references it) ─────────────────────

#define DSDT_LEN 70
STATIC UINT8 mDsdt[DSDT_LEN] = {
  0x44,0x53,0x44,0x54, DSDT_LEN&0xFF,(DSDT_LEN>>8)&0xFF,(DSDT_LEN>>16)&0xFF,(DSDT_LEN>>24)&0xFF,
  0x02,0x00, // Revision=2, Checksum=0 (calculated at runtime)
  'R','K','3','3','2','6','R','K','3','3','2','6',' ',' ',
  0x01,0x00,0x00,0x00,'E','D','K','2',0x01,0x00,0x00,0x00,
  // Scope(\_SB_) { Device(CPU0) { Name(_HID,"ACPI0007"), Name(_UID,0) } }
  0x10,0x0B,0x5F,0x53,0x42,0x5F,
  0x5B,0x83,0x0B,0x43,0x50,0x55,0x30,
  0x08,0x5F,0x48,0x49,0x44,0x0D,'A','C','P','I','0','0','0','7',0x00,
  0x08,0x5F,0x55,0x49,0x44,0x00,
};

// ── Checksum helper ─────────────────────────────────────────────────

STATIC
UINT8
AcpiChecksum (
  IN UINT8   *Data,
  IN UINT32   Length
  )
{
  UINT8  Sum = 0;
  UINT32 i;
  for (i = 0; i < Length; i++) {
    Sum += Data[i];
  }
  return Sum;
}

// ── FADT (static designated initializer) ───────────────────────────

STATIC EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE mFadt = {
  .Header = {
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
    sizeof (EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE),
    EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_REVISION,
    0,                                    // Checksum
    {'R','K','3','3','2','6'},           // OemId[6]
    1,                                    // OemTableId
    0,                                    // OemRevision
    0x544B525F,                           // CreatorId = '_RKT'
    0x01000013                            // CreatorRevision
  },
  .Flags         = EFI_ACPI_6_4_HW_REDUCED_ACPI,
  .ArmBootArch   = EFI_ACPI_6_4_ARM_PSCI_COMPLIANT |
                   EFI_ACPI_6_4_ARM_PSCI_USE_HVC,
  .MinorVersion  = EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_MINOR_REVISION,
  .XDsdt         = (UINT64)(UINTN)mDsdt,
};

// ── GTDT ────────────────────────────────────────────────────────────

STATIC EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE mGtdt = {
  { EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE,
    sizeof(EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE),
    EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_REVISION,
    0,{'R','K','3','3','2','6'},0,1,0,0 },
  0,0, 0,0, TIMER_GSIV,0, 0,0, 0,0
};

// ── MADT ────────────────────────────────────────────────────────────

#pragma pack(1)
typedef struct {
  EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER  Hdr;
  EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE               Gicd;
  EFI_ACPI_6_4_GIC_STRUCTURE                           Gicc;
} MADT;
#pragma pack()

STATIC MADT mMadt = {
  { { EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
      sizeof(MADT), EFI_ACPI_6_4_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
      0,{'R','K','3','3','2','6'},0,1,0,0 }, 0, 0 },
  { EFI_ACPI_6_4_GICD, sizeof(EFI_ACPI_6_4_GIC_DISTRIBUTOR_STRUCTURE),
    0,0,GICD_BASE,0,EFI_ACPI_6_4_GIC_V2 },
  { EFI_ACPI_6_4_GIC, sizeof(EFI_ACPI_6_4_GIC_STRUCTURE),
    0,0,0,EFI_ACPI_6_4_GIC_ENABLED, 0,25,0,GICC_BASE, 0,0,0,0,0,0 }
};

// ── Entry Point ─────────────────────────────────────────────────────

EFI_STATUS EFIAPI
Rk3326AcpiPlatformEntryPoint (IN EFI_HANDLE Img, IN EFI_SYSTEM_TABLE *Sys)
{
  EFI_STATUS              Status;
  UINTN                   Handle;
  EFI_ACPI_TABLE_PROTOCOL *AcpiTable;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL,
                                (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "ACPI: no protocol (%r)\n", Status));
    return EFI_SUCCESS;
  }

  //
  // DSDT checksum — must be valid. InstallAcpiTable handles FADT/MADT/GTDT
  // checksums; DSDT is referenced through FADT.XDsdt and not installed
  // directly, so we checksum it ourselves.
  //
  mDsdt[9] = 0;
  mDsdt[9] = (UINT8)(256 - (AcpiChecksum (mDsdt, DSDT_LEN) - mDsdt[9]));

  DEBUG ((DEBUG_INFO, "ACPI: DSDT @ 0x%llx len=%d csum=0x%02x\n",
          (UINT64)(UINTN)mDsdt, DSDT_LEN, mDsdt[9]));

  //
  // Install FADT first — XDsdt is set to mDsdt by the static initializer.
  //
  Status = AcpiTable->InstallAcpiTable (AcpiTable,
                       &mFadt.Header, sizeof (mFadt), &Handle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ACPI FADT: %r\n", Status));
    return Status;
  }

  //
  // Use SDT protocol to update FADT's XDsdt in the installed copy.
  // The static FADT initializer sets .XDsdt, but InstallAcpiTable copies
  // the table; some EDK2 versions don't preserve XDsdt.  We use the SDT
  // protocol to open the installed FADT and write the DSDT pointer, then
  // update the checksum — the same pattern used by RK3588/RPi/Juno.
  //
  {
    EFI_ACPI_SDT_PROTOCOL  *Sdt;
    EFI_ACPI_SDT_HEADER    *FadtInstalled;
    UINTN                   TableKey;
    EFI_ACPI_HANDLE         SdtHandle;
    EFI_ACPI_TABLE_VERSION  Version;

    Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL,
                                  (VOID **)&Sdt);
    if (!EFI_ERROR (Status)) {
      //
      // Find the FADT we just installed (index 0).
      //
      UINTN  Index = 0;
      Status = Sdt->GetAcpiTable (Index,
                     &FadtInstalled, &Version, &TableKey);
      if (!EFI_ERROR (Status) &&
          FadtInstalled->Signature ==
            EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE)
      {
        //
        // Open for update, write XDsdt, recalculate checksum.
        //
        Status = Sdt->OpenSdt (TableKey, &SdtHandle);
        if (!EFI_ERROR (Status)) {
          EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE  *FadtPtr;
          FadtPtr = (EFI_ACPI_6_4_FIXED_ACPI_DESCRIPTION_TABLE *)
                     FadtInstalled;
          FadtPtr->XDsdt = (UINT64)(UINTN)mDsdt;
          DEBUG ((DEBUG_INFO,
                  "ACPI: FADT XDsdt ← 0x%llx (via SDT)\n",
                  FadtPtr->XDsdt));

          //
          // Re-checksum the FADT after modifying XDsdt.
          //
          FadtInstalled->Checksum = 0;
          FadtInstalled->Checksum = (UINT8)(256 - AcpiChecksum (
            (UINT8 *)FadtInstalled, FadtInstalled->Length));

          Sdt->Close (SdtHandle);
        }
      }
    }
  }

  //
  // Install DSDT directly to XSDT so acpiview / OS can find it
  // regardless of FADT.XDsdt.  FADT.XDsdt linkage via SDT above is
  // a best-effort enhancement; the direct XSDT entry always works.
  //
  Status = AcpiTable->InstallAcpiTable (AcpiTable,
                       mDsdt, DSDT_LEN, &Handle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ACPI DSDT: %r\n", Status));
    return Status;
  }

  Status = AcpiTable->InstallAcpiTable (AcpiTable,
                       &mMadt.Hdr.Header, sizeof (mMadt), &Handle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ACPI MADT: %r\n", Status));
    return Status;
  }

  Status = AcpiTable->InstallAcpiTable (AcpiTable,
                       &mGtdt.Header, sizeof (mGtdt), &Handle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ACPI GTDT: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "RK3326 ACPI: FADT+MADT+GTDT+DSDT installed\n"));
  return EFI_SUCCESS;
}
