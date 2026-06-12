# 移植指南

> 从零移植 EDK2 到 RK3326/PX30 — 480×640 MIPI DSI 竖屏设备。

→ [English](porting-guide.md) · [README](../README_cn.md) · [移植状态](porting-status_cn.md)

## 1. 概述

RK3326 (PX30): 4×Cortex-A35, GIC-400, DWMMC v2.70a, MIPI DSI。
U-Boot 初始化 VOPB/DSI；EDK2 通过 VOPB WIN1 寄存器读取帧缓冲地址。

## 2. 构建

```bash
export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
cd edk2-rk3326 && ./build.sh DEBUG
```

## 3. 目录结构

```
edk2-rk3326/
├── build.sh
├── edk2/                         # EDK2 上游子模块
├── edk2-rockchip/Silicon/Rockchip/RK3326/
│   ├── RK3326.dsc.inc            # 平台 DSC
│   ├── RK3326.fdf                # Flash 布局
│   └── Drivers/                  # 自研驱动 (10)
│       ├── Px30EmmcDxe/          # eMMC DWMMC v2.70a
│       ├── SimpleFbDxe/          # GOP + 软件旋转
│       ├── AcpiPlatformDxe/      # ACPI 4 表
│       ├── GpioKeypadDxe/        # GPIO 按键
│       └── ...                   # 6 个其他驱动
├── docs/
└── CHANGELOG.md
```

## 4. EDK2 补丁 (4 补丁, 5 文件)

| # | 文件 | 修改 |
|---|------|------|
| 1 | `PlatformBm.c` | Shell 自动启动, 跳过设备发现 |
| 2 | `PeilessSec.c` | MP Core PPI 可选 |
| 3 | `MmcBlockIo.c` | BlockCount=0 修复 |
| 4 | `Gpt.c`+`Mbr.c` | GPT CRC 跳过 + MBR 0xEE 跳过 |

```bash
cd edk2 && git apply ../docs/edk2-all.patch
```

## 5. 关键驱动

### 5.1 eMMC — Px30EmmcDxe
DWMMC v2.70a @0xFF390000: 直接寄存器访问, 52MHz HS, 1ms 延迟, v2.70a quirks。

### 5.2 显示 — SimpleFbDxe
物理 480×640 → GOP 640×480, Blt() 中 90°CCW + X-flip。

### 5.3 ACPI — AcpiPlatformDxe
4 张表: FADT (HW_REDUCED+PSCI) → DSDT (CPU0), MADT (GICv2), GTDT (GSIV=30)。
70B 手写 AML, `InstallAcpiTable` 自动链接。

### 5.4 GPIO 按键 — GpioKeypadDxe
PB5→UP(0x01), PB7→ESC(0x17), 50ms 轮询, SimpleTextInEx。

### 5.5 USB 主机 — UsbHcdInitDxe
EHCI @0xFF340000 + OHCI @0xFF350000, CRU 时钟 + PHY 初始化。

## 6. 启动流程

```
 1. U-Boot SPL → DDR 初始化
 2. U-Boot proper → CRU/PMIC/IOMUX/VOPB/DSI
 3. boot_android → 加载 EDK2 FV @0x00200000
 4. PeilessSec → MMU, 异常向量, Cache
 5. DXE Core → 调度驱动
 6. Px30EmmcDxe → 硬件初始化 @400KHz
 7. MmcDxe → eMMC 识别 → InitializeEmmcDevice → 52MHz
 8. PartitionDxe → GPT (9 分区)
 9. FAT → 挂载文件系统
10. SimpleFbDxe → GOP 640×480
11. AcpiPlatformDxe → 4 张表
12. GpioKeypadDxe → 按键
13. BDS → Shell (LOAD_OPTION_ACTIVE), timeout=0
14. Shell → 用户交互 (exit → UiApp)
```

## 7. 调试

**日志级别** (RK3326.dsc.inc):
```c
PcdDebugPrintErrorLevel|0x8000004F
PcdDebugPropertyMask|0xFF
```

**eMMC**: 详见 `docs/px30-emmc-debug.md`。CMD2/9 CRC 关闭, 命令前 1ms 延迟, 轮询 RINTSTS。

**DS-5**: `add-symbol-file Build/.../DEBUG/*.dll 0x1XXXX000`

## 8. 刷写

```bash
# fastboot
magiskboot unpack boot.img && cp BL33_AP_UEFI.Fv kernel
magiskboot repack boot.img && upgrade_tool di -b new-boot.img

# dd
dd if=BL33_AP_UEFI.Fv of=/dev/mmcblk0 bs=512 seek=4096
```

## 9. 参考资料

| 项目 | SoC | 参考价值 |
|------|-----|---------|
| RK3399 edk2 | RK3399 | DwEmmcDxe 架构, PrePi 启动 |
| RK3588 edk2 | RK3588 | ACPI 9 表 + ASL |
| RK356x edk2 | RK3566/68 | DSDT ASL, FdtPlatformDxe |
| U-Boot 6.1 BSP | PX30 | CRU/GRF, dwmmc 驱动 |
| Linux PX30 DTS | PX30 | 时钟树, 外设基址 |
