/** @file
  ArmPlatformLib for RK3326 — adapted to EDK2 v202405 API.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/ArmLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Ppi/ArmMpCoreInfo.h>
#include <Soc.h>

#define MEM_UNMAPPED_REGION  0
#define MEM_BASIC_REGION     1
#define MEM_RUNTIME_REGION   2
#define MEM_RESERVED_REGION  3

typedef struct {
  CONST CHAR16    *Name;
  UINTN            Type;
} MEMORY_REGION_INFO;

// OP-TEE reserved: 0x08400000-0x08C00000 (from U-Boot memory banks)
#define OPTEE_BASE        0x08400000UL
#define OPTEE_SIZE        (8 * SIZE_1MB)

// System RAM after FV, before OP-TEE
#define RAM1_BASE         (FixedPcdGet64(PcdFvBaseAddress) + FixedPcdGet32(PcdFvSize))
#define RAM1_SIZE         (OPTEE_BASE - RAM1_BASE)

// System RAM after OP-TEE, up to end of physical DRAM (502MiB)
#define RAM2_BASE         (OPTEE_BASE + OPTEE_SIZE)  // 0x8C00000
#define RAM2_SIZE         (FixedPcdGet64(PcdSystemMemorySize) - OPTEE_SIZE - RAM1_SIZE - FixedPcdGet32(PcdFvSize))

STATIC MEMORY_REGION_INFO mMemoryInfo[] = {
  { L"TF-A + Pstore",         MEM_RESERVED_REGION },
  { L"UEFI Firmware Volume",  MEM_RUNTIME_REGION  },
  { L"System RAM (bank 0)",   MEM_BASIC_REGION    },
  { L"OP-TEE Reserved",       MEM_RESERVED_REGION },
  { L"System RAM (bank 1)",   MEM_BASIC_REGION    },
  { L"MMIO Peripherals",      MEM_UNMAPPED_REGION },
};

STATIC ARM_MEMORY_REGION_DESCRIPTOR mVirtualMemoryTable[] = {
  // TF-A + pstore (0x00000000 - 0x001FFFFF)
  { 0x00000000UL, 0x00000000UL, SIZE_2MB,
    ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // UEFI Firmware Volume
  { FixedPcdGet64 (PcdFvBaseAddress),
    FixedPcdGet64 (PcdFvBaseAddress),
    FixedPcdGet32 (PcdFvSize),
    ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // System RAM bank 0 (FV end → OP-TEE)
  { RAM1_BASE, RAM1_BASE, RAM1_SIZE,
    ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // OP-TEE reserved (0x08400000-0x08C00000)
  { OPTEE_BASE, OPTEE_BASE, OPTEE_SIZE,
    ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // System RAM bank 1 (OP-TEE end → DRAM end)
  { RAM2_BASE, RAM2_BASE, RAM2_SIZE,
    ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // MMIO peripherals (0xF8000000-0x100000000)
  { RK3326_PERIPH_BASE, RK3326_PERIPH_BASE, RK3326_PERIPH_SZ,
    ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  { 0, 0, 0, 0 }
};

STATIC ARM_CORE_INFO mRk3326MpCoreInfoTable[] = {
  { .Mpidr = 0x000 },
};

RETURN_STATUS EFIAPI ArmPlatformInitialize (IN UINTN MpId) {
  DEBUG ((DEBUG_ERROR, "\nRK3326 EDK2 UEFI (built %a %a)\n", __DATE__, __TIME__));
  return RETURN_SUCCESS;
}

UINT32 EFIAPI ArmPlatformGetBootMode (VOID) {
  return 0;
}

VOID EFIAPI ArmPlatformPeiBootAction (VOID) {
}

EFI_STATUS EFIAPI ArmPlatformInitializeSystemMemory (VOID) {
  return EFI_SUCCESS;
}

UINTN EFIAPI ArmPlatformGetCorePosition (IN UINTN MpId) {
  return (MpId >> 6) & 0xF;
}

UINTN EFIAPI ArmPlatformGetPrimaryCoreMpId (VOID) {
  return 0;
}

EFI_STATUS EFIAPI PrePeiCoreGetMpCoreInfo (OUT UINTN *CoreCount, OUT ARM_CORE_INFO **ArmCoreTable) {
  *CoreCount   = 1;
  *ArmCoreTable = mRk3326MpCoreInfoTable;
  return EFI_SUCCESS;
}

ARM_MP_CORE_INFO_PPI mMpCoreInfoPpi = { PrePeiCoreGetMpCoreInfo };

EFI_PEI_PPI_DESCRIPTOR mPlatformPpiList[] = {
  { EFI_PEI_PPI_DESCRIPTOR_PPI, &gArmMpCoreInfoPpiGuid, &mMpCoreInfoPpi }
};

VOID EFIAPI ArmPlatformGetPlatformPpiList (OUT UINTN *PpiListSize, OUT EFI_PEI_PPI_DESCRIPTOR **PpiList) {
  *PpiListSize = sizeof(mPlatformPpiList) / sizeof(EFI_PEI_PPI_DESCRIPTOR);
  *PpiList = mPlatformPpiList;
}

VOID EFIAPI ArmPlatformGetVirtualMemoryMap (OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap) {
  *VirtualMemoryMap = mVirtualMemoryTable;
}

VOID EFIAPI Rk3588PlatformGetVirtualMemoryInfo (OUT MEMORY_REGION_INFO **MemoryInfo) {
  *MemoryInfo = mMemoryInfo;
}
