# Porting Guide

> From zero to EDK2 on RK3326/PX30 — 480×640 MIPI DSI portrait device.

→ [中文](porting-guide_cn.md) · [README](../README.md) · [Status](porting-status.md)

## 1. Overview

RK3326 (PX30): 4×Cortex-A35, GIC-400, DWMMC v2.70a, MIPI DSI.
U-Boot initializes VOPB/DSI; EDK2 reads framebuffer via VOPB WIN1.

## 2. Build

```bash
export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
cd edk2-rk3326 && ./build.sh DEBUG
```

## 3. Directory Structure

```
edk2-rk3326/
├── build.sh
├── edk2/                         # EDK2 upstream submodule
├── edk2-rockchip/Silicon/Rockchip/RK3326/
│   ├── RK3326.dsc.inc            # Platform DSC
│   ├── RK3326.fdf                # Flash layout
│   └── Drivers/                  # Custom drivers (10)
│       ├── Px30EmmcDxe/          # eMMC DWMMC v2.70a
│       ├── SimpleFbDxe/          # GOP + SW rotate
│       ├── AcpiPlatformDxe/      # ACPI 4 tables
│       ├── GpioKeypadDxe/        # GPIO keys
│       └── ...                   # 6 more drivers
├── docs/
└── CHANGELOG.md
```

## 4. EDK2 Patches (4 patches, 5 files)

| # | File | Change |
|---|------|--------|
| 1 | `PlatformBm.c` | Shell auto-boot, skip discovery |
| 2 | `PeilessSec.c` | MP Core PPI optional |
| 3 | `MmcBlockIo.c` | BlockCount=0 fix |
| 4 | `Gpt.c`+`Mbr.c` | GPT CRC skip + MBR 0xEE skip |

```bash
cd edk2 && git apply ../docs/edk2-all.patch
```

## 5. Key Drivers

### 5.1 eMMC — Px30EmmcDxe
DWMMC v2.70a @0xFF390000: direct registers, 52MHz HS, 1ms delay, v2.70a quirks.

### 5.2 Display — SimpleFbDxe
Physical 480×640 → GOP 640×480 via 90°CCW + X-flip Blt().

### 5.3 ACPI — AcpiPlatformDxe
4 tables: FADT (HW_REDUCED+PSCI) → DSDT (CPU0), MADT (GICv2), GTDT (GSIV=30).
70B hand-written AML, `InstallAcpiTable` auto-link.

### 5.4 GPIO Keys — GpioKeypadDxe
PB5→UP(0x01), PB7→ESC(0x17), 50ms poll, SimpleTextInEx.

### 5.5 USB Host — UsbHcdInitDxe
EHCI @0xFF340000 + OHCI @0xFF350000, CRU clocks + PHY init.

## 6. Boot Flow

```
 1. U-Boot SPL → DDR init
 2. U-Boot proper → CRU/PMIC/IOMUX/VOPB/DSI
 3. boot_android → load EDK2 FV @0x00200000
 4. PeilessSec → MMU, exceptions, cache
 5. DXE Core → dispatch drivers
 6. Px30EmmcDxe → HW init @400KHz
 7. MmcDxe → eMMC ident → InitializeEmmcDevice → 52MHz
 8. PartitionDxe → GPT (9 partitions)
 9. FAT → mount filesystems
10. SimpleFbDxe → GOP 640×480
11. AcpiPlatformDxe → 4 tables
12. GpioKeypadDxe → keys
13. BDS → Shell with LOAD_OPTION_ACTIVE, timeout=0
14. Shell → user interaction (exit → UiApp)
```

## 7. Debug

**Log level** (RK3326.dsc.inc):
```c
PcdDebugPrintErrorLevel|0x8000004F
PcdDebugPropertyMask|0xFF
```

**eMMC**: see `docs/px30-emmc-debug.md`. CMD2/9 CRC off, pre-cmd 1ms delay, poll RINTSTS.

**DS-5**: `add-symbol-file Build/.../DEBUG/*.dll 0x1XXXX000`

## 8. Flash

```bash
# fastboot
magiskboot unpack boot.img && cp BL33_AP_UEFI.Fv kernel
magiskboot repack boot.img && upgrade_tool di -b new-boot.img

# dd
dd if=BL33_AP_UEFI.Fv of=/dev/mmcblk0 bs=512 seek=4096
```

## 9. References

| Project | SoC | Value |
|---------|-----|-------|
| RK3399 edk2 | RK3399 | DwEmmcDxe arch, PrePi boot |
| RK3588 edk2 | RK3588 | ACPI 9-table + ASL |
| RK356x edk2 | RK3566/68 | DSDT ASL, FdtPlatformDxe |
| U-Boot 6.1 BSP | PX30 | CRU/GRF, dwmmc driver |
| Linux PX30 DTS | PX30 | Clock tree, bases |
