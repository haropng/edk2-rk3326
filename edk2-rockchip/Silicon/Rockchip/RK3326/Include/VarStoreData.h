/** @file
  Variable Store Data for RK3326 — minimal stub.

  The RK3326 bring-up port does not have CPU cluster presets, ComboPHY,
  PCIe, or cooling fan. When HII setup menus are added, varstore
  structures will be defined here.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __RK3326_VARSTORE_DATA_H__
#define __RK3326_VARSTORE_DATA_H__

#include <Uefi/UefiBaseType.h>

#pragma pack (1)

#define CONFIG_TABLE_MODE_FDT       0x00000002
typedef struct {
  UINT32    Mode;
} CONFIG_TABLE_MODE_VARSTORE_DATA;

#define FDT_COMPAT_MODE_UNSUPPORTED  0
#define FDT_COMPAT_MODE_VENDOR       1
#define FDT_COMPAT_MODE_MAINLINE     2
typedef struct {
  UINT32    Mode;
} FDT_COMPAT_MODE_VARSTORE_DATA;

#pragma pack ()

#endif /* __RK3326_VARSTORE_DATA_H__ */
