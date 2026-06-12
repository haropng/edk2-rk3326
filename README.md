# RK3326 (PX30) EDK2 UEFI

UEFI firmware for Rockchip RK3326/PX30 — 4×Cortex-A35, 480×640 MIPI DSI.

**Status**: ✅ All core features complete.

[中文文档](README_cn.md) · [CHANGELOG](CHANGELOG.md) · [Porting Status](docs/porting-status.md) · [Guide](docs/porting-guide.md)

## Quick Start

```bash
cd edk2-rk3326 && ./build.sh DEBUG
# → Build/RK3326EVB/DEBUG_GCC/FV/BL33_AP_UEFI.Fv
```

## Boot Flow

```
U-Boot → EDK2 @0x00200000 → PeilessSec → DXE → BDS → Shell(auto) → exit → UiApp
```

## Features

| Feature | Status | Note |
|---------|--------|------|
| Display | ✅ | GOP 640×480 (480×640 SW rotate) |
| eMMC | ✅ | DWMMC v2.70a @52MHz HS, GPT/FAT32 |
| UEFI Shell | ✅ | Auto-boot, `exit` → UiApp |
| UiApp Menu | ✅ | GPIO keys operable |
| GPIO Keys | ✅ | UP + ESC (SimpleTextInEx) |
| ACPI | ✅ | FADT+DSDT+MADT+GTDT (0 Error) |
| USB2 | ✅ | EHCI+OHCI, KB + Mass Storage |
| Variable | ✅ | Emulated NVRAM |

## Hardware

| Component | Address | Note |
|-----------|---------|------|
| SoC | RK3326 (PX30) | 4×A35, GIC-400 |
| UART2 | 0xFF160000 | 115200 8N1 |
| eMMC | 0xFF390000 | DWMMC v2.70a |
| Display | 0xFF460000 | MIPI DSI 480×640 |
| GICD/GICC | 0xFF131000/0xFF132000 | GICv2 |

## Key Design

- **Display**: `SimpleFbDxe` 90°CCW+X-flip Blt()
- **Boot**: Shell with `LOAD_OPTION_ACTIVE`, skip device discovery
- **eMMC**: `EmbeddedPkg/MmcDxe` `InitializeEmmcDevice` 400KHz→52MHz
- **ACPI**: 4 tables, 70B hand-written DSDT AML

## Docs

| Document | Content |
|----------|---------|
| [`README_cn.md`](README_cn.md) | 中文说明 |
| [`CHANGELOG.md`](CHANGELOG.md) | Change log |
| [`docs/porting-status.md`](docs/porting-status.md) | Full status |
| [`docs/porting-guide.md`](docs/porting-guide.md) | Porting guide |
| [`docs/px30-emmc-debug.md`](docs/px30-emmc-debug.md) | eMMC debug notes |
| [`docs/edk2-all.patch`](docs/edk2-all.patch) | All EDK2 patches |
| [`docs/patches/`](docs/patches/) | Individual patches |
