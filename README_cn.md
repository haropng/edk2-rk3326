# RK3326 (PX30) EDK2 UEFI

Rockchip RK3326/PX30 的 UEFI 固件 — 4×Cortex-A35, 480×640 MIPI DSI 竖屏。

**状态**: ✅ 全部核心功能完成。

[English](README.md) · [改动日志](CHANGELOG_cn.md) · [移植状态](docs/porting-status_cn.md) · [移植指南](docs/porting-guide_cn.md)

## 快速开始

```bash
cd edk2-rk3326 && ./build.sh DEBUG
# → Build/RK3326EVB/DEBUG_GCC/FV/BL33_AP_UEFI.Fv
```

## 启动流程

```
U-Boot → EDK2 @0x00200000 → PeilessSec → DXE → BDS → Shell(自动) → exit → UiApp
```

## 功能

| 功能 | 状态 | 备注 |
|------|------|------|
| 显示 | ✅ | GOP 640×480 (480×640 软件旋转) |
| eMMC | ✅ | DWMMC v2.70a @52MHz HS, GPT/FAT32 |
| UEFI Shell | ✅ | 自动启动, `exit` → UiApp |
| UiApp 菜单 | ✅ | GPIO 按键可操作 |
| GPIO 按键 | ✅ | UP + ESC (SimpleTextInEx) |
| ACPI | ✅ | FADT+DSDT+MADT+GTDT (0 错误) |
| USB2 | ✅ | EHCI+OHCI, 键盘 + Mass Storage |
| 变量存储 | ✅ | 模拟 NVRAM |

## 硬件

| 组件 | 地址 | 备注 |
|------|------|------|
| SoC | RK3326 (PX30) | 4×A35, GIC-400 |
| UART2 | 0xFF160000 | 115200 8N1 |
| eMMC | 0xFF390000 | DWMMC v2.70a |
| 显示 | 0xFF460000 | MIPI DSI 480×640 |
| GICD/GICC | 0xFF131000/0xFF132000 | GICv2 |

## 关键设计

- **显示**: `SimpleFbDxe` 90°CCW+X-flip Blt()
- **启动**: Shell 注册 `LOAD_OPTION_ACTIVE`, 跳过设备发现
- **eMMC**: `EmbeddedPkg/MmcDxe` `InitializeEmmcDevice` 400KHz→52MHz
- **ACPI**: 4 张表, 70 字节手写 DSDT AML

## 文档

| 文档 | 内容 |
|------|------|
| [`README.md`](README.md) | English |
| [`CHANGELOG_cn.md`](CHANGELOG_cn.md) | 改动日志 |
| [`docs/porting-status_cn.md`](docs/porting-status_cn.md) | 移植状态 |
| [`docs/porting-guide_cn.md`](docs/porting-guide_cn.md) | 移植指南 |
| [`docs/px30-emmc-debug.md`](docs/px30-emmc-debug.md) | eMMC 调试记录 |
| [`docs/edk2-all.patch`](docs/edk2-all.patch) | 全部 EDK2 补丁 |
| [`docs/patches/`](docs/patches/) | 分项补丁 |
