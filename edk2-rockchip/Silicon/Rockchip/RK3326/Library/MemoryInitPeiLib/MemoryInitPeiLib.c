/** @file
  MemoryInitPeiLib — generic ARM memory initialization via MMU map.

  Reuses the pattern from RK3588/RK3576. Calls ArmPlatformGetVirtualMemoryMap()
  to get the SoC memory layout, then creates HOBs and enables the MMU.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrePiLib.h>
#include <Soc.h>

#define MEM_UNMAPPED_REGION  0
#define MEM_BASIC_REGION     1
#define MEM_RUNTIME_REGION   2
#define MEM_RESERVED_REGION  3

typedef struct {
  CONST CHAR16    *Name;
  UINTN            Type;
} MEMORY_REGION_INFO;

extern
VOID
EFIAPI
Rk3588PlatformGetVirtualMemoryInfo (
  OUT MEMORY_REGION_INFO **MemoryInfo
  );

STATIC
VOID
AddBasicMemoryRegion (IN ARM_MEMORY_REGION_DESCRIPTOR *Desc)
{
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    Desc->PhysicalBase,
    Desc->Length
    );
}

STATIC
VOID
AddReservedMemoryRegion (IN ARM_MEMORY_REGION_DESCRIPTOR *Desc)
{
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    Desc->PhysicalBase,
    Desc->Length
    );
  BuildMemoryAllocationHob (
    Desc->PhysicalBase,
    Desc->Length,
    EfiReservedMemoryType
    );
}

STATIC
VOID
AddRuntimeServicesRegion (IN ARM_MEMORY_REGION_DESCRIPTOR *Desc)
{
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    Desc->PhysicalBase,
    Desc->Length
    );
  BuildMemoryAllocationHob (
    Desc->PhysicalBase,
    Desc->Length,
    EfiRuntimeServicesData
    );
}

STATIC
VOID
AddUnmappedMemoryRegion (IN ARM_MEMORY_REGION_DESCRIPTOR *Desc)
{
  // MMIO — do not add to system memory HOBs
}

EFI_STATUS
EFIAPI
MemoryPeim (IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{
  ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable;
  MEMORY_REGION_INFO           *MemoryInfo;
  UINTN                         Index;

  ArmPlatformGetVirtualMemoryMap (&MemoryTable);
  Rk3588PlatformGetVirtualMemoryInfo (&MemoryInfo);

  for (Index = 0; MemoryTable[Index].Length != 0; Index++) {
    switch (MemoryInfo[Index].Type) {
    case MEM_UNMAPPED_REGION:
      AddUnmappedMemoryRegion (&MemoryTable[Index]);
      break;
    case MEM_BASIC_REGION:
      AddBasicMemoryRegion (&MemoryTable[Index]);
      break;
    case MEM_RUNTIME_REGION:
      AddRuntimeServicesRegion (&MemoryTable[Index]);
      break;
    case MEM_RESERVED_REGION:
      AddReservedMemoryRegion (&MemoryTable[Index]);
      break;
    }
  }

  // Raw debug marker — confirms we reach MMU config
  { volatile UINT32 *u = (volatile UINT32 *)UART2_BASE;
    const char *m = "\n[MMU]\n"; while (*m) { while (!(u[31]&2)); u[0]=*m++; } }

  ArmConfigureMmu (MemoryTable, NULL, NULL);

  // Raw debug marker — confirms MMU config completed
  { volatile UINT32 *u = (volatile UINT32 *)UART2_BASE;
    const char *m = "\n[DXE]\n"; while (*m) { while (!(u[31]&2)); u[0]=*m++; } }

  return EFI_SUCCESS;
}
