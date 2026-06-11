/** @file
  FdtDxe for RK3326 — load DTB from FV and install as config table.

  Following the RK356x pattern: DTB GUID is inlined here rather than
  relying on a separate RockchipPlatformLib (avoiding BASE build rule issues).

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Guid/Fdt.h>
#include <Uefi/UefiBaseType.h>

//
// DTB file GUID for RK3326 EVB LPDDR3 v10
// FDF embeds DTB as: FILE FREEFORM = <guid> { SECTION RAW = <dtb> }
//
STATIC EFI_GUID mRk3326EvbDtbFileGuid = {
  0xe8b0c3de, 0x9e7f, 0x4f8e,
  { 0xa7, 0xc8, 0x5e, 0x6f, 0x7a, 0x8b, 0x9c, 0x0d }
};

STATIC
CONST EFI_GUID *
PlatformGetDtbFileGuid (IN UINT32 CompatMode)
{
  return (CompatMode == 0) ? NULL : &mRk3326EvbDtbFileGuid;
}

STATIC
EFI_STATUS
LoadPlatformFdt (OUT VOID **Fdt, OUT UINTN *FdtSize)
{
  EFI_STATUS      Status;
  CONST EFI_GUID *DtbGuid;
  VOID           *Dtb;
  UINTN           Size;

  DtbGuid = PlatformGetDtbFileGuid (1 /* FDT_COMPAT_MODE_VENDOR */);
  if (DtbGuid == NULL) return EFI_UNSUPPORTED;

  Status = GetSectionFromAnyFv (DtbGuid, EFI_SECTION_RAW, 0, &Dtb, &Size);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "FdtDxe: DTB not found in FV (Status=%r)\n", Status));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "FdtDxe: Loaded DTB from FV (size=0x%x)\n", Size));
  *Fdt = Dtb;
  *FdtSize = Size;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FdtDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  VOID       *Fdt;
  UINTN       Size;

  DEBUG ((DEBUG_INFO, "FdtDxe: Enter (RK3326 FDT-only mode)\n"));

  Status = LoadPlatformFdt (&Fdt, &Size);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "FdtDxe: No DTB — boot may still work\n"));
    return EFI_SUCCESS;
  }

  Status = SystemTable->BootServices->InstallConfigurationTable (
                                         &gFdtTableGuid, Fdt);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FdtDxe: InstallConfigurationTable failed: %r\n", Status));
    FreePool (Fdt);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "FdtDxe: DTB (%d bytes) installed\n", Size));
  return EFI_SUCCESS;
}
