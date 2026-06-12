# Change Log

→ [中文](CHANGELOG_cn.md) · [README](README.md)

## 2026-06-13 — ACPI + Perf + Docs

### Added
- ✅ FADT: HW_REDUCED_ACPI + PSCI_COMPLIANT|USE_HVC
- ✅ DSDT: 70B AML (CPU0), linked by FADT + XSDT
- ✅ GTDT: Fixed GSIV=30 field offset
- ✅ ACPI: 4 tables, 0 Error

### Optimized
- ⚡ eMMC cmd delay: 15ms → 1ms
- ⚡ CMD1 retries: 1000 → 200
- ⚡ Poll timeout: 20000 → 5000 (10s→2.5s)
- ✅ eMMC 52MHz HS mode confirmed

### Fixed
- 🐛 Shell boot option: `0x0000` → `LOAD_OPTION_ACTIVE`
- 🐛 `PcdShellLibAutoInitialize`: TRUE → FALSE
- 🐛 GTDT NonSecureEL1 GSIV misfield
- 🐛 `CardType` missing in `CARD_INFO` struct

### Docs
- 📝 All docs split into EN/CN versions
- 📝 Cross-linked across all documents
- 📁 DTS files moved to `docs/dts/`
- 📝 Patches regenerated

---

## 2026-06-12 — Display + UiApp + eMMC

### Added
- ✅ GOP 640×480 via SW rotate (480×640 physical)
- ✅ UiApp standard boot manager
- ✅ GPIO keys: UP + ESC (SimpleTextInEx)
- ✅ Shell launchable from UiApp

### Fixed
- 🐛 UiApp crash at 480px → rotated to 640×480
- 🐛 MMC: BlockCount, GPT CRC, MBR 0xEE

---

## Status

| Feature | Status |
|---------|--------|
| GOP 640×480 | ✅ |
| eMMC 52MHz HS | ✅ |
| Shell auto-boot | ✅ |
| UiApp + GPIO keys | ✅ |
| ACPI 4 tables (0 err) | ✅ |
| USB2 EHCI+OHCI | ✅ |
| Variable NVRAM | ✅ |

→ Full: [`docs/porting-status.md`](docs/porting-status.md)
