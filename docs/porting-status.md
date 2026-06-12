# RK3326 (PX30) EDK2 — 移植状态

## 当前状态

✅ **全部核心功能工作** — 480×640 MIPI DSI 竖屏设备上运行标准 UEFI Boot Manager。

## 架构

```
U-Boot 6.1 BSP → EDK2 PeilessSec (PrePi) → DXE → BDS → UiApp / Shell
```

VOPB/DSI 由 U-Boot 初始化，EDK2 通过 VOPB WIN1 寄存器动态读取帧缓冲地址。

## 功能状态

| 功能 | 状态 | 备注 |
|------|------|------|
| Serial UART2 | ✅ | 0xFF160000, 115200 8N1 |
| eMMC (DWMMC v2.70a) | ✅ | 原生驱动，GPT/FAT32 读写 |
| USB2 (EHCI/OHCI) | ✅ | USB 键盘、Mass Storage |
| Display (GOP) | ✅ | 物理 480×640 → 软件旋转变为 640×480 |
| UiApp Boot Manager | ✅ | 标准 UEFI 菜单，按键可操作 |
| UEFI Shell | ✅ | 内置，可从 UiApp 启动 |
| GPIO 按键 | ✅ | PB5→UP, PB7→ENTER |
| SARADC 按键 | ⚠️ | 驱动加载，ADC 返回 0（时钟未完成） |
| Variable 存储 | ✅ | 模拟 NVRAM |

## 关键修改

### 显示旋转 (SimpleFbDxe)

物理 MIPI DSI 面板为 480×640 竖屏。EDK2 UiApp 需要 ≥640px 水平分辨率。
`SimpleFbDxe` 在 Blt 函数中做 90° CCW + X翻转 软件旋转：

```
GOP 上报: 640×480 横屏
物理帧缓冲: 480×640 竖屏
映射: logical(x,y) → physical[(639-x)][y]
```

### 启动流程 (PlatformBootManagerLib)

修改 `ArmPkg/Library/PlatformBootManagerLib/PlatformBm.c`：
- 跳过 `BootDiscoveryPolicyHandler()`（不自动发现 eMMC/USB 启动项）
- 跳过 UEFI Shell 自动注册
- BDS 找不到启动项 → `PlatformBootManagerUnableToBoot` → **直接进入 UiApp 菜单**

补丁: `docs/edk2-platformbm.patch`

### GPIO 按键

`GpioKeypadDxe` — 轮询 GPIO0 PB5 (SWL→方向键上) 和 PB7 (SWR→ENTER)，
通过 `SimpleTextInEx` 协议向系统提供输入。

### eMMC 驱动 (Px30EmmcDxe)

DWMMC v2.70a 原生寄存器驱动，处理已知硬件 quirk（瞬态错误位、CMD6 内联）。
详见 `docs/px30-emmc-debug.md`。

## EDK2 子模块修改 (5 files)

补丁目录: `docs/patches/` | 完整补丁: `docs/edk2-all.patch`

| 补丁 | 文件 | 说明 |
|------|------|------|
| `01-bds-enter-uiapp.patch` | `ArmPkg/.../PlatformBm.c` | 跳过启动发现 → BDS 自动进 UiApp |
| `02-peiless-single-core.patch` | `ArmPlatformPkg/.../PeilessSec.c` | MP Core PPI 改为可选，支持单核 |
| `03-mmc-blockcount-fix.patch` | `EmbeddedPkg/.../MmcBlockIo.c` | BlockCount=0 修复 |
| `04-partition-gpt-mbr.patch` | `MdeModulePkg/.../PartitionDxe/{Gpt,Mbr}.c` | GPT CRC / MBR 0xEE 跳过 |

## 构建

```bash
cd edk2-rk3326
./build.sh DEBUG
# 输出: Build/RK3326EVB/DEBUG_GCC/FV/BL33_AP_UEFI.Fv
```

## 已知问题

1. **SARADC**: 返回 0x00，PCLK_SARADC 时钟初始化可能需修正寄存器偏移
2. **Logo**: Tianocore logo 已嵌入 FV（SECTION RAW），但 `BootLogoEnableLogo` 未能显示
3. **DWMMC Warm-up**: 首次读返回过期数据，每个命令前需预热读（已在驱动中处理）
- 每块 512 字节，一次命令调用
- 对启动性能影响可忽略（UEFI 启动读量小）

## 外围状态

| 外设 | 状态 | 说明 |
|------|------|------|
| UART2 | ✅ 工作 | 串口控制台，115200 |
| GPIO | ❓ 未验证 | 基址已知 (GPIO0-3) |
| I2C | ❌ 未移植 | — |
| SPI Flash | ❌ 未移植 | FSPI 基址 0xFF3A0000 |
| USB | ❌ 未移植 | — |
| Ethernet | ❌ 未移植 | — |
| PMIC (RK809) | ❌ 未移植 | U-Boot 已初始化 |
| eFuse | ❌ 未移植 | — |
| CRU | ⚠️ 部分 | Px30EmmcDxe 内部直接操作 CRU |

## 启动流程

```
1. U-Boot SPL → DDR 初始化, 加载 U-Boot proper
2. U-Boot → 初始化基础外设 (CRU/PMIC/IOMUX), 加载 EDK2 FV
3. EDK2 PeilessSec → 设置异常向量，跳转 DXE Core
4. DXE Core → 加载所有 DXE 驱动
5. Px30EmmcDxe → 初始化 DWMMC, 安装 MMC Host Protocol
6. MmcDxe → 消费 MMC Host Protocol, eMMC 识别, 安装 Block I/O
7. PartitionDxe → 解析分区表
8. FAT → 挂载文件系统
9. BDS → 引导管理器
10. Shell / BOOTAA64.EFI → 用户交互 / OS 加载
```

## 待完成

### 高优先级
- [ ] **创建 EFI 分区**: eMMC 当前是 U-Boot 的 rockchip gpt 布局，需创建 GPT + FAT32 EFI 分区
- [ ] **放入 BOOTAA64.EFI**: 例如 GRUB 或 Linux EFI stub
- [ ] **完整启动测试**: 从 eMMC 加载 OS

### 中优先级
- [ ] **SD 卡支持**: GPIO1 PB1/PB2 为 SDMMC，基址 0xFF370000
- [ ] **验证 SimpleFbDxe**: MIPI DSI 显示输出
- [ ] **恢复 CMD2/CMD9 CRC 检查**: 诊断完成后可重新启用
- [ ] **RK3326Dxe 扩展**: 添加系统初始化 (GICD 配置, 定时器等)

### 低优先级
- [ ] **CMD18/CMD25 性能优化**: 分析是否可通过调整 IDMAC 时序或 phase 设置使多块命令工作
- [ ] **USB 支持**: 大容量存储 / 键盘
- [ ] **Ethernet 支持**: PXE 网络启动
- [ ] **Variable 持久化**: eMMC RPMB 或 SPI Flash 存储 UEFI 变量
- [ ] **固件更新**: Capsule Update 支持

## 构建命令

```bash
./build.sh          # 构建 EDK2
./flash.sh          # 写入 eMMC (通过 U-Boot fastboot 或 ums)
```

## 已知问题

1. **eMMC 缺少 EFI 分区**: U-Boot 分区布局不兼容 EDK2，需要 `gpt write` 或 UEFI Shell 分区工具
2. **GPLL 时钟计算偏差**: PCD 设为 100MHz，实际源 109MHz (GPLL 1200MHz/11)，卡时钟实际 54.5MHz 超 52MHz 规范 ~5%
3. **v2.70a IDMAC 不触发 CMD18**: 硬件限制，当前通过 IsMultiBlock=FALSE 规避
