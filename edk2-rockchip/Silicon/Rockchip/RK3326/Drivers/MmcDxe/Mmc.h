/** @file
  MMC/SD Block I/O Driver header

  Copyright (c) 2011-2015, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _MMC_DXE_H_
#define _MMC_DXE_H_

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverDiagnostics2.h>
#include <Protocol/MmcHost.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>

#define MMC_HOST_INSTANCE_SIGNATURE  SIGNATURE_32('m','m','c','h')

#define MMC_HOST_INSTANCE_FROM_BLOCK_IO_THIS(a) \
  CR(a, MMC_HOST_INSTANCE, BlockIo, MMC_HOST_INSTANCE_SIGNATURE)
#define MMC_HOST_INSTANCE_FROM_DISK_IO_THIS(a)  \
  CR(a, MMC_HOST_INSTANCE, DiskIo, MMC_HOST_INSTANCE_SIGNATURE)

#define MMC_R0_READY_FOR_DATA     BIT8

#define MMC_R0_CURRENTSTATE(r)    (((r)[0]) >> 9) & 0xF
#define MMC_R0_STATE_IDLE         0
#define MMC_R0_STATE_READY        1
#define MMC_R0_STATE_IDENT        2
#define MMC_R0_STATE_STBY         3
#define MMC_R0_STATE_TRAN         4
#define MMC_R0_STATE_DATA         5
#define MMC_R0_STATE_RCV          6
#define MMC_R0_STATE_PRG          7
#define MMC_R0_STATE_DIS          8

typedef enum {
  MmcHwInitializationState,
  MmcIdleState,
  MmcReadyState,
  MmcIdentificationState,
  MmcStandByState,
  MmcTransferState,
  MmcSendingDataState,
  MmcReceiveDataState,
  MmcProgrammingState,
  MmcDisconnectState,
} MMC_STATE;

#define EMMCBACKWARD          0
#define EMMCHS26              (1 << 0)  // 1 — High-Speed @26MHz
#define EMMCHS52              (1 << 1)  // 2 — High-Speed @52MHz
#define EMMCHS52DDR1V8        (1 << 2)  // 4 — HS DDR @52MHz 1.8V/3V
#define EMMCHS52DDR1V2        (1 << 3)  // 8 — HS DDR @52MHz 1.2V

// EXT_CSD byte offsets
#define EXTCSD_BUS_WIDTH      183
#define EXTCSD_HS_TIMING      185
#define EXTCSD_DEVICE_TYPE    196

#define EMMC_TIMING_BACKWARD  0
#define EMMC_TIMING_HS        1
#define EMMC_TIMING_HS200     2
#define EMMC_TIMING_HS400     3

#define EMMC_BUS_WIDTH_1BIT      0
#define EMMC_BUS_WIDTH_4BIT      1
#define EMMC_BUS_WIDTH_8BIT      2
#define EMMC_BUS_WIDTH_DDR_4BIT  5
#define EMMC_BUS_WIDTH_DDR_8BIT  6

#define EMMC_SWITCH_ERROR  (1 << 7)

#define EMMC_CARD  1
#define SD_CARD    2

#define EMMC_CMD6_ARG_CMD_SET(a)      ((a) << 24)
#define EMMC_CMD6_ARG_VALUE(a)        ((a) << 8)
#define EMMC_CMD6_ARG_INDEX(a)        ((a) << 16)
#define EMMC_CMD6_ARG_ACCESS(a)       ((a) << 24)

#define MMC_OCR_ACCESS_MASK           (3 << 29)
#define MMC_OCR_ACCESS_BYTE           (0 << 29)
#define MMC_OCR_ACCESS_SECTOR         (2 << 29)

typedef struct {
  UINT8   SD_SPEC:4;
  UINT8   SCR_STRUCTURE:4;
  UINT8   SD_BUS_WIDTHS:4;
  UINT8   SD_SECURITY:3;
  UINT8   DATA_STAT_AFTER_ERASE:1;
  UINT8   SD_SPEC3:1;
  UINT8   EX_SECURITY:4;
  UINT8   SD_SPEC4:1;
  UINT8   RESERVED:1;
  UINT8   CMD_SUPPORT:4;
  UINT8   RESERVED2:3;
  UINT8   SD_SPECX:1;
  UINT8   RESERVED3[2];
} SCR;

typedef struct {
  UINT32  Reserved0:7;
  UINT32  V170_V195:1;
  UINT32  V200_V260:7;
  UINT32  V270_V360:9;
  UINT32  Reserved1:5;
  UINT32  AccessMode:2;
  UINT32  PowerUp:1;
} OCR;

typedef struct {
  UINT8   Month:4;
  UINT8   Year:4;
  UINT8   Reserved0:4;
  UINT8   Day:4;
  UINT32  Psn:32;
  UINT8   Prv:8;
  UINT8   Pnm[6];
  UINT8   Oid[2];
  UINT8   Mid:8;
} CID;

typedef struct {
  UINT8   NotUsed:1;
  UINT8   CRC:7;
  UINT8   ECC:2;
  UINT8   FileFormat:2;
  UINT8   TmpWriteProtect:1;
  UINT8   PermWriteProtect:1;
  UINT8   Copy:1;
  UINT8   FileFormatGrp:1;
  UINT8   ContentProtApp:1;
  UINT8   Reserved0:4;
  UINT8   WriteBlPartial:1;
  UINT8   WriteBlLen:4;
  UINT8   R2WFactor:3;
  UINT8   DefaultEcc:2;
  UINT8   WpGrpEnable:1;
  UINT8   WpGrpSize:5;
  UINT8   EraseGrpMult:5;
  UINT8   EraseGrpSize:5;
  UINT8   CSizeMult:3;
  UINT8   VddWCurrMax:3;
  UINT8   VddWCurrMin:3;
  UINT8   VddRCurrMax:3;
  UINT8   VddRCurrMin:3;
  UINT8   CSizeLow:2;
  UINT8   CSizeHigh:10;
  UINT8   Reserved1:2;
  UINT8   DsrImp:1;
  UINT8   ReadBlkMisalign:1;
  UINT8   WriteBlkMisalign:1;
  UINT8   ReadBlPartial:1;
  UINT8   ReadBlLen:4;
  UINT8   CCC:12;
  UINT8   Transpeed:8;
  UINT8   Nsac:8;
  UINT8   Taac:8;
  UINT8   Reserved2:6;
  UINT8   CsdStructure:2;
} CSD;

typedef struct {
  UINT8   Reserved0[2];
  UINT8   ExtCsdStruct:8;
  UINT8   CardType:8;
  UINT8   Reserved1[56];
  UINT8   SecCount[4];
  UINT8   Reserved2[109];
  UINT8   BusWidth:8;
  UINT8   HsTiming:8;
  UINT8   Reserved3[314];
  UINT8   CmdqSupport:1;
  UINT8   Reserved4:7;
  UINT8   Reserved5[2];
} ECSD;

typedef struct  {
  UINT16    RCA;
  UINT32    OCRData;
  UINT8     CardType;    // EMMC_CARD or SD_CARD
  CID       CIDData;
  CSD       CSDData;
  ECSD      ECSDData;
  SCR       SCRData;
} CARD_INFO;

typedef struct {
  UINT32                    Signature;
  LIST_ENTRY                Link;
  EFI_HANDLE                Handle;
  EFI_BLOCK_IO_PROTOCOL     BlockIo;
  EFI_BLOCK_IO_MEDIA        Media;
  EFI_DISK_IO_PROTOCOL      DiskIo;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_MMC_HOST_PROTOCOL     *MmcHost;
  CARD_INFO                 CardInfo;
  MMC_STATE                 State;
} MMC_HOST_INSTANCE;

// Block I/O
#define MMC_IOBLOCKS_READ     0
#define MMC_IOBLOCKS_WRITE    1

// Commands
#define MMC_CMD0      0
#define MMC_CMD1      1
#define MMC_CMD2      2
#define MMC_CMD3      3
#define MMC_CMD6      6
#define MMC_CMD7      7
#define MMC_CMD8      8
#define MMC_CMD9      9
#define MMC_CMD12     12
#define MMC_CMD13     13
#define MMC_CMD16     16
#define MMC_CMD17     17
#define MMC_CMD18     18
#define MMC_CMD24     24
#define MMC_CMD25     25
#define MMC_CMD55     55
#define MMC_ACMD41    41
#define MMC_ACMD51    51

extern EFI_BLOCK_IO_MEDIA  mMmcMediaTemplate;
extern LIST_ENTRY          mMmcHostPool;

VOID  InitializeMmcHostPool (VOID);
VOID  InsertMmcHost (IN MMC_HOST_INSTANCE *MmcHostInstance);
VOID  RemoveMmcHost (IN MMC_HOST_INSTANCE *MmcHostInstance);
MMC_HOST_INSTANCE *CreateMmcHostInstance (IN EFI_MMC_HOST_PROTOCOL *MmcHost);
EFI_STATUS DestroyMmcHostInstance (IN MMC_HOST_INSTANCE *MmcHostInstance);

// BlockIo
EFI_STATUS MmcNotifyState (IN MMC_HOST_INSTANCE *M, IN MMC_STATE S);
EFI_STATUS MmcGetCardStatus (IN MMC_HOST_INSTANCE *M);
EFI_STATUS MmcReset (IN EFI_BLOCK_IO_PROTOCOL *T, IN BOOLEAN E);
EFI_STATUS MmcDetectCard (IN EFI_MMC_HOST_PROTOCOL *H);
EFI_STATUS MmcStopTransmission (IN EFI_MMC_HOST_PROTOCOL *H);
EFI_STATUS MmcReadBlocks (IN EFI_BLOCK_IO_PROTOCOL *T, IN UINT32 Id, IN EFI_LBA L, IN UINTN S, OUT VOID *B);
EFI_STATUS MmcWriteBlocks (IN EFI_BLOCK_IO_PROTOCOL *T, IN UINT32 Id, IN EFI_LBA L, IN UINTN S, IN VOID *B);

// DiskIo
EFI_STATUS MmcReadDisk (IN EFI_DISK_IO_PROTOCOL *T, IN UINT32 Id, IN UINT64 O, IN UINTN S, OUT VOID *B);
EFI_STATUS MmcWriteDisk (IN EFI_DISK_IO_PROTOCOL *T, IN UINT32 Id, IN UINT64 O, IN UINTN S, IN VOID *B);

// Identification
EFI_STATUS InitializeMmcDevice (IN MMC_HOST_INSTANCE *M);

// Debug
VOID PrintCID (IN UINT32 *Cid);
VOID PrintCSD (IN UINT32 *Csd);
VOID PrintRCA (IN UINT32 Rca);
VOID PrintOCR (IN UINT32 Ocr);
VOID PrintResponseR1 (IN UINT32 R1);

// Diagnostics
EFI_STATUS MmcDriverDiagnosticsRunDiagnostics (IN EFI_DRIVER_DIAGNOSTICS2_PROTOCOL *T,
              IN EFI_HANDLE H, IN EFI_HANDLE C, IN EFI_DRIVER_DIAGNOSTIC_TYPE DT,
              IN CHAR8 *L, OUT EFI_DRIVER_DIAGNOSTIC_RESULT **R, OUT CHAR16 **B);

// Component Name
EFI_STATUS MmcGetDriverName (IN EFI_COMPONENT_NAME_PROTOCOL *T, IN CHAR8 *L, OUT CHAR16 **N);
EFI_STATUS MmcGetControllerName (IN EFI_COMPONENT_NAME_PROTOCOL *T, IN EFI_HANDLE H,
              IN EFI_HANDLE C, IN CHAR8 *L, OUT CHAR16 **N);

#endif
