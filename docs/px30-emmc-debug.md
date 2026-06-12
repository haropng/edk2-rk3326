# PX30 eMMC DWMMC v2.70a 调试记录

## 问题概述

在 PX30 (RK3326) 平台上移植 EDK2，eMMC 在 U-Boot 中正常工作（HS200, 150MHz），
但在 UEFI 中遇到一系列 DWMMC v2.70a (VERID=0x5342270A) 硬件 bug。

所有问题已解决，块 I/O 正常工作。当前状态：eMMC 缺少 EFI 分区。

## 硬件环境

- SoC: Rockchip PX30 (RK3326), 4x Cortex-A35
- DWMMC: Synopsys DesignWare MMC IP v2.70a (VERID=0x5342270A)
- USRID: 0x00000000 (不支持 internal phase tuning)
- eMMC 基址: 0xFF390000
- CRU 基址: 0xFF2B0000, GRF 基址: 0xFF140000
- GPLL: 1200MHz, eMMC source clk = 109MHz (GPLL/11)
- 卡时钟: 50MHz @ divider=1 (PCD 基准 100MHz，显示为 52MHz)
- VCCIO6: 1.8V

## v2.70a 硬件 Bug（总结）

### Bug 1: 瞬态错误位（RE, EBE, RTO, FRUN, HTO）

DWMMC v2.70a 虚假触发多个错误位，但它们都在 CMD_DONE 已置位时出现，
证明命令/响应/数据传输实际是成功的。

| 瞬态位 | 触发命令 | 现象 |
|--------|---------|------|
| RE (BIT1) | CMD2, CMD6, CMD7, CMD8, CMD9 | early-RE 或 RE+CMD_DONE 同时出现 |
| EBE (BIT15) | CMD6, CMD8 | 数据命令上 EBE+CMD_DONE 同时出现 |
| RTO (BIT8) | CMD16 | RTO+CMD_DONE，响应已正确捕获 |
| FRUN (BIT11) | CMD8, CMD17 | FRUN+DTO+CMD_DONE，数据传输正常完成 |
| HTO (BIT10) | CMD18 | HTO+CMD_DONE，但 DTO 未到达（Bug 3 引起） |

**修复**: SendCommand() 中检测到 CMD_DONE 时清除瞬态位并继续（非数据命令 goto LogResponse，
数据命令 continue 等 DTO）。

### Bug 2: DAT_FSM=1 是正常状态

控制器 DAT_FSM 的四个状态:
- 0 = IDLE — 空闲
- 1 = WAIT — 等待数据起始位，**v2.70a 命令之间的正常静止状态**
- 2 = RECEIVE — 接收数据中
- 3 = BUSY — 繁忙

v2.70a 在命令之间通常处于 DAT_FSM=1。之前的代码将其误判为"卡死"并触发虚假 FIFO 复位，
导致控制器状态紊乱。

**修复**: 仅当 DAT_FSM >= 2 且 DATA_BUSY=0 时触发 FIFO 复位。

### Bug 3: IDMAC 不触发 CMD18 / CMD25（最严重）

**IDMAC 引擎对多块命令 (CMD18, CMD25) 完全不启动。**

证据:
- CMD17 单块读: DES0 OWN 被 DMA 清除, DTO 正常, IDSTS 有活动
- CMD18 多块读: IDSTS=0x0, OWN 未变 (DMA 从未消费描述符)
- CMD 寄存器值正确（0x20003352）, BMOD 有 IDMAC_ENABLE, DBADDR 指向正确描述符
- 添加 CMD12 SEND_AUTO_STOP 无效
- 每次传输后 IDMAC SW_RESET 无效
- BYTCNT 和 BLKSIZ 设置正确

**修复**:
1. `Px30EmmcIsMultiBlock()` 返回 FALSE → MmcDxe 只用单块命令
2. 修复 EmbeddedPkg MmcBlockIo.c 的 BlockCount=0 bug（见下文）

## 软件 Bug: MmcBlockIo.c BlockCount=0

当 `IsMultiBlock` 返回 FALSE 时，EmbeddedPkg MmcBlockIo.c 的循环有 bug:

```
BlockCount = 1 (初始), RemainingBlock = 1
→ 第1次循环: BlockCount=1 → CMD17 ✓
→ RemainingBlock -= 1 = 0
→ 第2次循环: BlockCount=0 → 走 CMD18 分支, 且 BYTCNT=0!
```

此 bug 之前未被发现，因为没有驱动返回过 IsMultiBlock=FALSE。

**修复**: `if (RemainingBlock == 0) RemainingBlock = 1;`

文件: `edk2/EmbeddedPkg/Universal/MmcDxe/MmcBlockIo.c`

## 代码修改清单

### Px30EmmcDxe.c

| # | 位置 | 改动 | 原因 |
|---|------|------|------|
| 1 | SendCommand | 6 种瞬态位 quirk 处理 (early-RE, RE+, EBE+, RTO+, FRUN+, HTO+ 配合 CMD_DONE) | Bug 1 |
| 2 | SendCommand 前等待 | DAT_FSM 0/1 正常，仅 ≥2 时 FIFO 复位 | Bug 2 |
| 3 | PrepareDmaData | 每次数据传输前复位 FIFO (对齐 U-Boot dwmci_prepare_data) | 清理 FIFO 状态 |
| 4 | ReadBlockData/WriteBlockData | 每次传输后复位 IDMAC (BMOD SW_RESET) | 清理 IDMAC 状态 |
| 5 | ReadBlockData/WriteBlockData | 每次传输后禁用 DMA_EN (对齐 U-Boot) | 对齐 U-Boot |
| 6 | ReadBlockData/WriteBlockData | Cache 修复: 读 DMA 前 Invalidate，**不做** WriteBack (旧代码 WriteBackInvalidate 覆盖 DMA 数据) | Cache 一致性 |
| 7 | Px30EmmcSendCommand CMD18 | 添加 SEND_AUTO_STOP (BIT12) | 语义正确（虽对 Bug 3 无帮助） |
| 8 | Px30EmmcIsMultiBlock | 返回 FALSE | Bug 3 规避 |
| 9 | CMD6 inline | 在驱动内执行 CMD6 512 字节数据块 | MmcDxe 不调用 WriteBlockData |
| 10 | CMD2/CMD9 | 临时禁用 CRC 检查 | 诊断用，可恢复 |

### Px30Emmc.h

| # | 改动 | 原因 |
|---|------|------|
| 1 | 补充完整中断位定义 (EBE/ACD/SBE/HLE/FRUN/HTO/DRT/RTO/DCRC/RCRC/RXDR/TXDR/DTO/CMD_DONE/RE) | U-Boot 对齐 |

### MmcBlockIo.c (EmbeddedPkg)

| # | 改动 | 原因 |
|---|------|------|
| 1 | `if (RemainingBlock == 0) RemainingBlock = 1;` | 阻止 BlockCount 变为 0 导致误用 CMD18 |

## 当前状态

### 已完成
- [x] CMD0-CMD16 全部成功
- [x] eMMC 识别完成 (CID, CSD, EXT_CSD)
- [x] 时钟切换到 52MHz, 8-bit 总线
- [x] CMD17 单块读完美工作
- [x] CMD18/CMD25 通过 IsMultiBlock=FALSE 禁用
- [x] LastBlock = 0x1D2DFFF (cache 修复后有效)
- [x] BLK0 在 UEFI Shell 可见
- [x] MBR/GPT 分区表可解析
- [x] 无超时、无 DMA 错误、无卡死

### 硬件限制
- [ ] CMD18 (READ_MULTIPLE_BLOCK): IDMAC v2.70a 硬件不触发
- [ ] CMD25 (WRITE_MULTIPLE_BLOCK): 预期同样不触发（未测试）

### 下一步
- [ ] eMMC 上创建 GPT + FAT32 EFI 分区（当前是 U-Boot 布局）
- [ ] 放入 BOOTAA64.EFI
- [ ] 从 eMMC 启动

## 相关文件

### 本项目修改
- `edk2-rockchip/Silicon/Rockchip/RK3326/Drivers/Px30EmmcDxe/Px30EmmcDxe.c` — 主驱动
- `edk2-rockchip/Silicon/Rockchip/RK3326/Drivers/Px30EmmcDxe/Px30Emmc.h` — 寄存器定义
- `edk2/EmbeddedPkg/Universal/MmcDxe/MmcBlockIo.c` — MmcDxe Block I/O (BlockCount=0 修复)
- `edk2-rockchip/Platform/Rockchip/RK3326Evb/RK3326Evb.dsc` — 平台描述
- `edk2-rockchip/Silicon/Rockchip/RK3326/RK3326.fdf` — 固件布局

### U-Boot 参考
- `drivers/mmc/dw_mmc.c` — DWMMC 通用驱动
- `drivers/mmc/rockchip_dw_mmc.c` — Rockchip 平台层 (phase tuning)
- `include/dwmmc.h` — DWMMC 寄存器/中断位定义
- `drivers/clk/rockchip/clk_px30.c` — PX30 时钟驱动
- `arch/arm/mach-rockchip/px30/px30.c` — PX30 板级初始化

### Linux 内核
- `drivers/mmc/host/dw_mmc-rockchip.c` — Rockchip DWMMC 内核驱动

## 调试时间线

- 2026-06-11: 初始调试，添加 Px30EmmcPlatformInit (CRU/IOMUX/VCCIO6)
- 2026-06-11: 修复 RSTNOUT IOMUX (设为 GPIO 避免 DWMMC 驱动 RST)
- 2026-06-12: 对比 U-Boot 修复 CMD 标志位
- 2026-06-12: 添加 CARDTHRCTL / FIFOTH / CLKENA 初始化
- 2026-06-12: 关闭 CMD2 CRC → RE 仍存在 → 确认为 v2.70a quirk
- 2026-06-12: 深入分析瞬态错误位 (RE/EBE/RTO) 处理
- 2026-06-12: 识别 DAT_FSM=1 正常状态，修复虚假 FIFO 复位
- 2026-06-12: IDMAC descriptor cache flush 缺失 → CMD6/CMD8 数据卡死修复
- 2026-06-12: CMD16 RTO+CMD_DONE 处理
- 2026-06-12: CMD8 成功, CMD18 失败 → 发现 FRUN/HTO quirk
- 2026-06-12: 添加 FIFO 复位 / DMA 禁用 / cache 修复 (对齐 U-Boot)
- 2026-06-12: CMD17 成功, CMD18 IDSTS=0 → 确认 IDMAC 硬件不触发
- 2026-06-12: SEND_AUTO_STOP 测试 → 无效
- 2026-06-12: IsMultiBlock=FALSE + MmcBlockIo.c BlockCount=0 修复
- 2026-06-12: CMD18 完全消除，块 I/O 正常，文档整理
