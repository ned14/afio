/* Specifies a time deadline
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (4 commits)
File Created: Dec 2015


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef LLFIO_DEADLINE_H
#define LLFIO_DEADLINE_H

#include <stdbool.h>  // NOLINT
#include <time.h>     // NOLINT

//! \file deadline.h Provides struct deadline

#if defined(__cplusplus) || DOXYGEN_IS_IN_THE_HOUSE
#ifndef LLFIO_CONFIG_HPP
#error You must include the master llfio.hpp, not individual header files directly
#endif
#include "config.hpp"
#include <stdexcept>
LLFIO_V2_NAMESPACE_EXPORT_BEGIN
#define LLFIO_DEADLINE_NAME deadline
#else
#define LLFIO_DEADLINE_NAME boost_llfio_deadline
#endif

/*! \struct deadline
\brief A time deadline in either relative-to-now or absolute (system clock) terms
*/
struct LLFIO_DEADLINE_NAME
{
  //! True if deadline does not change with system clock changes
  bool steady;  // NOLINT
  union {
    //! System time from timespec_get(&ts, TIME_UTC)
    struct timespec utc;  // NOLINT
    //! Nanosecond ticks from start of operation
    unsigned long long nsecs;  // NOLINT
  };
#ifdef __cplusplus
  constexpr deadline()  // NOLINT
      : steady(false)
      , utc{0, 0}
  {
  }
  //! True if deadline is valid
  constexpr explicit operator bool() const noexcept { return steady || utc.tv_sec != 0; }
  //! Implicitly construct a deadline from a system clock time point
  deadline(std::chrono::system_clock::time_point tp)  // NOLINT
      : steady(false)
      , utc{0, 0}
  {
    std::chrono::seconds secs(std::chrono::system_clock::to_time_t(tp));
    utc.tv_sec = secs.count();
    std::chrono::system_clock::time_point _tp(std::chrono::system_clock::from_time_t(utc.tv_sec));
    utc.tv_nsec = static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(tp - _tp).count());
  }
  //! Implicitly construct a deadline from a duration from now
  template <class Rep, class Period>
  constexpr deadline(std::chrono::duration<Rep, Period> d)  // NOLINT
      : steady(true)
      , nsecs(0)
  {
    std::chrono::nanoseconds _nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
    // Negative durations are zero duration
    if(_nsecs.count() > 0)
    {
      nsecs = _nsecs.count();
    }
    else
    {
      nsecs = 0;
    }
  }
  //! Returns a system_clock::time_point for this deadline
  std::chrono::system_clock::time_point to_time_point() const
  {
    if(steady)
    {
      throw std::invalid_argument("Not a UTC deadline!");  // NOLINT
    }
    std::chrono::system_clock::time_point tp(std::chrono::system_clock::from_time_t(utc.tv_sec));
    tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(utc.tv_nsec));
    return tp;
  }
#endif
};

#define LLFIO_DEADLINE_TO_SLEEP_INIT(d)                                                                                                                                                                                                                                                                                        \
  std::chrono::steady_clock::time_point began_steady;                                                                                                                                                                                                                                                                          \
  std::chrono::system_clock::time_point end_utc;                                                                                                                                                                                                                                                                               \
  if((d) && (d).nsecs != 0)                                                                                                                                                                                                                                                                                                    \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
      began_steady = std::chrono::steady_clock::now();                                                                                                                                                                                                                                                                         \
    else                                                                                                                                                                                                                                                                                                                       \
      end_utc = (d).to_time_point();                                                                                                                                                                                                                                                                                           \
  }

#define LLFIO_DEADLINE_TO_PARTIAL_DEADLINE(nd, d)                                                                                                                                                                                                                                                                              \
  if(d)                                                                                                                                                                                                                                                                                                                        \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                                                                          \
      (nd).steady = true;                                                                                                                                                                                                                                                                                                      \
      if((d).nsecs == 0)                                                                                                                                                                                                                                                                                                       \
      {                                                                                                                                                                                                                                                                                                                        \
        (nd).nsecs = 0;                                                                                                                                                                                                                                                                                                        \
      }                                                                                                                                                                                                                                                                                                                        \
      else                                                                                                                                                                                                                                                                                                                     \
      {                                                                                                                                                                                                                                                                                                                        \
        std::chrono::nanoseconds ns = std::chrono::duration_cast<std::chrono::nanoseconds>((began_steady + std::chrono::nanoseconds((d).nsecs)) - std::chrono::steady_clock::now());                                                                                                                                           \
        if(ns.count() < 0)                                                                                                                                                                                                                                                                                                     \
          (nd).nsecs = 0;                                                                                                                                                                                                                                                                                                      \
        else                                                                                                                                                                                                                                                                                                                   \
          (nd).nsecs = ns.count();                                                                                                                                                                                                                                                                                             \
      }                                                                                                                                                                                                                                                                                                                        \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
      (nd) = (d);                                                                                                                                                                                                                                                                                                              \
  }

#define LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d)                                                                                                                                                                                                                                                                                      \
  if(d)                                                                                                                                                                                                                                                                                                                        \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::steady_clock::now() >= (began_steady + std::chrono::nanoseconds((d).nsecs)))                                                                                                                                                                                                                             \
        return errc::timed_out;                                                                                                                                                                                                                                                                                                \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::system_clock::now() >= end_utc)                                                                                                                                                                                                                                                                          \
        return errc::timed_out;                                                                                                                                                                                                                                                                                                \
    }                                                                                                                                                                                                                                                                                                                          \
  }

#define LLFIO_DEADLINE_TRY_FOR_UNTIL(name)                                                                                                                                                                                                                                                                                     \
  template <class... Args> auto try_##name(Args &&... args) noexcept { return name(std::forward<Args>(args)..., std::chrono::seconds(0)); }                                                                                                                                                                                    \
  template <class... Args, class Rep, class Period> auto name##_for(Args &&... args, const std::chrono::duration<Rep, Period> &duration) noexcept { return name(std::forward<Args>(args)..., duration); }                                                                                                                      \
  template <class... Args, class Clock, class Duration> auto name##_until(Args &&... args, const std::chrono::time_point<Clock, Duration> &timeout) noexcept { return name(std::forward<Args>(args)..., timeout); }

#undef LLFIO_DEADLINE_NAME
#if defined(__cplusplus) || DOXYGEN_IS_IN_THE_HOUSE
LLFIO_V2_NAMESPACE_END
#endif

#endif
