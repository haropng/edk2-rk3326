/** @file
  RK3326 (PX30) SoC Header — peripheral MMIO addresses.

  All addresses verified against Linux 6.1 BSP px30.dtsi and U-Boot 2017.09.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __RK3326_SOC_H__
#define __RK3326_SOC_H__

#include <Uefi/UefiBaseType.h>

/* ── Peripheral MMIO Map ───────────────────────────────────────────── */

#define UART2_BASE           0xFF160000UL
#define UART_CLOCK           24000000UL
#define UART_BAUD            1500000UL

#define GICD_BASE            0xFF131000UL
#define GICC_BASE            0xFF132000UL

#define CRU_BASE             0xFF2B0000UL
#define PMUCRU_BASE          0xFF2BC000UL
#define PMU_BASE             0xFF000000UL
#define PMUGRF_BASE          0xFF010000UL
#define GRF_BASE             0xFF140000UL
#define CORE_GRF_BASE        0xFF148000UL

#define GPIO0_BASE           0xFF040000UL
#define GPIO1_BASE           0xFF250000UL
#define GPIO2_BASE           0xFF260000UL
#define GPIO3_BASE           0xFF270000UL

#define I2C0_BASE            0xFF180000UL
#define I2C1_BASE            0xFF190000UL
#define I2C2_BASE            0xFF1A0000UL
#define I2C3_BASE            0xFF1B0000UL

#define EMMC_BASE            0xFF390000UL
#define SDMMC_BASE           0xFF370000UL
#define SDIO_BASE            0xFF380000UL
#define SFC_BASE             0xFF3A0000UL

#define USB_OTG_BASE         0xFF300000UL
#define USB_EHCI_BASE        0xFF340000UL
#define USB_OHCI_BASE        0xFF350000UL
#define USB2PHY_GRF_BASE     0xFF2C0000UL

#define GMAC_BASE            0xFF360000UL
#define VOPB_BASE            0xFF460000UL
#define DSI_BASE             0xFF450000UL
#define GPU_BASE             0xFF400000UL
#define DMC_BASE             0xFF2A0000UL
#define DMAC_BASE            0xFF240000UL
#define OTP_BASE             0xFF290000UL
#define TSADC_BASE           0xFF280000UL
#define SARADC_BASE          0xFF288000UL
#define WDT_BASE             0xFF1E0000UL

#define PLL_INPUT_OSC_RATE   (24 * 1000 * 1000)

#define RK3326_PERIPH_BASE   0xF8000000UL
#define RK3326_PERIPH_SZ     0x08000000UL

#define PMIC_I2C_BUS         0
#define PMIC_I2C_ADDR        0x20

#endif /* __RK3326_SOC_H__ */
