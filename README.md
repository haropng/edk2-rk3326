# RK3326 (PX30) EDK2 UEFI Firmware

UEFI firmware for Rockchip RK3326/PX30 — 4×Cortex-A35, GIC-400 (GICv2).  
Boots via Android boot flow → UEFI Shell. Serial console only.

**Status**: Fully functional via serial (1,500,000 baud 8N1).

## Hardware

- **SoC**: Rockchip RK3326 (PX30)
- **CPU**: 4× Cortex-A35 @ up to 1.5 GHz
- **GIC**: GIC-400 (GICv2) @ 0xFF131000/0xFF132000
- **DRAM**: LPDDR3, 502MB on EVB
- **UART**: DW APB UART2 @ 0xFF160000, 1.5Mbaud
- **eMMC**: dwmmc @ 0xFF390000 | **SD**: dwmmc @ 0xFF370000
- **Display**: MIPI DSI 480×640 portrait (VOPB @ 0xFF460000)
- **PMIC**: RK817 @ I2C0 0x20

## Boot Flow

```
BootROM → U-Boot TPL/SPL → boot_android → bootm
       → DO RELOCATE → EDK2 entry (0x00200000)
       → PeilessSec → DXE → BDS → UEFI Shell
```

## Building

Prerequisites (in OrbStack/Linux): gcc-aarch64-linux-gnu, python3, uuid-dev

```bash
./build.sh DEBUG      # Debug build with full logging
./build.sh RELEASE    # Release build (quiet)
./build.sh clean      # Clean Build/ and Conf/
```

Output: `Build/RK3326EVB/DEBUG_GCC/FV/NOR_FLASH_IMAGE.fd` (2MB)

## Flashing

```bash
magiskboot unpack boot.img
cp NOR_FLASH_IMAGE.fd kernel
magiskboot repack boot.img new-boot.img
upgrade_tool di -b new-boot.img
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
│       ├── RK3326.dsc.inc     # Self-contained platform DSC (42 components)
│       ├── RK3326.fdf         # FD layout (2MB, ARM64 header @ offset 0)
│       ├── Include/
│       │   ├── Soc.h          # MMIO map (verified from px30.dtsi)
│       │   └── VarStoreData.h
│       └── Library/
│           ├── PlatformLib/   # ArmPlatformLib + MMU memory map
│           ├── MemoryInitPeiLib/ # ARM memory init (generic)
│           ├── SerialPortLib/ # DW APB UART (TX+RX, preserves U-Boot config)
│           ├── SdramLib/      # DMC-based DRAM detection
│           ├── CruLib/        # CRU clock/reset
│           ├── GpioLib/       # GPIO read/write
│           └── OtpLib/        # OTP chip version
│       └── Drivers/
│           ├── RK3326Dxe/     # SoC init
│           ├── FdtDxe/        # DTB loading from FV
│           └── SimpleFbDxe/   # GOP framebuffer (PENDING VOPB init)
├── configs/
│   └── rk3326-evb-lp3-v10.conf
├── rk3326-appolo.dts          # Device DTS reference
└── .gitignore
```

## Key Design Decisions

**Self-contained DSC** — Not using shared `Rockchip.dsc.inc` (RK3588 target). That file has ArmGicV3, 200+ drivers, and requires extensive gating. Our DSC declares exactly what RK3326 needs.

**SerialPortLib no-op** — SerialPortInitialize and SerialPortSetAttributes return SUCCESS without touching UART hardware. EDK2 calls these from every DXE driver; each call would otherwise reset UART registers and break RX.

**Memory map with OP-TEE hole** — U-Boot reserves 0x08400000-0x08C00000 for OP-TEE. The MMU map explicitly skips this region.

**PcdSystemMemorySize=496MB** — Board has 502MB DRAM. Conservative cap avoids accessing beyond physical memory.

**Android boot flow** — Uses U-Boot `boot_android` → `bootm` with DO RELOCATE. ARM64 kernel header (`ARM\x64` magic + `text_offset=0`) at FD offset 0x38 tells U-Boot this is a valid ARM64 boot image.

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
| GOP Framebuffer | ⚠️ Pending VOPB+DSI init |
| eMMC/SD block devices | ❌ Needs hardware driver port |
| Display (MIPI DSI) | ❌ PX30 VOPB needs full HW init |
| USB | ❌ |
| GMAC | ❌ |
| Multi-core (PSCI) | ❌ Single-core only |

## Credits

Based on analysis of: RK3588 (edk2-rockchip shared infra), RK3576 (GICv2 pattern), RK356x (minimal DSC pattern), RK3399 (ARM64 header / booti pattern).

Copyright (c) 2024–2026. SPDX: BSD-2-Clause-Patent.
