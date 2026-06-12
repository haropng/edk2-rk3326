/** @file
  MMC/SD Card Interface Driver - Main entry point

  Copyright (c) 2011-2013, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Protocol/DevicePath.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include "Mmc.h"

EFI_BLOCK_IO_MEDIA mMmcMediaTemplate = {
  SIGNATURE_32('m','m','c','o'),
  TRUE,   // RemovableMedia
  FALSE,  // MediaPresent
  FALSE,  // LogicalPartition
  FALSE,  // ReadOnly
  FALSE,  // WriteCaching
  512,    // BlockSize
  4,      // IoAlign
  0,      // Pad
  0       // LastBlock
};

LIST_ENTRY mMmcHostPool;

VOID
InitializeMmcHostPool (
  VOID
  )
{
  InitializeListHead (&mMmcHostPool);
}

VOID
InsertMmcHost (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  InsertTailList (&mMmcHostPool, &(MmcHostInstance->Link));
}

VOID
RemoveMmcHost (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  RemoveEntryList (&(MmcHostInstance->Link));
}

MMC_HOST_INSTANCE *
CreateMmcHostInstance (
  IN EFI_MMC_HOST_PROTOCOL *MmcHost
  )
{
  EFI_STATUS             Status;
  MMC_HOST_INSTANCE     *MmcHostInstance;

  MmcHostInstance = AllocateZeroPool (sizeof (MMC_HOST_INSTANCE));
  if (MmcHostInstance == NULL) {
    return NULL;
  }

  MmcHostInstance->Signature = MMC_HOST_INSTANCE_SIGNATURE;
  MmcHostInstance->MmcHost   = MmcHost;
  MmcHostInstance->State     = MmcHwInitializationState;

  // Block IO
  MmcHostInstance->BlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  MmcHostInstance->BlockIo.Media       = &MmcHostInstance->Media;
  MmcHostInstance->BlockIo.Reset       = MmcReset;
  MmcHostInstance->BlockIo.ReadBlocks  = MmcReadBlocks;
  MmcHostInstance->BlockIo.WriteBlocks = MmcWriteBlocks;
  MmcHostInstance->BlockIo.FlushBlocks = NULL;

  CopyMem (&MmcHostInstance->Media, &mMmcMediaTemplate, sizeof (EFI_BLOCK_IO_MEDIA));

  // Disk IO
  MmcHostInstance->DiskIo.Revision  = EFI_DISK_IO_PROTOCOL_REVISION;
  MmcHostInstance->DiskIo.ReadDisk  = MmcReadDisk;
  MmcHostInstance->DiskIo.WriteDisk = MmcWriteDisk;

  // Install protocols
  Status = gBS->InstallMultipleProtocolInterfaces (
             &MmcHostInstance->Handle,
             &gEfiBlockIoProtocolGuid, &MmcHostInstance->BlockIo,
             &gEfiDiskIoProtocolGuid,  &MmcHostInstance->DiskIo,
             NULL
             );
  if (EFI_ERROR (Status)) {
    FreePool (MmcHostInstance);
    return NULL;
  }

  InsertMmcHost (MmcHostInstance);

  DEBUG ((DEBUG_INFO, "MmcDxe: Created host instance %p\n", MmcHostInstance));
  return MmcHostInstance;
}

EFI_STATUS
DestroyMmcHostInstance (
  IN MMC_HOST_INSTANCE *MmcHostInstance
  )
{
  RemoveMmcHost (MmcHostInstance);
  gBS->UninstallMultipleProtocolInterfaces (
         MmcHostInstance->Handle,
         &gEfiBlockIoProtocolGuid, &MmcHostInstance->BlockIo,
         &gEfiDiskIoProtocolGuid,  &MmcHostInstance->DiskIo,
         NULL
         );
  FreePool (MmcHostInstance);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MmcDxeInitialize (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS            Status;
  UINTN                 Index;
  EFI_HANDLE           *Handles;
  UINTN                 NoHandles;
  MMC_HOST_INSTANCE    *MmcHostInstance;
  EFI_MMC_HOST_PROTOCOL *MmcHost;

  DEBUG ((DEBUG_INFO, "MmcDxe: Initializing MMC Block I/O driver\n"));

  InitializeMmcHostPool ();

  // Locate all MMC Host Protocol instances
  Status = gBS->LocateHandleBuffer (
             ByProtocol,
             &gEmbeddedMmcHostProtocolGuid,
             NULL,
             &NoHandles,
             &Handles
             );
  if (EFI_ERROR (Status) || NoHandles == 0) {
    DEBUG ((DEBUG_WARN, "MmcDxe: No MMC host controllers found\n"));
    return Status;
  }

  for (Index = 0; Index < NoHandles; Index++) {
    Status = gBS->HandleProtocol (
               Handles[Index],
               &gEmbeddedMmcHostProtocolGuid,
               (VOID **)&MmcHost
               );
    if (EFI_ERROR (Status)) {
      continue;
    }

    MmcHostInstance = CreateMmcHostInstance (MmcHost);
    if (MmcHostInstance == NULL) {
      continue;
    }

    // Initialize the MMC device (detect card, read CID/CSD, etc.)
    Status = InitializeMmcDevice (MmcHostInstance);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MmcDxe: Failed to init device: %r\n", Status));
      DestroyMmcHostInstance (MmcHostInstance);
      continue;
    }
  }

  FreePool (Handles);
  DEBUG ((DEBUG_INFO, "MmcDxe: Initialization complete\n"));
  return EFI_SUCCESS;
}
