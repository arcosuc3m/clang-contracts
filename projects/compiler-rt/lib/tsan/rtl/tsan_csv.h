/// tsan_csv.h - You should include this file before using the CSV interface
/// Last modified: 8-nov-2018 14:41
///
#ifndef TSAN_CSV_H
#define TSAN_CSV_H

#ifndef NO_CONTRACT_H
# include <contract>
#endif

namespace csv {
class event_set;

// Execution event
class event {
 public:
  // XXX: required for event_set::concurrent()
  bool happens_before(const event_set &evs) const;
  event(event &ev) = delete;
};

// Class for handling sets of events
class event_set {
 public:
  event_set();
  ~event_set();

  std::size_t size() const;
  bool empty() const { return size() == 0; }

  void add_event(const event &ev);

  // Check if this event set happens-before another event or event set
  bool happens_before(const event &ev) const;
  bool happens_before(const event_set &evs) const;

  // Check if this event set is concurrent with another event of event set
  bool concurrent(const event &ev) const
    { return !happens_before(ev) && !ev.happens_before(*this); }
  bool concurrent(const event_set &evs) const
    { return !happens_before(evs) && !evs.happens_before(*this); }

 private:
  friend struct event_set_impl;
  void *private_data_;
};

// Get temporary reference to current event.  It may be stored in a event_set.
event &current_event();

// Calculate set union and intersection
template <class = void>
event_set &set_union(const event_set &, const event_set &);
template <class = void>
event_set &set_intersection(const event_set &, const event_set &);

template <typename... Ts>
event_set &set_union(const event_set &a, const event_set &b, const Ts &...u)
  { return set_union(a, set_union(b, u...)); }
template <typename... Ts>
event_set &set_intersection(const event_set &a, const event_set &b, const Ts &...u)
  { return set_intersection(a, set_intersection(b, u...)); }

  // TODO: the [[csv::add_current(ES)]] attribute is not implemented in this
  // version of Clang.  Instead, revert to:
  //   [[expects /*axiom*/: csv::add_current(ES)]]
  static bool inline __attribute__((always_inline)) add_current(event_set &evs)
  { evs.add_event(current_event());
    return true; }

  // TODO: the [[csv::event_sets(...)]] attribute is not implemented yet. Instead use this macro.
#define CSV__EVENT_SETS(...) mutable csv::event_set __VA_ARGS__;
  
// DEPRECATED: Checks a semantic expression, if false prints a message
// bool valid_if(bool expr, const char *msg) __attribute__((deprecated));
} // namespace csv

extern "C" {
  // This contract violation handler is used instead the old valid_if() function
  void __csv_violation_handler(const std::contract_violation &cv);
}

#endif // TSAN_CSV_H
