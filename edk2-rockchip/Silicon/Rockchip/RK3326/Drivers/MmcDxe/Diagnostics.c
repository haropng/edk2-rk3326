/** @file
  Diagnostics Protocol implementation for the MMC DXE driver

  Copyright (c) 2011-2014, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include "Mmc.h"

CHAR16 *mLogBuffer = NULL;
UINTN   mLogRemainChar = 0;

EFI_STATUS
EFIAPI
MmcDriverDiagnosticsRunDiagnostics (
  IN  EFI_DRIVER_DIAGNOSTICS2_PROTOCOL              *This,
  IN  EFI_HANDLE                                      ControllerHandle,
  IN  EFI_HANDLE                                      ChildHandle  OPTIONAL,
  IN  EFI_DRIVER_DIAGNOSTIC_TYPE                      DiagnosticType,
  IN  CHAR8                                          *Language,
  OUT EFI_DRIVER_DIAGNOSTIC_RESULT                  **ErrorType,
  OUT CHAR16                                        **Buffer
  )
{
  *ErrorType = NULL;
  *Buffer    = NULL;
  return EFI_UNSUPPORTED;
}
