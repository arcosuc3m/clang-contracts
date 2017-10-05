//===-- scudo_tsd_shared.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo shared TSD implementation.
///
//===----------------------------------------------------------------------===//

#include "scudo_tsd.h"

#if !SCUDO_TSD_EXCLUSIVE

#include <pthread.h>

namespace __scudo {

static pthread_once_t GlobalInitialized = PTHREAD_ONCE_INIT;
static pthread_key_t PThreadKey;

static atomic_uint32_t CurrentIndex;
static ScudoTSD *TSDs;
static u32 NumberOfTSDs;

// sysconf(_SC_NPROCESSORS_{CONF,ONLN}) cannot be used as they allocate memory.
static uptr getNumberOfCPUs() {
  cpu_set_t CPUs;
  CHECK_EQ(sched_getaffinity(0, sizeof(cpu_set_t), &CPUs), 0);
  return CPU_COUNT(&CPUs);
}

static void initOnce() {
  // Hack: TLS_SLOT_TSAN was introduced in N. To be able to use it on M for
  // testing, we create an unused key. Since the key_data array follows the tls
  // array, it basically gives us the extra entry we need.
  // TODO(kostyak): remove and restrict to N and above.
  CHECK_EQ(pthread_key_create(&PThreadKey, NULL), 0);
  initScudo();
  NumberOfTSDs = getNumberOfCPUs();
  if (NumberOfTSDs == 0)
    NumberOfTSDs = 1;
  if (NumberOfTSDs > 32)
    NumberOfTSDs = 32;
  TSDs = reinterpret_cast<ScudoTSD *>(
      MmapOrDie(sizeof(ScudoTSD) * NumberOfTSDs, "ScudoTSDs"));
  for (u32 i = 0; i < NumberOfTSDs; i++)
    TSDs[i].init(/*Shared=*/true);
}

void initThread(bool MinimalInit) {
  pthread_once(&GlobalInitialized, initOnce);
  // Initial context assignment is done in a plain round-robin fashion.
  u32 Index = atomic_fetch_add(&CurrentIndex, 1, memory_order_relaxed);
  ScudoTSD *TSD = &TSDs[Index % NumberOfTSDs];
  *get_android_tls_ptr() = reinterpret_cast<uptr>(TSD);
}

ScudoTSD *getTSDAndLockSlow() {
  ScudoTSD *TSD;
  if (NumberOfTSDs > 1) {
    // Go through all the contexts and find the first unlocked one.
    for (u32 i = 0; i < NumberOfTSDs; i++) {
      TSD = &TSDs[i];
      if (TSD->tryLock()) {
        *get_android_tls_ptr() = reinterpret_cast<uptr>(TSD);
        return TSD;
      }
    }
    // No luck, find the one with the lowest Precedence, and slow lock it.
    u64 LowestPrecedence = UINT64_MAX;
    for (u32 i = 0; i < NumberOfTSDs; i++) {
      u64 Precedence = TSDs[i].getPrecedence();
      if (Precedence && Precedence < LowestPrecedence) {
        TSD = &TSDs[i];
        LowestPrecedence = Precedence;
      }
    }
    if (LIKELY(LowestPrecedence != UINT64_MAX)) {
      TSD->lock();
      *get_android_tls_ptr() = reinterpret_cast<uptr>(TSD);
      return TSD;
    }
  }
  // Last resort, stick with the current one.
  TSD = reinterpret_cast<ScudoTSD *>(*get_android_tls_ptr());
  TSD->lock();
  return TSD;
}

}  // namespace __scudo

#endif  // !SCUDO_TSD_EXCLUSIVE
