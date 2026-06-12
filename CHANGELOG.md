# RK3326 EDK2 Port — Change Log / 改动日志

## 功能状态 / Feature Status

| Feature / 功能 | Status / 状态 | Notes / 备注 |
|---------|--------|-------|
| Serial UART2 | ✅ | 0xFF160000, 115200 8N1 |
| eMMC | ✅ | DWMMC v2.70a, GPT, FAT32 |
| USB2 | ✅ | EHCI+OHCI |
| Display / 显示 (GOP) | ✅ | 480×640 物理 → 640×480 软件旋转 |
| UiApp Boot Menu / 启动菜单 | ✅ | 640×480 横屏正常 |
| UEFI Shell | ✅ | 内置，可从 UiApp 启动 |
| GPIO Keys / 按键 | ✅ | PB5→UP, PB7→ENTER |
| SARADC Keys | ⚠️ | 驱动加载，ADC 返回 0 |
| Variable / 变量存储 | ✅ | 模拟 NVRAM |

## 关键设计 / Key Design

### 显示旋转 / Display Rotation

标准 EDK2 UiApp 需要 ≥640px 水平分辨率，物理面板为 480×640 竖屏。
`SimpleFbDxe` 在 Blt 中做 90° CCW + X翻转软件旋转：

Standard UiApp needs ≥640px horizontal. Physical panel is 480×640 portrait.
`SimpleFbDxe` performs 90° CCW + X-flip in Blt:

```
GOP 上报/reports: 640×480 landscape / 横屏
物理帧缓冲/physical: 480×640
映射/mapping: logical(x,y) → physical[(639-x)][y]
```

### 启动流程 / Boot Flow

修改 `ArmPkg/PlatformBm.c`：跳过自动启动发现 → BDS 无启动项 → 自动进入 UiApp。

Patched `ArmPkg/PlatformBm.c`: skip auto-discovery → no boot options → UiApp.

### GPIO 按键 / GPIO Keys

`GpioKeypadDxe` 轮询 GPIO0 PB5(UP) + PB7(ENTER)，通过 `SimpleTextInEx` 输入。
Polls GPIO0 via `SimpleTextInEx` protocol.

## 修改文件 / Modified Files

See / 详见 `docs/edk2-all.patch` (6 files in EDK2 submodule) and / 和 `docs/porting-status.md`.
