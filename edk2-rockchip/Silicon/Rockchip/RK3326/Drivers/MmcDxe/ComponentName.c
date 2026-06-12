/** @file
  MMC/SD Card Interface Driver - Component Name

  Copyright (c) 2011, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Mmc.h"

GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME_PROTOCOL  gMmcComponentName = {
  MmcGetDriverName,
  MmcGetControllerName,
  "eng"
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME2_PROTOCOL gMmcComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME) MmcGetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME) MmcGetControllerName,
  "en"
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE mMmcDriverNameTable[] = {
  {"eng;en", L"MMC/SD Card Interface Driver"},
  {NULL, NULL}
};

EFI_STATUS
EFIAPI
MmcGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mMmcDriverNameTable,
           DriverName,
           (BOOLEAN)(This == &gMmcComponentName)
           );
}

EFI_STATUS
EFIAPI
MmcGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  EFI_HANDLE                   ControllerHandle,
  IN  EFI_HANDLE                   ChildHandle        OPTIONAL,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **ControllerName
  )
{
  return EFI_UNSUPPORTED;
}
