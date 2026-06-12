# Porting Status

✅ **All core features complete** — Shell auto-boot + UiApp + ACPI + eMMC 52MHz HS.

→ [中文](porting-status_cn.md) · [README](../README.md) · [CHANGELOG](../CHANGELOG.md) · [Guide](porting-guide.md)

## Architecture

```
U-Boot 6.1 → EDK2 @0x00200000 → PeilessSec → DXE → BDS → Shell(auto) → exit → UiApp
```

## Feature Status

| Feature | Status | Note |
|---------|--------|------|
| Serial UART2 | ✅ | 0xFF160000 |
| eMMC | ✅ | DWMMC v2.70a, 52MHz HS, GPT/FAT32, 9 part |
| USB2 | ✅ | EHCI+OHCI, KB + Mass Storage |
| Display GOP | ✅ | 640×480 (480×640 → SW rotate) |
| UEFI Shell | ✅ | Auto-boot, `exit` → UiApp |
| UiApp Menu | ✅ | GPIO keys operable |
| GPIO Keys | ✅ | PB5→UP, PB7→ESC |
| ACPI | ✅ | FADT+DSDT+MADT+GTDT, 0 Error |
| Variable | ✅ | Emulated NVRAM |
| I2C | ⚠️ | I2C0 init, no upper drivers |
| SARADC | ⚠️ | Driver loaded, ADC=0 |
| Logo | ⚠️ | Embedded, not shown |

## Key Design

### Display — SimpleFbDxe
```
Physical: 480×640 portrait → GOP: 640×480 landscape
Rotation: 90°CCW + X-flip in Blt()
Mapping: logical(x,y) → physical[(639-x)][y]
```

### Boot — PlatformBm.c
Skip `BootDiscoveryPolicyHandler()`, Shell with `LOAD_OPTION_ACTIVE`, timeout=0.

### eMMC HS Mode
```
CMD6(HS_TIMING=1) → SetIos(52MHz, 8-bit) → CMD6(BUS_WIDTH)
```

### ACPI
```
XSDT → FACP (HW_REDUCED, PSCI) → X_DSDT → DSDT
     → MADT (GICv2, 1×GICC)
     → GTDT (GSIV=30)
```

### Performance

| Item | Before | After |
|------|--------|-------|
| Cmd delay | 15ms | 1ms |
| CMD1 retries | 1000 | 200 |
| Poll timeout | 10s | 2.5s |
| eMMC clock | 400KHz | **52MHz** |

## EDK2 Patches (4 patches, 5 files)

| # | File | Change |
|---|------|--------|
| 1 | `PlatformBm.c` | Shell auto-boot |
| 2 | `PeilessSec.c` | Single-core support |
| 3 | `MmcBlockIo.c` | BlockCount=0 |
| 4 | `Gpt.c`+`Mbr.c` | CRC/0xEE skip |

→ Combined: [`edk2-all.patch`](edk2-all.patch) · Individual: [`patches/`](patches/)

## Custom Drivers (10)

`Px30EmmcDxe` · `SimpleFbDxe` · `AcpiPlatformDxe` · `GpioKeypadDxe` · `AdcKeypadDxe` · `UsbHcdInitDxe` · `I2cDxe` · `RK3326Dxe` · `FdtDxe` · `MmcDxe`

## Build

```bash
cd edk2-rk3326 && ./build.sh DEBUG
# → Build/RK3326EVB/DEBUG_GCC/FV/BL33_AP_UEFI.Fv
```

## Known Issues

1. **SARADC**: returns 0, clock init needed
2. **Logo**: embedded but not shown
3. **CMD18**: IDMAC bug, `IsMultiBlock=FALSE`
4. **GPLL**: 100MHz vs 109MHz, eMMC ~54.5MHz

## TODO

**High**: OS boot test, GRUB/Linux EFI stub  
**Mid**: SD card, SARADC fix, Logo  
**Low**: CMD18/25, persistent Variable, Capsule Update
