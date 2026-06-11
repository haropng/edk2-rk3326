/** @file
  PX30 DesignWare MMC register definitions.

  Based on RK3399 DwEmmc.h — same DWMMC IP version.
  + Rockchip-specific eMMC control / DLL registers.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __PX30_EMMC_H__
#define __PX30_EMMC_H__

// ── DW MMC Standard Registers ──────────────────────────────────────
#define DWEMMC_CTRL             0x000
#define DWEMMC_PWREN            0x004
#define DWEMMC_CLKDIV           0x008
#define DWEMMC_CLKSRC           0x00c
#define DWEMMC_CLKENA           0x010
#define DWEMMC_TMOUT            0x014
#define DWEMMC_CTYPE            0x018
#define DWEMMC_BLKSIZ           0x01c
#define DWEMMC_BYTCNT           0x020
#define DWEMMC_INTMASK          0x024
#define DWEMMC_CMDARG           0x028
#define DWEMMC_CMD              0x02c
#define DWEMMC_RESP0            0x030
#define DWEMMC_RESP1            0x034
#define DWEMMC_RESP2            0x038
#define DWEMMC_RESP3            0x03c
#define DWEMMC_RINTSTS          0x044
#define DWEMMC_STATUS           0x048
#define DWEMMC_FIFOTH           0x04c
#define DWEMMC_DEBNCE           0x064
#define DWEMMC_USRID            0x068
#define DWEMMC_VERID            0x06C
#define DWEMMC_HCON             0x070
#define DWEMMC_UHSREG           0x074
#define DWEMMC_BMOD             0x080
#define DWEMMC_DBADDR           0x088
#define DWEMMC_IDSTS            0x08c
#define DWEMMC_IDINTEN          0x090
#define DWEMMC_DSCADDR          0x094
#define DWEMMC_BUFADDR          0x098
#define DWEMMC_CARDTHRCTL       0x100

// ── Rockchip-specific eMMC registers ──────────────────────────────
#define EMMC_EMMC_CTRL          0x04c   // eMMC control (differs from FIFOTH)
#define EMMC_DLL_CTRL           0x104

#define EMMC_DLL_CTRL_START_POINT_DEFAULT   (0x10 << 16)
#define EMMC_DLL_CTRL_INCREMENT_DEFAULT     (0x10 <<  8)
#define EMMC_DLL_CTRL_START                 BIT0

// ── CMD register bits ──────────────────────────────────────────────
#define CMD_START_BIT           BIT31
#define CMD_UPDATE_CLK          0x80202000

#define BIT_CMD_RESPONSE_EXPECT                 BIT6
#define BIT_CMD_LONG_RESPONSE                   BIT7
#define BIT_CMD_CHECK_RESPONSE_CRC              BIT8
#define BIT_CMD_DATA_EXPECTED                   BIT9
#define BIT_CMD_WRITE                           BIT10
#define BIT_CMD_STREAM_TRANSFER                 BIT11
#define BIT_CMD_SEND_AUTO_STOP                  BIT12
#define BIT_CMD_WAIT_PRVDATA_COMPLETE           BIT13
#define BIT_CMD_STOP_ABORT_CMD                  BIT14
#define BIT_CMD_SEND_INIT                       BIT15
#define BIT_CMD_UPDATE_CLOCK_ONLY               BIT21
#define BIT_CMD_USE_HOLD_REG                    BIT29
#define BIT_CMD_START                           BIT31

// ── CTRL register bits ─────────────────────────────────────────────
#define DWEMMC_CTRL_RESET                       BIT0
#define DWEMMC_CTRL_FIFO_RESET                  BIT1
#define DWEMMC_CTRL_DMA_RESET                   BIT2
#define DWEMMC_CTRL_INT_EN                      BIT4
#define DWEMMC_CTRL_DMA_EN                      BIT5
#define DWEMMC_CTRL_IDMAC_EN                    BIT25
#define DWEMMC_CTRL_RESET_ALL \
  (DWEMMC_CTRL_RESET | DWEMMC_CTRL_FIFO_RESET | DWEMMC_CTRL_DMA_RESET)

// ── Interrupt bits ─────────────────────────────────────────────────
#define DWEMMC_INT_EBE                          BIT15
#define DWEMMC_INT_ACD                          BIT14
#define DWEMMC_INT_SBE                          BIT13
#define DWEMMC_INT_HLE                          BIT12
#define DWEMMC_INT_FRUN                         BIT11
#define DWEMMC_INT_HTO                          BIT10
#define DWEMMC_INT_DRT                          BIT9
#define DWEMMC_INT_RTO                          BIT8
#define DWEMMC_INT_DCRC                         BIT7
#define DWEMMC_INT_RCRC                         BIT6
#define DWEMMC_INT_RXDR                         BIT5
#define DWEMMC_INT_TXDR                         BIT4
#define DWEMMC_INT_DTO                          BIT3
#define DWEMMC_INT_CMD_DONE                     BIT2
#define DWEMMC_INT_RE                           BIT1

// ── IDMAC bits ─────────────────────────────────────────────────────
#define DWEMMC_IDMAC_DES0_DIC                   BIT1
#define DWEMMC_IDMAC_DES0_LD                    BIT2
#define DWEMMC_IDMAC_DES0_FS                    BIT3
#define DWEMMC_IDMAC_DES0_CH                    BIT4
#define DWEMMC_IDMAC_DES0_OWN                   BIT31
#define DWEMMC_IDMAC_DES1_BS1(x)                ((x) & 0x1fff)
#define DWEMMC_IDMAC_SWRESET                    BIT0
#define DWEMMC_IDMAC_FB                         BIT1
#define DWEMMC_IDMAC_ENABLE                     BIT7

#define DWEMMC_STS_DATA_BUSY                    BIT9

#endif /* __PX30_EMMC_H__ */
