/** @file
  ADC Keypad Driver for RK3326 (PX30) — SARADC-based button input.

  Reads the SARADC (0xFF288000) channel 2 to detect resistor-ladder
  button presses.  Installs EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL so
  ConSplitterDxe merges this input with serial/USB keyboard.

  Hardware: PX30 SARADC (rk3399-compatible), 12-bit, VREF=1.8V.
  Key resistor ladder from appolo.dts adc-keys.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/SimpleTextInEx.h>
#include <Soc.h>

//
// SARADC registers (PX30, rk3399-compatible)
//
// SARADC_BASE defined in Soc.h as 0xFF288000
#define SARADC_DATA         0x00
#define SARADC_STAS         0x04
#define SARADC_CTRL         0x08
#define SARADC_DLY_PU_SOC   0x0C

#define SARADC_CTRL_CHN_MASK    0x07U   // bits[2:0] = channel
#define SARADC_CTRL_POWER       BIT3    // power control (1 = on)
#define SARADC_STAS_DONE        BIT0    // conversion complete
#define SARADC_DATA_MASK        0x0FFFU  // 12-bit result

//
// SARADC clock/reset IDs (from dt-bindings/clock/px30-cru.h & DTS)
//
#define SCLK_SARADC     45
#define PCLK_SARADC     343   // 0x157 from DTS, NOT 344
#define SRST_SARADC_APB  165

//
// Rockchip CRU write convention: hi16=mask, lo16=value
//
#define CRU_WR(addr, bit, val) \
  MmioWrite32 ((addr), ((1U << (bit)) << 16) | ((val) ? (1U << (bit)) : 0))

// ── SARADC clock + reset init ────────────────────────────────────
STATIC VOID SaradcInit (VOID) {
  // Enable clock gates
  CRU_WR (CRU_BASE + 0x100U + ((SCLK_SARADC) / 16U) * 4U, (SCLK_SARADC) % 16U, 1);
  CRU_WR (CRU_BASE + 0x100U + ((PCLK_SARADC) / 16U) * 4U, (PCLK_SARADC) % 16U, 1);
  // Deassert reset
  MicroSecondDelay (10);
  CRU_WR (CRU_BASE + 0x200U + ((SRST_SARADC_APB) / 16U) * 4U, (SRST_SARADC_APB) % 16U, 0);
  MicroSecondDelay (10);
  // Configure power-up delay (RK3399/PX30 need this for stable readings)
  MmioWrite32 (SARADC_BASE + SARADC_DLY_PU_SOC, 8);
  DEBUG ((DEBUG_INFO, "ADC: SARADC init done\n"));
}

// ── Read SARADC channel ──────────────────────────────────────────
STATIC UINT32 SaradcReadChannel (UINT32 Ch) {
  UINT32 Data;
  UINTN  To;
  // Power up + select channel
  MmioWrite32 (SARADC_BASE + SARADC_CTRL, SARADC_CTRL_POWER | (Ch & SARADC_CTRL_CHN_MASK));
  // Wait for conversion done
  for (To = 10000; To; To--) {
    if (MmioRead32 (SARADC_BASE + SARADC_STAS) & SARADC_STAS_DONE) break;
    MicroSecondDelay (10);
  }
  Data = MmioRead32 (SARADC_BASE + SARADC_DATA);
  // Power down
  MmioWrite32 (SARADC_BASE + SARADC_CTRL, 0);
  return (To == 0) ? (UINT32)-1 : (Data & SARADC_DATA_MASK);
}

//
// ADC channel for buttons
//
#define ADC_CHN_BUTTONS         2
#define ADC_VREF_UV             1800000U   // 1.8V reference

//
// Key definitions (from appolo.dts adc-keys, sorted LOW→HIGH)
// voltage = VREF * (data / 4095)
//
typedef struct {
  UINT32  ThresholdUv;    // press threshold in microvolts
  UINT16  ScanCode;       // UEFI scan code
  CHAR8  *Name;
} ADC_KEY;

STATIC ADC_KEY  mAdcKeys[] = {
  {   17000, 0x01, "VOL_UP"   },   // < 17mV
  {  300000, 0x02, "VOL_DOWN" },   // < 300mV
  {  987000, 0x0D, "MENU"     },   // < 987mV → ENTER
  { 1310000, 0x17, "BACK"     },   // < 1310mV → ESC
};

//
// Key-up threshold: above this = no key pressed
//
#define KEYUP_THRESHOLD_UV  1800000U

//
// Polling / debounce
//
#define POLL_MS             50     // poll every 50ms
#define DEBOUNCE_SAMPLES     3     // 3 consecutive samples = 150ms

//
// Key FIFO (ring buffer)
//
#define FIFO_DEPTH          8

STATIC EFI_KEY_DATA  mFifo[FIFO_DEPTH];
STATIC UINTN         mFifoHead = 0;
STATIC UINTN         mFifoTail = 0;

STATIC EFI_EVENT     mWaitEvent = NULL;
STATIC EFI_EVENT     mTimerEvent = NULL;
STATIC EFI_HANDLE    mHandle    = NULL;

STATIC INT8          mPrevKeyIdx = -1;    // which key is currently pressed
STATIC UINTN         mStableCnt  = 0;     // consecutive samples of same key

/**
  Convert ADC raw value to microvolts.
**/
STATIC UINT32 AdcToUv (UINT32 Raw) {
  return (UINT32)(((UINT64)Raw * ADC_VREF_UV) / 4095ULL); }

//
// ── SimpleTextInEx protocol ──────────────────────────────────────────
//

STATIC
EFI_STATUS
EFIAPI
AdcKeypadReset (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
  IN BOOLEAN                           ExtendedVerification
  )
{
  mFifoHead = mFifoTail = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
AdcKeypadReadKeyStrokeEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
  OUT EFI_KEY_DATA                      *KeyData
  )
{
  if (mFifoHead == mFifoTail) {
    return EFI_NOT_READY;
  }
  CopyMem (KeyData, &mFifo[mFifoHead], sizeof (EFI_KEY_DATA));
  mFifoHead = (mFifoHead + 1) % FIFO_DEPTH;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
AdcKeypadWaitForKeyEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
  OUT EFI_KEY_DATA                      *KeyData
  )
{
  // ConSplitter polls ReadKeyStrokeEx, so we don't block here.
  // Signal mWaitEvent when keys are available.
}

STATIC
EFI_STATUS
EFIAPI
AdcKeypadSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
  IN EFI_KEY_TOGGLE_STATE              *KeyToggleState
  )
{ return EFI_UNSUPPORTED; }

STATIC
EFI_STATUS
EFIAPI
AdcKeypadRegisterKeyNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
  IN  EFI_KEY_DATA                      *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                             **NotifyHandle
  )
{ return EFI_UNSUPPORTED; }

STATIC
EFI_STATUS
EFIAPI
AdcKeypadUnregisterKeyNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
  IN VOID                              *NotificationHandle
  )
{ return EFI_UNSUPPORTED; }

STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  mTextInEx = {
  AdcKeypadReset,
  AdcKeypadReadKeyStrokeEx,
  AdcKeypadWaitForKeyEx,
  AdcKeypadSetState,
  AdcKeypadRegisterKeyNotify,
  AdcKeypadUnregisterKeyNotify,
};

//
// ── Key FIFO ─────────────────────────────────────────────────────────
//

STATIC
VOID
PushKey (
  IN UINT16   ScanCode,
  IN BOOLEAN  KeyDown
  )
{
  EFI_KEY_DATA  *Kd;
  UINTN          Next;

  Next = (mFifoTail + 1) % FIFO_DEPTH;
  if (Next == mFifoHead) return;  // full

  Kd = &mFifo[mFifoTail];
  ZeroMem (Kd, sizeof (*Kd));
  Kd->Key.ScanCode    = ScanCode;
  Kd->Key.UnicodeChar = 0;
  Kd->KeyState.KeyShiftState = KeyDown ? EFI_SHIFT_STATE_VALID : 0;

  mFifoTail = Next;
  gBS->SignalEvent (mWaitEvent);
}

//
// ── Timer callback ───────────────────────────────────────────────────
//

STATIC
VOID
EFIAPI
AdcKeypadTimer (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  UINT32   Raw, Uv;
  INT8     NewIdx;
  UINTN    i;

  //
  // Read ADC and determine which key (if any) is pressed
  //
  Raw = SaradcReadChannel (ADC_CHN_BUTTONS);
  if (Raw == (UINT32)-1) return;

  Uv = AdcToUv (Raw);

  //
  // Find matching key (lowest threshold > voltage wins, first match)
  //
  NewIdx = -1;
  if (Uv < KEYUP_THRESHOLD_UV) {
    for (i = 0; i < ARRAY_SIZE (mAdcKeys); i++) {
      if (Uv < mAdcKeys[i].ThresholdUv) {
        NewIdx = (INT8)i;
        break;
      }
    }
  }

  //
  // Debounce: require DEBOUNCE_SAMPLES consecutive reads of same state
  //
  if (NewIdx == mPrevKeyIdx) {
    if (++mStableCnt >= DEBOUNCE_SAMPLES) {
      //
      // State change confirmed
      //
      if (NewIdx >= 0 && mStableCnt == DEBOUNCE_SAMPLES) {
        // Button pressed — inject keystroke
        DEBUG ((DEBUG_INFO, "ADC: %a pressed (ADC=%u, %uuV)\n",
                mAdcKeys[NewIdx].Name, Raw, Uv));
        PushKey (mAdcKeys[NewIdx].ScanCode, TRUE);
        PushKey (mAdcKeys[NewIdx].ScanCode, FALSE);  // key release
      }
      // else if (NewIdx < 0): key released, nothing to do
    }
  } else {
    mPrevKeyIdx = NewIdx;
    mStableCnt  = 1;
  }
}

//
// ── Driver Entry ─────────────────────────────────────────────────────
//

EFI_STATUS
EFIAPI
AdcKeypadEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "AdcKeypadDxe: initializing SARADC ch=%d\n",
          ADC_CHN_BUTTONS));

  //
  // Power up SARADC clocks and deassert reset
  //
  SaradcInit ();

  //
  // Create WaitForKey event
  //
  Status = gBS->CreateEvent (
                  0,
                  TPL_NOTIFY,
                  NULL,
                  NULL,
                  &mWaitEvent
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Install SimpleTextInEx
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mHandle,
                  &gEfiSimpleTextInputExProtocolGuid, &mTextInEx,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Create poll timer
  //
  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  AdcKeypadTimer,
                  NULL,
                  &mTimerEvent
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->SetTimer (mTimerEvent, TimerPeriodic, POLL_MS * 10000);
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "AdcKeypadDxe: ready (poll=%dms, debounce=%d)\n",
          POLL_MS, DEBOUNCE_SAMPLES));
  return EFI_SUCCESS;
}
