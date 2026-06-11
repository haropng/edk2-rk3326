/** @file
 *
 *  RkEmmcDxe platform helper library interface.
 *
 *  Copyright (c) 2026, RK3326 EDK2 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RKEMMC_PLATFORM_LIB_H__
#define __RKEMMC_PLATFORM_LIB_H__

#include <Uefi.h>

typedef enum {
  RkEmmcCardPresenceUnsupported = 0,
  RkEmmcCardPresent,
  RkEmmcCardNotPresent
} RKEMMC_CARD_PRESENCE_STATE;

EFI_STATUS
EFIAPI
RkEmmcSetClockRate (
  IN UINTN  Frequency
  );

VOID
EFIAPI
RkEmmcSetIoMux (
  VOID
  );

RKEMMC_CARD_PRESENCE_STATE
EFIAPI
RkEmmcGetCardPresenceState (
  VOID
  );

#endif /* __RKEMMC_PLATFORM_LIB_H__ */
