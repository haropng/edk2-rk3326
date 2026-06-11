/** @file
  SimpleFbDxe for RK3326 — 32bpp shadow → 24bpp VOPB via Blt + cache flush.

  U-Boot configures VOPB WIN1: RGB888, VIR=240, framebuffer @ 0x1DF4C000.
  GOP uses 32bpp shadow buffer. Blt() flushes with stride clamp + cache clean.
  VOPB hardware not touched.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Protocol/GraphicsOutput.h>

typedef struct {
  VENDOR_DEVICE_PATH        DisplayDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  EndDevicePath;
} DISPLAY_DEVICE_PATH;

DISPLAY_DEVICE_PATH mDp = {
  { { HARDWARE_DEVICE_PATH, HW_VENDOR_DP,
      { (UINT8)(sizeof(VENDOR_DEVICE_PATH)), (UINT8)(sizeof(VENDOR_DEVICE_PATH) >> 8) } },
    EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof(EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mGop;
STATIC UINT32  mW, mH, mPhys, mPStride, mPMaxW;
STATIC UINT8  *mShadow;

STATIC VOID
Flush (UINTN X, UINTN Y, UINTN W, UINTN H)
{
  // NO-OP for test: don't touch U-Boot's physical buffer.
  // If U-Boot logo stays clean, hardware is OK and the bug is in our writes.
  // If corruption appears anyway, VOPB/DSI is broken at hardware level.
  return;
}

STATIC EFI_STATUS EFIAPI
Qry (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *T, IN UINT32 M,
     OUT UINTN *S, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **I)
{ *I = AllocateCopyPool(sizeof(**I), T->Mode->Info); *S = sizeof(**I); return EFI_SUCCESS; }

STATIC EFI_STATUS EFIAPI Set(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *T, IN UINT32 M) { return EFI_SUCCESS; }

STATIC EFI_STATUS EFIAPI
Blt (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *T, IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *B OPTIONAL,
     IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION O, IN UINTN SX, IN UINTN SY,
     IN UINTN DX, IN UINTN DY, IN UINTN W, IN UINTN H, IN UINTN D OPTIONAL)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *s, *d;
  UINTN r, ss;
  if (W == 0 || H == 0) return EFI_INVALID_PARAMETER;
  switch (O) {
  case EfiBltVideoFill:
    d = (VOID *)(mShadow + (DY * mW + DX) * sizeof(*d));
    for (r = 0; r < H; r++) {
      SetMem32 (d, W * sizeof(*d), *(UINT32 *)B);
      d = (VOID *)((UINT8 *)d + mW * sizeof(*d));
    }
    Flush(DX, DY, W, H);
    return EFI_SUCCESS;
  case EfiBltVideoToBltBuffer:
    ss = (D == 0) ? W : D; d = B; s = (VOID *)(mShadow + (SY*mW + SX)*sizeof(*s));
    for (r = 0; r < H; r++) {
      CopyMem (d, s, W * sizeof(*d));
      d = (VOID *)((UINT8 *)d + ss * sizeof(*d));
      s = (VOID *)((UINT8 *)s + mW * sizeof(*s));
    }
    return EFI_SUCCESS;
  case EfiBltBufferToVideo:
    ss = (D == 0) ? W : D; s = B; d = (VOID *)(mShadow + (DY*mW + DX)*sizeof(*d));
    for (r = 0; r < H; r++) {
      CopyMem (d, s, W * sizeof(*d));
      s = (VOID *)((UINT8 *)s + ss * sizeof(*s));
      d = (VOID *)((UINT8 *)d + mW * sizeof(*d));
    }
    Flush(DX, DY, W, H);
    return EFI_SUCCESS;
  default: return EFI_UNSUPPORTED;
  }
}

EFI_STATUS EFIAPI
SimpleFbDxeInitialize (IN EFI_HANDLE Img, IN EFI_SYSTEM_TABLE *Sys)
{
  EFI_STATUS S;
  EFI_HANDLE H = NULL;
  UINT32 Sz, vir, fmt;
  UINT8 *Fb;

  mH = FixedPcdGet32 (PcdMipiFrameBufferHeight);
  mPhys = FixedPcdGet32 (PcdMipiFrameBufferAddress);
  // Read VOPB stride from hardware to match U-Boot's exact config
  { UINT32 vir = MmioRead32 (0xFF460098) & 0x1FFF;
    UINT32 fmt = (MmioRead32 (0xFF460090) >> 4) & 7;
    UINT32 bpp = (fmt == 0) ? 4 : 3;
    mPStride = vir * bpp;
    mW = vir;  // match GOP to VOPB virtual width
    mPhys = MmioRead32 (0xFF4600A0);  // actual address from VOPB
  }
  mPMaxW = mW;
  if (mW == 0 || mH == 0 || mPhys == 0) return EFI_UNSUPPORTED;

  Sz = mW * mH * 4; // 32bpp shadow
  Fb = AllocatePages (EFI_SIZE_TO_PAGES (Sz));
  if (Fb == NULL) return EFI_OUT_OF_RESOURCES;
  mShadow = Fb;
  ZeroMem (Fb, Sz);
  // NO physical buffer write — keep U-Boot logo intact

  S = gBS->AllocatePool(EfiBootServicesData, sizeof(*mGop.Mode), (VOID**)&mGop.Mode);
  if (EFI_ERROR(S)) return S;
  ZeroMem(mGop.Mode, sizeof(*mGop.Mode));
  S = gBS->AllocatePool(EfiBootServicesData, sizeof(*mGop.Mode->Info), (VOID**)&mGop.Mode->Info);
  if (EFI_ERROR(S)) return S;
  ZeroMem(mGop.Mode->Info, sizeof(*mGop.Mode->Info));
  mGop.Mode->MaxMode = 1;
  mGop.Mode->Mode = 0;
  mGop.Mode->Info->Version = 0;
  mGop.Mode->Info->HorizontalResolution = mW;
  mGop.Mode->Info->VerticalResolution = mH;
  mGop.Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  mGop.Mode->Info->PixelsPerScanLine = mW;
  mGop.Mode->SizeOfInfo = sizeof(*mGop.Mode->Info);
  mGop.Mode->FrameBufferBase = (EFI_PHYSICAL_ADDRESS)(UINTN)Fb;
  mGop.Mode->FrameBufferSize = Sz;
  mGop.QueryMode = Qry;
  mGop.SetMode   = Set;
  mGop.Blt       = Blt;
  S = gBS->InstallMultipleProtocolInterfaces (&H,
    &gEfiDevicePathProtocolGuid,     &mDp,
    &gEfiGraphicsOutputProtocolGuid, &mGop,
    NULL);
  DEBUG((DEBUG_INFO, "SimpleFbDxe: %dx%d vir=%d fmt=%d stride=%d phys=0x%X shadow=%p\n",
         mW, mH, vir, fmt, mPStride, mPhys, Fb));
  return S;
}
