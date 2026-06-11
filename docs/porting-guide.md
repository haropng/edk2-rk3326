# RK3326 (PX30) EDK2 移植指南

> 从零移植 EDK2 到 Rockchip RK3326/PX30 — 480×640 MIPI DSI 竖屏设备

## 目录

1. [概述](#概述)
2. [前置准备](#前置准备)
3. [最小启动](#最小启动)
4. [串口驱动](#串口驱动)
5. [MMU 和内存映射](#mmu-和内存映射)
6. [GOP 显示驱动](#gop-显示驱动)
7. [U-Boot 配合修改](#u-boot-配合修改)
8. [GraphicsConsole 窄屏适配](#graphicsconsole-窄屏适配)
9. [调试方法](#调试方法)
10. [参考移植版本对比](#参考移植版本对比)

---

## 概述

RK3326 (PX30) 是 Rockchip 低功耗 SoC：4×Cortex-A35、GIC-400、MIPI DSI 显示。

### 移植架构

```
                    U-Boot 6.1 BSP
                         │
                    boot_android → bootm (DO RELOCATE)
                         │
                    ┌────▼────┐
                    │  EDK2   │
                    │ SEC/PEI │  ← ARM64 kernel header
                    │   DXE   │  ← SimpleFbDxe, RK3326Dxe, ...
                    │   BDS   │  ← UEFI Shell
                    └─────────┘
```

### 关键移植原则

1. **最小修改 EDK2 上游代码** — 只改必需的 (GraphicsConsole 窄屏修复)
2. **跟随已有移植模式** — RK3399 的 FrameBufferBltLib 是最佳参考
3. **只读 VOPB 寄存器** — PX30 VOPB 的 CFG_DONE 机制未知，不要写
4. **U-Boot 负责硬件初始化** — 显示/时钟/PLL 都在 U-Boot 中配好

---

## 前置准备

### 需要的文件

| 来源 | 内容 |
|------|------|
| `6.1-rksdk/u-boot/drivers/video/drm/` | PX30 VOPB 寄存器定义 (`rockchip_vop_reg.h`) |
| `6.1-rksdk/u-boot/configs/rk3326_defconfig` | U-Boot 编译配置 |
| `rk3326-appolo.dts` | 面板时序 (33000kHz, 480×640, DSI RGB888) |
| `edk2-rk3588/devicetree/.../px30-cru.h` | CRU 时钟 ID 定义 |
| `rk3399-edk2/` | RK3399 SimpleFbDxe 参考实现 |

### 参考平台 (按复杂度排序)

| 平台 | SoC | 显示 | SimpleFbDxe 模式 | 推荐参考 |
|------|-----|------|-----------------|---------|
| **RK3399** | SDM845 | 不需要 | PCD + FrameBufferBltLib | ⭐⭐⭐ Blt 模式 |
| **RK3576** | RK3576 | VOP2 | VOP 寄存器 + 手动 Blt | ⭐⭐ 寄存器读取 |
| **RK3588** | RK3588 | VOP2 | 完整 DisplayDxe | ⭐ CRU/时钟 |
| **RK356x** | RK356x | VOP2 | 完整 DisplayDxe | ⭐ |

---

## 最小启动

### ARM64 内核头

EDK2 FD 镜像作为 U-Boot `bootm` 的 payload，需要 ARM64 kernel header：

```c
// 在 FDF 中
0x00000000|0x00000038
DATA = {
  0x00, 0x00, 0xA0, 0xE1,  // NOP (ARM32)
  0x00, 0x00, 0xA0, 0xE1,  // NOP
  ...
  0x05, 0x00, 0x00, 0x14,  // B offset to entry
  0x00, 0x00, 0x00, 0x00,  // text_offset = 0
  0x00, 0x00, 0x00, 0x00,  // image_size = 0
  0x00, 0x00, 0x00, 0x00,  // flags = 0
  0x00, 0x00, 0x00, 0x00,  // res2 = 0
  0x00, 0x00, 0x00, 0x00,  // res3 = 0
  0x00, 0x00, 0x00, 0x00,  // res4 = 0
  0x41, 0x52, 0x4D, 0x64,  // magic = "ARM\x64"
  0x00, 0x00, 0x00, 0x00,  // res5 = 0
}
```

`text_offset=0` 告诉 U-Boot 不需要重定位入口点。

### Minimal DSC

```ini
[Defines]
  PLATFORM_NAME                  = RK3326Evb
  DSC_SPECIFICATION              = 0x0001001B
  OUTPUT_DIRECTORY               = Build/RK3326EVB
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses.common]
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  # ... 最小集合: 42 个驱动就够了

!include MdePkg/MdeLibs.dsc.inc
```

关键是**自包含 DSC** — 不引用其他平台的 `.dsc.inc`（那些文件包含 GICv3、PCIe、200+ 驱动等多余内容）。

---

## 串口驱动

### 核心问题

EDK2 在 **每个 DXE 驱动加载时** 调用 `SerialPortInitialize()` 和 `SerialPortSetAttributes()`。如果不加保护，这些调用会重置 U-Boot 已经初始化好的 UART 寄存器 (IER/FCR/LCR)，导致 **RX 中断**。

### 解决方案

```c
// SerialPortLib.c
RETURN_STATUS EFIAPI SerialPortInitialize(VOID) {
  return RETURN_SUCCESS;  // 不动 UART 硬件
}

RETURN_STATUS EFIAPI SerialPortSetAttributes(...) {
  return RETURN_SUCCESS;  // 不动 UART 硬件
}
```

UART2 基址 `0xFF160000`，**不需要加偏移** (DW APB UART 的标准 `reg-shift=2` 由 `SerialPortWriteRegister` 内部处理)。

**波特率**: 1,500,000 (U-Boot 预设，不需要改)。

---

## MMU 和内存映射

### OP-TEE 保留区

U-Boot 为 OP-TEE 保留 `0x08400000-0x08C00000`：

```c
// PlatformLib.c — 在内存映射中跳过
ARM_MEMORY_REGION_DESCRIPTOR mMemoryTable[] = {
  { 0x00200000, 0x08200000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },
  // OP-TEE hole: 0x08400000-0x08C00000 SKIPPED
  { 0x08C00000, 0x37000000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },
  // Peripheral space: Device memory
  { 0xF8000000, 0x08000000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
};
```

### PcdSystemMemorySize

保守设置为 **496MB**（板子有 502MB，留余量防止访问超界）。

---

## GOP 显示驱动

### 开发历程 (教训)

| 版本 | 方案 | 结果 | 根因 |
|------|------|------|------|
| 1 | shadow 32→24 Flush | 花屏 | 自分配 buffer 重定向 VOPB，IOMMU 阻断 |
| 2 | 32bpp 重配 VOPB | 黑屏 | WIN1_YRGB_MST 写不进去 (CFG_DONE 机制未知) |
| 3 | U-Boot 原始 buffer + Flush | 花屏→灰屏 | stride/bpp 逐步匹配，颜色字节序混乱 |
| 4 | U-Boot 32bpp + 全屏 | 花屏→彩块→花屏 | 帧缓冲被 UEFI 内存分配覆盖！ |
| 5 | +AllocatePages 保护 | ✅ 稳定 | **根因找到** |
| 6 | +FrameBufferBltLib | ✅ | 消除手动 Blt bug |

**关键教训：帧缓冲必须用 `AllocatePages(AllocateAddress)` 保护！**

```c
EFI_PHYSICAL_ADDRESS fb = FbAddr;
gBS->AllocatePages(AllocateAddress, EfiReservedMemoryType,
                   EFI_SIZE_TO_PAGES(Sz), &fb);
```

没有这行，UEFI 的 DXE Core 可能会把帧缓冲地址分配给其他驱动，导致随机花屏。

### 最终架构

```
SimpleFbDxeInitialize()
  ├── 1. 启用 CRU 时钟 (VOPB + DSI)
  ├── 2. 读 VOPB WIN1_YRGB_MST (0xFF4600A0) → fb 地址
  ├── 3. 读 VOPB WIN1_VIR (0xFF460098) → 宽度
  ├── 4. AllocatePages(AllocateAddress) → 保护 fb
  ├── 5. FrameBufferBltConfigure → BltLib
  ├── 6. 清屏到黑色 (BltLib VideoFill)
  ├── 7. 安装 GOP 协议
  └── GOP->Blt = FrameBufferBlt + WriteBackInvalidateDataCacheRange
```

### VOPB 寄存器 (PX30/RK3366_LIT 布局)

```
Base: 0xFF460000

Offset  Name            Value (U-Boot 6.1)  Notes
──────────────────────────────────────────────────
0x00    REG_CFG_DONE    0x00000000          写 1 触发配置更新
0x18    SYS_CTRL2       0x00000000          bit1=standby
0x20    DSP_CTRL0       ?                   bit0=RGB, bit8=HDMI, bit16=LVDS
0x40    WIN0_MST        0x00000000          WIN0 禁用
0x48    WIN0_VIR        0x00000000
0x4C    WIN0_YRGB_MST   0x00000000
0x90    WIN1_MST        0x00000001          fmt=0(ARGB8888), enabled
0x98    WIN1_VIR        480                 stride (像素)
0xA0    WIN1_YRGB_MST   0x1DE00000          fb 基址
0xA4    WIN1_ACT_INFO   0x027F01DF          width=480, height=640
0xA8    WIN1_DSP_ST     0x00000000          显示起始位置
```

### CRU 时钟

```c
// px30-cru.h clock IDs → CRU gate registers
#define GATE_ON(cru, clk_id)  \
  do { UINT32 r=(cru)+0x100+((clk_id)/16)*4, m=1U<<((clk_id)%16); \
       MmioWrite32(r, (m<<16)|m); } while(0)

GATE_ON(cru, 174);  // ACLK_VO_PRE
GATE_ON(cru, 243);  // HCLK_VO_PRE
GATE_ON(cru, 322);  // PCLK_VO_PRE
GATE_ON(cru, 181);  // ACLK_VOPB
GATE_ON(cru, 251);  // HCLK_VOPB
GATE_ON(cru, 150);  // DCLK_VOPB
GATE_ON(cru, 324);  // PCLK_MIPI_DSI
GATE_ON(cru, 325);  // PCLK_MIPIDSIPHY
```

Rockchip 写使能: bits[31:16] = 写掩码, bits[15:0] = 值。

---

## U-Boot 配合修改

### 为什么需要改 U-Boot

U-Boot 原始行为：
- `src_rect` = logo 图片尺寸 (不是面板尺寸)
- `crtc_rect` = 居中或缩放 (不是 1:1)
- `dma_addr` = logo buffer + offset (有偏移)
- Buffer 大小 = logo 图片尺寸 (可能 < 面板)

UEFI 需要：
- `src_rect` = 面板全分辨率 (480×640)
- `crtc_rect` = 1:1 满屏 (无缩放)
- `dma_addr` = 帧缓冲起始 (无偏移)
- Buffer ≥ 480×640×4 = 1,228,800 bytes
- bpp = 32 (VOPB ARGB8888 匹配 GOP)

### 修改位置

`6.1-rksdk/u-boot/drivers/video/drm/rockchip_display.c` — 3 处：

1. `display_logo()` — src_rect + crtc_rect + dma_addr
2. `display_bmp()` — 同上
3. `load_bmp_logo()` — 最小 buffer

完整 patch: `docs/rockchip_display.patch`

---

## GraphicsConsole 窄屏适配

### 问题

480px / 8px 字体 = 60 列 < UEFI 规范要求的 80 列。

原始代码：
```c
ASSERT(MaxColumns >= 80);  // 调试版崩溃！
DeltaX = (480 - 80*8) >> 1;  // = -80 → 下溢为 0xFFFFFFB0 → HiiDatabase 越界
```

### 修复

```c
// 1. 移除 ASSERT，窄屏仅打 WARN
if (MaxColumns < 80)
  DEBUG((DEBUG_WARN, "narrow screen: only %d cols\n", MaxColumns));

// 2. DeltaX 使用有符号计算，钳位负值
INT32 dx = ((INT32)480 - (INT32)(80*8)) >> 1;  // = -40
NewModeBuffer[0].DeltaX = (dx < 0) ? 0 : (UINT32)dx;  // = 0

// 3. 始终提供 ≥2 modes
if (宽屏) { 80×25 + 80×50 }
else       { 80×25 + 80×25 }  // 窄屏第二个mode也照塞
```

完整 patch: `docs/GraphicsConsole.patch`

### 已知限制

UiApp (BootManagerMenu) 在 480px 下崩溃 — HiiDatabase 的 Forms 布局缓冲区计算不支持窄屏。绕开：

```ini
# 直接启动 Shell，跳过 UiApp
gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x00, ... }
gArmTokenSpaceGuid.PcdUefiShellDefaultBootEnable|TRUE
```

完整支持需要 **窄字体**（≤6px）使 80 列适配 480px。

---

## 调试方法

### 串口调试

```bash
# 1.5Mbaud 连接
screen /dev/ttyUSB0 1500000
```

### 关键 DEBUG 宏

```c
DEBUG((DEBUG_INFO, "SimpleFbDxe: fb=0x%X %dx%d\n", Addr, Width, Height));
DEBUG((DEBUG_INFO, "Blt #%d op=%d xy=(%d,%d) %dx%d\n", cnt, op, x, y, w, h));
```

### 帧缓冲验证

```c
// 写红色 → 读回 → 比较
UINT32 *p = (UINT32*)fb;
for (i=0; i<n; i++) p[i] = 0x00FF0000;  // BGRA red
WriteBackInvalidateDataCacheRange(fb, size);
// 读回验证
volatile UINT8 *vp = (volatile UINT8*)fb;
for (i=0; i<width*4; i++)
  if (vp[i] != expected[i%4]) mismatches++;
```

### 常见问题

| 症状 | 可能原因 | 排查 |
|------|---------|------|
| 花屏 | fb 被覆盖/stride 不匹配 | `AllocatePages` 保护 + `mismatches` 检查 |
| 黑屏 | VOPB standby/clocks gated | 读 `SYS_CTRL2` + 启 CRU gates |
| 纯灰 | 格式匹配但颜色错 | 检查 PixelFormat 和 VOPB fmt 位 |
| 分屏 (3/4-1/4) | WIN1_DSP_ST 偏移 | 读取 `0xFF4600A8` |
| HiiDatabase 崩溃 | 窄屏 80 列下溢 | 修复 GraphicsConsole DeltaX |

---

## 参考移植版本对比

| | RK3399 | RK3576 | **RK3326 (本移植)** |
|---|---|---|---|
| **SoC** | SDM845 | RK3576 | PX30 |
| **显示** | MDP5 | VOP2 | VOPB (RK3366_LIT) |
| **分辨率** | 1080×1920 | HDMI | 480×640 DSI |
| **Blt** | FrameBufferBltLib | 手动 & 0xFFFFFF | **FrameBufferBltLib** |
| **fb 来源** | PCD 固定地址 | VOP 寄存器 | **VOP 寄存器 + PCD fallback** |
| **Cache** | WBI full frame | 无 | **WBI full frame** |
| **fb 保护** | 无 (PCD 预保留) | 无 (自分配) | **AllocatePages** |
| **时钟** | 不需要 | 不需要 | **CRU GATE/RST** |
| **VOPB 写入** | N/A | WIN0_YRGB_MST | **只读** |
| **Console** | 80+ 列 | 80+ 列 | **60 列 (窄屏修复)** |
