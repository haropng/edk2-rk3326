# RK3326 (PX30) EDK2 UEFI Firmware / 固件

UEFI firmware for Rockchip RK3326/PX30 — 4×Cortex-A35, GIC-400, 480×640 MIPI DSI.  
适用于 Rockchip RK3326/PX30 的 UEFI 固件。

**Status / 状态**: ✅ All core features working / 全部核心功能工作 — UiApp + Shell + GPIO keys.

## Hardware / 硬件

| Component / 组件 | Address / 地址 | Note / 备注 |
|------|------|------|
| SoC | RK3326 (PX30) | 4×Cortex-A35 |
| DRAM | 0x00000000 | LPDDR3, ~500MB |
| UART2 | 0xFF160000 | 115200 8N1 |
| eMMC | 0xFF390000 | DWMMC v2.70a, GPT |
| Display / 显示 | 0xFF460000 (VOPB) | MIPI DSI 480×640 portrait / 竖屏 |
| GOP reports / 上报 | 640×480 landscape / 横屏 | Software rotation / 软件旋转 |

## Boot Flow / 启动流程

```
U-Boot 6.1 BSP → boot_android → EDK2 @ 0x00200000
       → PeilessSec → DXE → BDS → UiApp / Shell
```

## Build / 构建

```bash
./build.sh DEBUG
# Output / 输出: Build/RK3326EVB/DEBUG_GCC/FV/BL33_AP_UEFI.Fv
```

## Flash / 刷写

```bash
magiskboot unpack boot.img
cp Build/.../BL33_AP_UEFI.Fv kernel
magiskboot repack boot.img new-boot.img
upgrade_tool di -b new-boot.img
```

## Docs / 文档

| File / 文件 | Content / 内容 |
|------|------|
| `CHANGELOG.md` | Change log / 改动日志 |
| `docs/porting-status.md` | Status + EDK2 patch list / 移植状态 |
| `docs/porting-guide.md` | Porting guide / 移植指南 |
| `docs/px30-emmc-debug.md` | eMMC DWMMC v2.70a debug / 调试记录 |
| `docs/edk2-all.patch` | All EDK2 patches (combined) / 全部补丁 |
| `docs/patches/` | Per-category patches / 分类补丁 (4 files) |
upgrade_tool rd
```

## Project Structure

```
edk2-rk3326/
├── build.sh                   # Build script (GCC, tools_def patching)
├── edk2/                      # EDK2 upstream (submodule)
├── edk2-rockchip/
│   └── Silicon/Rockchip/RK3326/
│       ├── RK3326.dec         # SoC PCD declarations
│       ├── RK3326.dsc.inc     # Self-contained platform DSC
│       ├── RK3326.fdf         # FD layout (2MB)
│       ├── Include/
│       │   ├── Soc.h          # MMIO map (verified from px30.dtsi)
│       │   └── VarStoreData.h
│       └── Library/
│           ├── PlatformLib/   # ArmPlatformLib + MMU memory map
│           ├── MemoryInitPeiLib/
│           ├── SerialPortLib/ # DW APB UART (preserves U-Boot config)
│           ├── SdramLib/      # DMC-based DRAM detection
│           ├── CruLib/        # CRU clock/reset
│           ├── GpioLib/       # GPIO read/write
│           └── OtpLib/        # OTP chip version
│       └── Drivers/
│           ├── RK3326Dxe/     # SoC init
│           ├── FdtDxe/        # DTB loading from FV
│           └── SimpleFbDxe/   # GOP framebuffer (480×640 32bpp)
│               └── README.md  # Driver design doc
├── docs/
│   ├── rockchip_display.patch # U-Boot 6.1 BSP modifications
│   └── GraphicsConsole.patch  # Narrow-screen (480px) fix
├── rk3326-appolo.dts          # Device DTS reference
└── .gitignore
```

## GOP Display (SimpleFbDxe)

480×640 32bpp MIPI DSI portrait panel. FrameBufferBltLib mode (RK3399 pattern), no shadow buffer.

| Detail | Value |
|--------|-------|
| Driver | `Silicon/Rockchip/RK3326/Drivers/SimpleFbDxe` |
| GOP resolution | 480×640 |
| Pixel format | `PixelBlueGreenRedReserved8BitPerColor` (32bpp) |
| Framebuffer | VOPB physical buffer (U-Boot allocated) |
| Blt | FrameBufferBltLib |
| Cache | WriteBackInvalidateDataCacheRange |
| U-Boot required | 6.1 BSP with `rockchip_display.c` patch |

**Key design decisions**:
- Reads VOPB WIN1 registers (YRGB_MST @ 0xA0, VIR @ 0x98) for dynamic fb address
- `AllocatePages(AllocateAddress)` protects fb from UEFI memory allocator
- CRU clock gates re-enabled (may be gated during UEFI handoff)
- VOPB registers are read-only — CFG_DONE mechanism unknown on PX30

## GraphicsConsole (Text on Screen)

Text console works via GOP→Blt (60 columns at 8px glyph). Narrow-screen fix applied
to `GraphicsConsole.c` `InitializeGraphicsConsoleTextMode()`:
- DeltaX clamped to 0 (prevents underflow from 80*8 > 480)
- ASSERT replaced with DEBUG_WARN
- Always ≥2 text modes

**Known limits**:
- UiApp (BootManagerMenu) crashes — HiiDatabase buffer overflow at 480px.
  Workaround: boot directly to Shell (`PcdBootManagerMenuFile` = zeros,
  `PcdUefiShellDefaultBootEnable=TRUE`).
- Full UI support needs a narrower font (e.g. 6px → 80 columns).

## U-Boot 6.1 BSP Modifications

File: `6.1-rksdk/u-boot/drivers/video/drm/rockchip_display.c`

Three locations modified (full patch: `docs/rockchip_display.patch`):

| Function | Change |
|----------|--------|
| `display_logo()` | `src_rect` = panel resolution, `crtc_rect` = fullscreen 1:1 |
| `display_bmp()` | Same as above |
| `load_bmp_logo()` | Buffer ≥ 480×640×4 bytes |

Detailed design doc: `Silicon/Rockchip/RK3326/Drivers/SimpleFbDxe/README.md`

## Key Design Decisions

**Self-contained DSC** — Not using shared `Rockchip.dsc.inc`. Our DSC declares exactly what RK3326 needs (42 components).

**SerialPortLib no-op** — SerialPortInitialize and SetAttributes return SUCCESS without touching UART registers. Prevents RX breakage from repeated re-init by DXE drivers.

**Memory map with OP-TEE hole** — U-Boot reserves 0x08400000-0x08C00000. MMU map skips this region.

**FrameBufferBltLib** — Standard EDK2 Blt library. Avoids manual byte-order bugs that plagued early development (RGB888 vs BGR888, alpha channel).

**Framebuffer protection** — `AllocatePages(AllocateAddress, EfiReservedMemoryType)` prevents other DXE drivers from allocating over the U-Boot framebuffer.

## MMIO Map

| Peripheral | Address |
|-----------|---------|
| UART2 | 0xFF160000 |
| GIC-400 D | 0xFF131000 |
| GIC-400 C | 0xFF132000 |
| CRU | 0xFF2B0000 |
| PMUCRU | 0xFF2BC000 |
| eMMC | 0xFF390000 |
| SDMMC | 0xFF370000 |
| VOPB | 0xFF460000 |
| DSI | 0xFF450000 |
| DMC | 0xFF2A0000 |
| GPIO0-3 | 0xFF040000, 0xFF250000–0xFF270000 |
| I2C0-3 | 0xFF180000–0xFF1B0000 |

## Status

| Feature | Status |
|---------|--------|
| Boot (PeilessSec→DXE→BDS→Shell) | ✅ |
| Serial I/O (1.5Mbaud 8N1) | ✅ |
| UEFI Shell (interactive) | ✅ |
| Variables (emulated NVRAM) | ✅ |
| **GOP Framebuffer (480×640 32bpp)** | ✅ |
| **GraphicsConsole (text on screen)** | ✅ (60 cols, UiApp disabled) |
| eMMC/SD block devices | ❌ Needs hardware driver port |
| USB | ❌ |
| GMAC | ❌ |
| Multi-core (PSCI) | ❌ Single-core only |

## Credits

Based on analysis of: RK3588 (edk2-rockchip shared infra), RK3576 (GICv2 pattern), RK356x (minimal DSC pattern), RK3399 (SimpleFbDxe FrameBufferBltLib pattern), RK3326 6.1 BSP U-Boot (VOPB register layout).

Copyright (c) 2024–2026. SPDX: BSD-2-Clause-Patent.
