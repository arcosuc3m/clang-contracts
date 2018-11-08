//===-- tsan_csv.cc -------------------------------------------------------===//
// This file is a part of ThreadSanitizer (TSan), a race detector.
// CSV implementation for TSan.  Last modified: 8-nov-2018 20:01
//===----------------------------------------------------------------------===//

#ifndef NO_CONTRACT_H
# include "../../../../../tools/clang/lib/Headers/contract"
#else
class __builtin_contract_violation { // Required if the compilation does not
                                     // use clang++ from clang-contracts
 public:
  int __line;
  const char *__file, *__func, *__comment, *__level;

  // Leave these undefined and use the `__xxx' members instead (the P0542R5 TS
  // mandates to return std::string_view, but depending on it is problematic).
  int line_number() const noexcept;
  auto file_name() const noexcept;
  auto function_name() const noexcept;
  auto comment() const noexcept;
  auto assertion_level() const noexcept;
};
typedef __builtin_contract_violation __builtin_contract_violation_t;

namespace std {
  using contract_violation = __builtin_contract_violation_t;
}
#endif

namespace std { using size_t = unsigned; }
#include "tsan_csv.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_rtl.h"
using namespace __sanitizer;

void __csv_violation_handler(const std::contract_violation &cv) {
#if !SANITIZER_DEBUG
  Printf("==================\n"
	 "\033[01;31mWARNING: CSV: rule violation at %s:%d\n"
	 "  \033[00;35m`%s'\033[00m\n\n  Stack trace:\n",
	 cv.__file, cv.__line, cv.__comment);
  __tsan::PrintCurrentStack(__tsan::cur_thread(), StackTrace::GetCurrentPc());
  Printf("==================\n");
  __tsan::ctx->nreported++;
#else
  __tsan::ReportRace(__tsan::cur_thread());
#endif
}

namespace csv {
  struct event_set_impl {
    __tsan::ThreadClock tc_first_, tc_last_; // Save memory, as set operations only require [first,last]
    unsigned  count_;
    SpinMutex mutex_;

    event_set_impl() : mutex_{}
    { internal_memset(this, 0, sizeof(event_set_impl)); }
    
#define grow_TC(__c, __size) if (__c.nclk_ < __size) __c.nclk_ = __size
    ALWAYS_INLINE void updateTC_first(const __tsan::ThreadClock& c) noexcept {
      if (tc_first_.size() != 0) {
        grow_TC(tc_first_, c.size());

        for (unsigned i = 0; i < c.size(); ++i)
          tc_first_.clk_[i] = __tsan::min(tc_first_.clk_[i],
                                          c.clk_[i]);
      } else
        tc_first_ = c;
    }

    ALWAYS_INLINE void updateTC_last(const __tsan::ThreadClock& c) noexcept {
      if (tc_last_.size() != 0) {
        grow_TC(tc_last_, c.size());

        tc_last_.clk_[c.tid_] = __tsan::max(tc_last_.clk_[c.tid_],
                                            c.clk_[c.tid_]);
      } else
        tc_last_ = c;
    }
#undef grow_TC

    static ALWAYS_INLINE event_set_impl *get_es_private_data(event_set &es) noexcept
    { return static_cast<event_set_impl *>(es.private_data_); }
    static ALWAYS_INLINE event_set_impl *get_es_private_data(const event_set &es) noexcept
    { return static_cast<event_set_impl *>(es.private_data_); }
  };

  /**
    \brief Returns true if ThreadClock `a` is ordered before `b`.
   */
  bool __happens_before(const __tsan::ThreadClock& a, const __tsan::ThreadClock& b) {
#if SANITIZER_DEBUG
    using printf_t = int (*)(const char *, ...);

    const_cast<__tsan::ThreadClock&>(a).DebugDump((printf_t)Printf);
    Printf("\033[00;33m\t->\t\033[00m");
    const_cast<__tsan::ThreadClock&>(b).DebugDump((printf_t)Printf);
    Printf("\n");
#endif
    // U happen-before V iff, forall i : U[i] <= V[i], and exists j : U[j] < V[j]
    // more at https://queue.acm.org/detail.cfm?id=2917756
    //
    for (unsigned i = 0; i < __tsan::max(a.size(), b.size()); ++i)
      if (a.get(i) > b.get(i)) return false;

    return true;
  }

  ALWAYS_INLINE const __tsan::ThreadClock&
    event_to_ThreadClock(const event &e)
    { return *reinterpret_cast<const __tsan::ThreadClock *>(&e); }

  constexpr const unsigned __kTmpEsPoolSize = 4, // MUST be power-of-2
		__kTmpEsPoolMask = __kTmpEsPoolSize-1;

  // Array of TLS temporary event_set instances that may be reused by a
  // set_union()/set_intersection() call.  Therefore, up to __kTmpEsPoolSize
  // temporary instances may be referenced in a user predicate.
  __attribute__((tls_model("initial-exec")))
  thread_local event_set __tmp_es[__kTmpEsPoolSize];
  __attribute__((tls_model("initial-exec"))) THREADLOCAL unsigned __tmp_es_i = 0;

  ALWAYS_INLINE event_set &get_reused_event_set() {
    auto &ret = __tmp_es[__tmp_es_i++ & __kTmpEsPoolMask];
    new (event_set_impl::get_es_private_data(ret)) event_set_impl{};
    return ret;
  }

  event_set::event_set()
    : private_data_(InternalAlloc(sizeof(event_set_impl), nullptr, alignof(event_set_impl)))
  { new (private_data_) event_set_impl{}; }
  event_set::~event_set() { InternalFree(private_data_); }

  // event{,_set}::happens_before(): these rely on __happens_before(__tsan::ThreadClock&, __tsan::ThreadClock&) [see above]
  //
  bool event::happens_before(const event_set &evs) const {
    SpinMutexLock lk(&event_set_impl::get_es_private_data(evs)->mutex_);

    return __happens_before(event_to_ThreadClock(*this),
			    event_set_impl::get_es_private_data(evs)->tc_first_);
  }
  bool event_set::happens_before(const event &ev) const {
    SpinMutexLock lk_this(&static_cast<event_set_impl *>(private_data_)->mutex_);

    return __happens_before(static_cast<event_set_impl *>(private_data_)->tc_last_,
			    event_to_ThreadClock(ev));
  }
  bool event_set::happens_before(const event_set &evs) const {
    SpinMutexLock lk_this(&static_cast<event_set_impl *>(private_data_)->mutex_);
    SpinMutexLock lk(&static_cast<event_set_impl *>(evs.private_data_)->mutex_);

    return __happens_before(static_cast<event_set_impl *>(private_data_)->tc_last_,
			    static_cast<event_set_impl *>(evs.private_data_)->tc_first_);
  }

  std::size_t event_set::size() const {
    SpinMutexLock lk_this(&static_cast<event_set_impl *>(private_data_)->mutex_);
    return static_cast<event_set_impl *>(private_data_)->count_;
  }

  void event_set::add_event(const event &ev) {
    auto &es = *static_cast<event_set_impl *>(private_data_);
    auto &tc = event_to_ThreadClock(ev);

    SpinMutexLock lk_this(&es.mutex_);
    es.updateTC_first(tc), es.updateTC_last(tc);
    es.count_++;
  }

  event &current_event() {
    // XXX: the returned reference is really a pointer to the current ThreadClock.
    //      It should never be dereferenced in the user code.
    return *reinterpret_cast<event *>(&__tsan::cur_thread()->clock);
  }

  template <>
  event_set &set_union(const event_set &a, const event_set &b) {
    event_set &ret = get_reused_event_set();
    auto es = event_set_impl::get_es_private_data(ret);

    // es->mutex_.Lock() is not required as this event_set is in the TLS area (thread-local)
    {
      auto __a = event_set_impl::get_es_private_data(a);
      SpinMutexLock lk(&__a->mutex_);
      es->updateTC_first(__a->tc_first_), es->updateTC_last(__a->tc_last_),
	es->count_ += __a->count_;
    }
    {
      auto __b = event_set_impl::get_es_private_data(b);
      SpinMutexLock lk(&__b->mutex_);
      es->updateTC_first(__b->tc_first_), es->updateTC_last(__b->tc_last_),
	es->count_ += __b->count_; // or less (don't care)
    }
    return ret;
  }

  template <>
  event_set &set_intersection(const event_set &a, const event_set &b) {
    event_set &ret = get_reused_event_set();
    // TODO: 06-nov-18: jfmunoz/jalopezg/drio: does intersection make sense at all?
    return ret;
  }
} // namespace csv
