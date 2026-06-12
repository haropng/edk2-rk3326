/** @file
  SimpleFbDxe for RK3326 (PX30) — GOP driver with 90° software rotation.

  Physical framebuffer: 480×640 portrait (MIPI DSI panel).
  GOP reports 640×480 landscape — Blt rotates all operations by 90° CCW
  with X-flip so UiApp/console render correctly on the narrow panel.

  Reads VOPB WIN1 registers for dynamic framebuffer address (U-Boot BSP).
  Protects framebuffer from UEFI memory allocator.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Soc.h>

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

STATIC EFI_STATUS EFIAPI
Qry (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *T, IN UINT32 M,
     OUT UINTN *S, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **I)
{
  *I = AllocateCopyPool (sizeof(**I), T->Mode->Info);
  if (*I == NULL) return EFI_OUT_OF_RESOURCES;
  *S = sizeof(**I);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Set (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *T, IN UINT32 M)
{
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
Blt (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *T, IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *B OPTIONAL,
     IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION O, IN UINTN SX, IN UINTN SY,
     IN UINTN DX, IN UINTN DY, IN UINTN W, IN UINTN H, IN UINTN D OPTIONAL)
{
  RETURN_STATUS R;
  UINTN i, j, PhysStride, GopWidth;

  // Physical framebuffer: 480×640 portrait, stride=480
  // GOP reports:           640×480 landscape (90° CCW, X-flipped)
  // Mapping: logical(x,y) -> physical offset = (639-x)*480 + y
  PhysStride = FixedPcdGet32 (PcdMipiFrameBufferWidth);   // 480
  GopWidth   = FixedPcdGet32 (PcdMipiFrameBufferHeight);  // 640

  switch (O) {
  case EfiBltVideoFill:
    for (j = 0; j < H; j++) {
      for (i = 0; i < W; i++) {
        UINT32 *pix = (UINT32 *)(UINTN)mGop.Mode->FrameBufferBase;
        pix[(GopWidth - 1 - (DX + i)) * PhysStride + (DY + j)] = *(UINT32 *)B;
      }
    }
    R = RETURN_SUCCESS;
    break;

  case EfiBltBufferToVideo:
    {
      UINTN SrcStride = (D == 0) ? W : (D / sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      for (j = 0; j < H; j++) {
        for (i = 0; i < W; i++) {
          UINT32 *src = (UINT32 *)B + j * SrcStride + i;
          UINT32 *dst = (UINT32 *)(UINTN)mGop.Mode->FrameBufferBase;
          dst[(GopWidth - 1 - (DX + i)) * PhysStride + (DY + j)] = *src;
        }
      }
    }
    R = RETURN_SUCCESS;
    break;

  case EfiBltVideoToBltBuffer:
    {
      UINTN DstStride = (D == 0) ? W : (D / sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      for (j = 0; j < H; j++) {
        for (i = 0; i < W; i++) {
          UINT32 *src = (UINT32 *)(UINTN)mGop.Mode->FrameBufferBase;
          UINT32 *dst = (UINT32 *)B + j * DstStride + i;
          *dst = src[(GopWidth - 1 - (SX + i)) * PhysStride + (SY + j)];
        }
      }
    }
    R = RETURN_SUCCESS;
    break;

  case EfiBltVideoToVideo:
    for (j = 0; j < H; j++) {
      for (i = 0; i < W; i++) {
        UINT32 *fb = (UINT32 *)(UINTN)mGop.Mode->FrameBufferBase;
        fb[(GopWidth - 1 - (DX + i)) * PhysStride + (DY + j)] =
          fb[(GopWidth - 1 - (SX + i)) * PhysStride + (SY + j)];
      }
    }
    R = RETURN_SUCCESS;
    break;

  default:
    R = RETURN_UNSUPPORTED;
    break;
  }

  WriteBackInvalidateDataCacheRange (
    (VOID *)(UINTN)mGop.Mode->FrameBufferBase,
    mGop.Mode->FrameBufferSize);

  return RETURN_ERROR (R) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

EFI_STATUS EFIAPI
SimpleFbDxeInitialize (IN EFI_HANDLE Img, IN EFI_SYSTEM_TABLE *Sys)
{
  EFI_STATUS  S;
  EFI_HANDLE  H = NULL;
  UINT32      Addr, Width, Height, Sz;
  EFI_PHYSICAL_ADDRESS FbAddr;

  // Enable VOPB+DSI clocks (may be gated during UEFI handoff)
  {
    UINT32 cru = CRU_BASE;
    #define GATE(b,id) do{UINT32 r=(b)+0x100+((id)/16)*4,m=1U<<((id)%16);MmioWrite32(r,(m<<16)|m);}while(0)
    #define RST(b,id)  do{UINT32 r=(b)+0x200+((id)/16)*4,m=1U<<((id)%16);MmioWrite32(r,(m<<16)|0);}while(0)
    GATE(cru,174); GATE(cru,243); GATE(cru,322);
    GATE(cru,181); GATE(cru,251); GATE(cru,150);
    GATE(cru,324); GATE(cru,325);
    RST(cru,48); RST(cru,49); RST(cru,50);
    RST(cru,51); RST(cru,52); RST(cru,53);
    RST(cru,61); RST(cru,62);
    #undef GATE
    #undef RST
  }

  // Read framebuffer address/width from VOPB WIN1 (U-Boot 6.1 BSP config)
  {
    UINT32 mst = MmioRead32 (0xFF4600A0);
    UINT32 vir = MmioRead32 (0xFF460098) & 0x1FFF;
    Addr = (mst >= 0x10000000) ? mst : FixedPcdGet32(PcdMipiFrameBufferAddress);
    Width = (vir > 0) ? vir : FixedPcdGet32(PcdMipiFrameBufferWidth);
  }
  Height = FixedPcdGet32 (PcdMipiFrameBufferHeight);

  if (Addr == 0 || Width == 0 || Height == 0)
    return EFI_UNSUPPORTED;

  Sz = Width * Height * 4;
  FbAddr = (EFI_PHYSICAL_ADDRESS)Addr;

  // Protect framebuffer from UEFI memory allocator.
  // U-Boot allocated this region; UEFI doesn't know it's in use.
  // If this fails (e.g. already allocated), we still continue.
  {
    EFI_PHYSICAL_ADDRESS fb = FbAddr;
    EFI_STATUS            ProtStatus;
    ProtStatus = gBS->AllocatePages (AllocateAddress, EfiReservedMemoryType,
                                     EFI_SIZE_TO_PAGES (Sz), &fb);
    DEBUG ((DEBUG_INFO, "SimpleFbDxe: FB protect at 0x%lX -> %r\n", fb, ProtStatus));
  }

  // Allocate and init GOP mode
  S = gBS->AllocatePool (EfiBootServicesData, sizeof(*mGop.Mode), (VOID**)&mGop.Mode);
  if (EFI_ERROR(S)) return S;
  ZeroMem (mGop.Mode, sizeof(*mGop.Mode));
  S = gBS->AllocatePool (EfiBootServicesData, sizeof(*mGop.Mode->Info), (VOID**)&mGop.Mode->Info);
  if (EFI_ERROR(S)) return S;
  ZeroMem (mGop.Mode->Info, sizeof(*mGop.Mode->Info));

  mGop.Mode->MaxMode                    = 1;
  mGop.Mode->Mode                       = 0;
  mGop.Mode->Info->Version              = 0;
  // Report landscape 640×480 (physical framebuffer is 480×640 portrait,
  // 90° CCW rotation done in Blt function)
  mGop.Mode->Info->HorizontalResolution = Height;   // 640
  mGop.Mode->Info->VerticalResolution   = Width;    // 480
  mGop.Mode->Info->PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mGop.Mode->Info->PixelsPerScanLine    = Height;   // 640
  mGop.Mode->SizeOfInfo                 = sizeof(*mGop.Mode->Info);
  mGop.Mode->FrameBufferBase            = FbAddr;
  mGop.Mode->FrameBufferSize            = Sz;       // 480*640*4 (physical)

  // Clear physical framebuffer to black (rotated fill)
  {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = { 0, 0, 0, 0 };
    Blt (&mGop, &black, EfiBltVideoFill, 0, 0, 0, 0, Height, Width, 0);
  }

  DEBUG ((DEBUG_INFO,
    "SimpleFbDxe: phys=%dx%d GOP=%dx%d stride=%d ready\n",
    Width, Height, mGop.Mode->Info->HorizontalResolution,
    mGop.Mode->Info->VerticalResolution, mGop.Mode->Info->PixelsPerScanLine));

  mGop.QueryMode = Qry;
  mGop.SetMode   = Set;
  mGop.Blt       = Blt;

  S = gBS->InstallMultipleProtocolInterfaces (&H,
    &gEfiDevicePathProtocolGuid,     &mDp,
    &gEfiGraphicsOutputProtocolGuid, &mGop,
    NULL);
  return S;
}
