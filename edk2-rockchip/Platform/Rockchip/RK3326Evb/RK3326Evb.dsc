## @file
#  RK3326 EVB LPDDR3 v10 — Board Platform Description
#
#  RK3326 EVB: 4x Cortex-A35, LPDDR3 1-2GB, RK817 PMIC
#
#  Copyright (c) 2024-2026, RK3326 EDK2 Port
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = RK3326EVB
  PLATFORM_VENDOR                = Rockchip
  PLATFORM_GUID                  = 1a2b3c4d-5e6f-7a8b-9c0d-1e2f3a4b5c6d
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  PLATFORM_DIRECTORY             = Platform/Rockchip/RK3326Evb
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3326/RK3326.fdf

  # Peripheral flags
  DEFINE RK_SD_ENABLE            = TRUE
  DEFINE RK_EMMC_ENABLE          = TRUE
  DEFINE RK_USB_ENABLE           = TRUE
  DEFINE RK_GMAC_ENABLE          = FALSE
  DEFINE RK_NOR_FLASH_ENABLE     = FALSE
  DEFINE RK_DISPLAY_ENABLE       = FALSE
  DEFINE SECURE_BOOT_ENABLE      = FALSE

!include Silicon/Rockchip/RK3326/RK3326.dsc.inc

[BuildOptions]
  GCC:*_*_AARCH64_CC_FLAGS = -DSOC_RK3326
