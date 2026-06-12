# 移植状态

✅ **全部核心功能完成** — Shell 自动启动 + UiApp + ACPI + eMMC 52MHz HS。

→ [English](porting-status.md) · [README](../README_cn.md) · [改动日志](../CHANGELOG_cn.md) · [移植指南](porting-guide_cn.md)

## 架构

```
U-Boot 6.1 → EDK2 @0x00200000 → PeilessSec → DXE → BDS → Shell(自动) → exit → UiApp
```

## 功能状态

| 功能 | 状态 | 备注 |
|------|------|------|
| 串口 UART2 | ✅ | 0xFF160000 |
| eMMC | ✅ | DWMMC v2.70a, 52MHz HS, GPT/FAT32, 9分区 |
| USB2 | ✅ | EHCI+OHCI, 键盘 + Mass Storage |
| 显示 GOP | ✅ | 640×480 (480×640 → 软件旋转) |
| UEFI Shell | ✅ | 自动启动, `exit` → UiApp |
| UiApp 菜单 | ✅ | GPIO 按键可操作 |
| GPIO 按键 | ✅ | PB5→UP, PB7→ESC |
| ACPI | ✅ | FADT+DSDT+MADT+GTDT, 0 错误 |
| 变量存储 | ✅ | 模拟 NVRAM |
| I2C | ⚠️ | I2C0 初始化, 无上层驱动 |
| SARADC | ⚠️ | 驱动加载, ADC=0 |
| Logo | ⚠️ | 已嵌入, 未显示 |

## 关键设计

### 显示 — SimpleFbDxe
```
物理: 480×640 竖屏 → GOP: 640×480 横屏
旋转: Blt() 中 90°CCW + X-flip
映射: logical(x,y) → physical[(639-x)][y]
```

### 启动 — PlatformBm.c
跳过 `BootDiscoveryPolicyHandler()`, Shell 注册 `LOAD_OPTION_ACTIVE`, timeout=0。

### eMMC 高速模式
```
CMD6(HS_TIMING=1) → SetIos(52MHz, 8-bit) → CMD6(BUS_WIDTH)
```

### ACPI
```
XSDT → FACP (HW_REDUCED, PSCI) → X_DSDT → DSDT
     → MADT (GICv2, 1×GICC)
     → GTDT (GSIV=30)
```

### 性能优化

| 项目 | 优化前 | 优化后 |
|------|--------|--------|
| 命令延迟 | 15ms | 1ms |
| CMD1 重试 | 1000 | 200 |
| 轮询超时 | 10s | 2.5s |
| eMMC 时钟 | 400KHz | **52MHz** |

## EDK2 补丁 (4 补丁, 5 文件)

| # | 文件 | 修改 |
|---|------|------|
| 1 | `PlatformBm.c` | Shell 自动启动 |
| 2 | `PeilessSec.c` | 单核支持 |
| 3 | `MmcBlockIo.c` | BlockCount=0 |
| 4 | `Gpt.c`+`Mbr.c` | CRC/0xEE 跳过 |

→ 合并: [`edk2-all.patch`](edk2-all.patch) · 分项: [`patches/`](patches/)

## 自研驱动 (10)

`Px30EmmcDxe` · `SimpleFbDxe` · `AcpiPlatformDxe` · `GpioKeypadDxe` · `AdcKeypadDxe` · `UsbHcdInitDxe` · `I2cDxe` · `RK3326Dxe` · `FdtDxe` · `MmcDxe`

## 构建

```bash
cd edk2-rk3326 && ./build.sh DEBUG
# → Build/RK3326EVB/DEBUG_GCC/FV/BL33_AP_UEFI.Fv
```

## 已知问题

1. **SARADC**: 返回 0, 时钟未完成
2. **Logo**: 已嵌入但未显示
3. **CMD18**: IDMAC bug, `IsMultiBlock=FALSE` 规避
4. **GPLL**: 100MHz vs 109MHz, eMMC ~54.5MHz

## 待完成

**高**: OS 启动测试, GRUB/Linux EFI stub  
**中**: SD 卡, SARADC 修复, Logo 显示  
**低**: CMD18/25, 持久化 Variable, Capsule Update
