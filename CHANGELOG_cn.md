# 改动日志

→ [English](CHANGELOG.md) · [README](README_cn.md)

## 2026-06-13 — ACPI 完善 + 性能优化 + 文档整理

### 新增
- ✅ FADT: HW_REDUCED_ACPI + PSCI_COMPLIANT|USE_HVC
- ✅ DSDT: 70B AML (CPU0), FADT+XSDT 双路引用
- ✅ GTDT: 修复 NonSecureEL1 GSIV=30 字段偏移
- ✅ ACPI: 4 张表, 0 错误

### 优化
- ⚡ eMMC 命令延迟: 15ms → 1ms
- ⚡ CMD1 重试: 1000 → 200
- ⚡ 轮询超时: 20000 → 5000 (10s→2.5s)
- ✅ eMMC 52MHz HS 模式确认

### 修复
- 🐛 Shell 启动选项: `0x0000` → `LOAD_OPTION_ACTIVE`
- 🐛 `PcdShellLibAutoInitialize`: TRUE → FALSE
- 🐛 GTDT NonSecureEL1 GSIV 字段偏移
- 🐛 `CARD_INFO` 结构体缺 `CardType` 字段

### 文档
- 📝 全部文档拆分为中英独立版本
- 📝 文档间交叉链接
- 📁 设备树文件归档到 `docs/dts/`
- 📝 补丁重新生成

---

## 2026-06-12 — 显示旋转 + UiApp + eMMC

### 新增
- ✅ GOP 640×480 软件旋转 (480×640 物理)
- ✅ UiApp 标准启动管理器
- ✅ GPIO 按键: UP + ESC (SimpleTextInEx)
- ✅ 从 UiApp 启动 Shell

### 修复
- 🐛 UiApp 在 480px 崩溃 → 旋转到 640×480
- 🐛 MMC: BlockCount, GPT CRC, MBR 0xEE

---

## 状态

| 功能 | 状态 |
|------|------|
| GOP 640×480 | ✅ |
| eMMC 52MHz HS | ✅ |
| Shell 自动启动 | ✅ |
| UiApp + GPIO 按键 | ✅ |
| ACPI 4 表 (0 错误) | ✅ |
| USB2 EHCI+OHCI | ✅ |
| 变量 NVRAM | ✅ |

→ 详见: [`docs/porting-status_cn.md`](docs/porting-status_cn.md)
