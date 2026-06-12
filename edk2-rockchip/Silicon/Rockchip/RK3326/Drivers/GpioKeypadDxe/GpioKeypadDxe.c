/** @file
  GPIO Keypad Driver for RK3326 (PX30) — SimpleTextInEx protocol

  Polls GPIO0 PB5 (swl/volume-up) and PB7 (swr/volume-down).
  Installs SimpleTextInEx; ConSplitterDxe aggregates into SimpleTextIn.

  Copyright (c) 2024-2026, RK3326 EDK2 Port
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/SimpleTextInEx.h>

#define KEY_GPIO   0
#define PIN_PLAY  13
#define PIN_VOLDN 15
#define ACTIVE     0
#define DB_MS     50
#define POLL_MS   20
#define FIFO_DEP   8

STATIC EFI_KEY_DATA  mFifo[FIFO_DEP];
STATIC UINTN         mHead = 0, mTail = 0;
STATIC EFI_EVENT     mWait = NULL, mTimer = NULL;
STATIC EFI_HANDLE    mH    = NULL;

typedef struct { UINT8 Pin; UINT16 Scan; CHAR8 *Name; } GK;
STATIC GK mK[] = {{PIN_PLAY,0x01,"SWL->UP"},{PIN_VOLDN,0x17,"SWR->ESC"}};
STATIC BOOLEAN mPrev[2]={FALSE,FALSE}, mCur[2]={FALSE,FALSE};
STATIC UINTN  mCnt[2]={0,0};

// FIFO
STATIC VOID Push(UINT16 S,BOOLEAN D){
  UINTN n=(mTail+1)%FIFO_DEP;if(n==mHead)return;
  EFI_KEY_DATA *k=&mFifo[mTail];ZeroMem(k,sizeof(*k));
  k->Key.ScanCode=S;k->KeyState.KeyShiftState=D?EFI_SHIFT_STATE_VALID:0;
  mTail=n;gBS->SignalEvent(mWait);}

// SimpleTextInEx
STATIC EFI_STATUS EFIAPI RstEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *T,BOOLEAN E)
  {mHead=mTail=0;return EFI_SUCCESS;}
STATIC EFI_STATUS EFIAPI RdKEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *T,EFI_KEY_DATA *K){
  if(mHead==mTail)return EFI_NOT_READY;
  CopyMem(K,&mFifo[mHead],sizeof(*K));mHead=(mHead+1)%FIFO_DEP;return EFI_SUCCESS;}
STATIC EFI_STATUS EFIAPI StSEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *T,
  EFI_KEY_TOGGLE_STATE *S){return EFI_UNSUPPORTED;}
STATIC EFI_STATUS EFIAPI RgNEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *T,
  EFI_KEY_DATA *K,EFI_KEY_NOTIFY_FUNCTION F,VOID **H){return EFI_UNSUPPORTED;}
STATIC EFI_STATUS EFIAPI UnNEx(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *T,
  VOID *H){return EFI_UNSUPPORTED;}
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL mTEx={
  RstEx,RdKEx,NULL,StSEx,RgNEx,UnNEx};

// Timer
STATIC VOID EFIAPI Tmr(EFI_EVENT E,VOID *C){
  for(UINTN i=0;i<ARRAY_SIZE(mK);i++){
    BOOLEAN r=GpioPinRead(KEY_GPIO,mK[i].Pin);
    if(r==mPrev[i]){if(++mCnt[i]>=(DB_MS/POLL_MS)){
      if(r!=mCur[i]){mCur[i]=r;
        if(r==ACTIVE){DEBUG((DEBUG_ERROR,"GPIO: %a\n",mK[i].Name));
          Push(mK[i].Scan,TRUE);}}
      mCnt[i]=0;}}else mCnt[i]=0;
    mPrev[i]=r;}}

// Entry
EFI_STATUS EFIAPI GpioKeypadEntryPoint(EFI_HANDLE Img,EFI_SYSTEM_TABLE *Sys){
  EFI_STATUS S;
  for(UINTN i=0;i<ARRAY_SIZE(mK);i++){
    GpioPinSetDirection(KEY_GPIO,mK[i].Pin,GPIO_PIN_INPUT);
    GpioPinSetPull(KEY_GPIO,mK[i].Pin,GPIO_PIN_PULL_UP);
    mPrev[i]=mCur[i]=GpioPinRead(KEY_GPIO,mK[i].Pin);}
  S=gBS->CreateEvent(0,TPL_NOTIFY,NULL,NULL,&mWait);ASSERT_EFI_ERROR(S);
  mTEx.WaitForKeyEx=mWait;
  S=gBS->InstallMultipleProtocolInterfaces(&mH,
    &gEfiSimpleTextInputExProtocolGuid,&mTEx,NULL);ASSERT_EFI_ERROR(S);
  S=gBS->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_NOTIFY,Tmr,NULL,&mTimer);
  ASSERT_EFI_ERROR(S);
  S=gBS->SetTimer(mTimer,TimerPeriodic,POLL_MS*10000);ASSERT_EFI_ERROR(S);
  DEBUG((DEBUG_INFO,"GpioKeypadDxe: ready\n"));
  return EFI_SUCCESS;}
