/** @file
  RK3326 ACPI Platform Driver — minimal ARM ACPI tables (MADT, GTDT, DSDT).
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

#define GICD_BASE  0xFF131000ULL
#define GICC_BASE  0xFF132000ULL
#define TIMER_GSIV 30

// GTDT
STATIC EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE mGtdt = {
  { EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE,
    sizeof(EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE),
    EFI_ACPI_6_4_GENERIC_TIMER_DESCRIPTION_TABLE_REVISION,
    0,{'R','K','3','3','2','6'},0,1,0,0 },
  0,0,0,TIMER_GSIV,0,0,0,0,0
};

// MADT (GICv2, 1 core)
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

// DSDT — 36-byte ACPI header + 34-byte AML
#define DSDT_LEN 70
STATIC UINT8 mDsdt[DSDT_LEN] = {
  0x44,0x53,0x44,0x54, DSDT_LEN&0xFF,(DSDT_LEN>>8)&0xFF,(DSDT_LEN>>16)&0xFF,(DSDT_LEN>>24)&0xFF,
  0x02,0x00, // Revision=2, Checksum=0 (filled by AcpiTableDxe)
  'R','K','3','3','2','6','R','K','3','3','2','6',' ',' ',
  0x01,0x00,0x00,0x00,'E','D','K','2',0x01,0x00,0x00,0x00,
  // Scope(\_SB_) { Device(CPU0) { Name(_HID,"ACPI0007"), Name(_UID,0) } }
  0x10,0x0B,0x5F,0x53,0x42,0x5F,
  0x5B,0x83,0x0B,0x43,0x50,0x55,0x30,
  0x08,0x5F,0x48,0x49,0x44,0x0D,'A','C','P','I','0','0','0','7',0x00,
  0x08,0x5F,0x55,0x49,0x44,0x00,
};

EFI_STATUS EFIAPI
Rk3326AcpiPlatformEntryPoint (IN EFI_HANDLE Img, IN EFI_SYSTEM_TABLE *Sys)
{
  EFI_STATUS S; UINTN H; EFI_ACPI_TABLE_PROTOCOL *A;
  S = gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid,NULL,(VOID**)&A);
  if(EFI_ERROR(S)){DEBUG((DEBUG_WARN,"ACPI: no protocol (%r)\n",S));return EFI_SUCCESS;}
  S=A->InstallAcpiTable(A,&mMadt.Hdr.Header,sizeof(mMadt),&H);
  if(EFI_ERROR(S)){DEBUG((DEBUG_ERROR,"ACPI MADT:%r\n",S));return S;}
  S=A->InstallAcpiTable(A,&mGtdt.Header,sizeof(mGtdt),&H);
  if(EFI_ERROR(S)){DEBUG((DEBUG_ERROR,"ACPI GTDT:%r\n",S));return S;}
  S=A->InstallAcpiTable(A,mDsdt,DSDT_LEN,&H);
  if(EFI_ERROR(S)){DEBUG((DEBUG_ERROR,"ACPI DSDT:%r\n",S));return S;}
  DEBUG((DEBUG_INFO,"RK3326 ACPI: MADT+GTDT+DSDT installed\n"));
  return EFI_SUCCESS;
}
