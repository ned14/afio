/* Declarations for POSIX system APIs
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (8 commits)
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

#ifndef LLFIO_POSIX_HPP
#define LLFIO_POSIX_HPP

#include "../../../handle.hpp"

#ifdef _WIN32
#error You should not include posix/import.hpp on Windows platforms
#endif

#include <fcntl.h>
#include <unistd.h>

LLFIO_V2_NAMESPACE_BEGIN

inline result<int> attribs_from_handle_mode_caching_and_flags(native_handle_type &nativeh, handle::mode _mode, handle::creation _creation, handle::caching _caching, handle::flag flags) noexcept
{
  int attribs = O_CLOEXEC;
  switch(_mode)
  {
  case handle::mode::unchanged:
    return errc::invalid_argument;
  case handle::mode::none:
    break;
  case handle::mode::attr_read:
  case handle::mode::read:
    attribs = O_RDONLY;
    nativeh.behaviour |= native_handle_type::disposition::seekable | native_handle_type::disposition::readable;
    break;
  case handle::mode::attr_write:
  case handle::mode::write:
    attribs = O_RDWR;
    nativeh.behaviour |= native_handle_type::disposition::seekable | native_handle_type::disposition::readable | native_handle_type::disposition::writable;
    break;
  case handle::mode::append:
    attribs = O_WRONLY | O_APPEND;
    nativeh.behaviour |= native_handle_type::disposition::writable | native_handle_type::disposition::append_only;
    break;
  }
  switch(_creation)
  {
  case handle::creation::open_existing:
    break;
  case handle::creation::only_if_not_exist:
    attribs |= O_CREAT | O_EXCL;
    break;
  case handle::creation::if_needed:
    attribs |= O_CREAT;
    break;
  case handle::creation::truncate_existing:
    attribs |= O_TRUNC;
    break;
  case handle::creation::always_new:
    attribs |= O_CREAT | O_EXCL;
    break;
  }
  switch(_caching)
  {
  case handle::caching::unchanged:
    return errc::invalid_argument;
  case handle::caching::none:
    attribs |= O_SYNC
#ifdef O_DIRECT
               | O_DIRECT;
#else
    ;
#endif
    nativeh.behaviour |= native_handle_type::disposition::aligned_io;
    break;
  case handle::caching::only_metadata:
#ifdef O_DIRECT
    attribs |= O_DIRECT;
#endif
    nativeh.behaviour |= native_handle_type::disposition::aligned_io;
    break;
  case handle::caching::reads:
    attribs |= O_SYNC;
    break;
  case handle::caching::reads_and_metadata:
#ifdef O_DSYNC
    attribs |= O_DSYNC;
#else
    attribs |= O_SYNC;
#endif
    break;
  case handle::caching::all:
  case handle::caching::safety_barriers:
  case handle::caching::temporary:
    break;
  }
  if(!!(flags & handle::flag::multiplexable))
  {
    attribs |= O_NONBLOCK;
    nativeh.behaviour |= native_handle_type::disposition::nonblocking;
  }
  return attribs;
}

/*! Defines a number of variables into its scope:
- began_steady: Set to the steady clock at the beginning of a sleep
- end_utc: Set to the system clock when the sleep must end
- sleep_interval: Set to the number of steady milliseconds until the sleep must end
- sleep_object: Set to a primed deadline timer HANDLE which will signal when the system clock reaches the deadline
*/
#define LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d)                                                                                                                                                                                                                                                                                  \
  std::chrono::steady_clock::time_point began_steady;                                                                                                                                                                                                                                                                          \
  std::chrono::system_clock::time_point end_utc;                                                                                                                                                                                                                                                                               \
  struct timespec _timeout                                                                                                                                                                                                                                                                                                     \
  {                                                                                                                                                                                                                                                                                                                            \
  };                                                                                                                                                                                                                                                                                                                           \
  memset(&_timeout, 0, sizeof(_timeout));                                                                                                                                                                                                                                                                                      \
  struct timespec *timeout = nullptr;                                                                                                                                                                                                                                                                                          \
  if(d)                                                                                                                                                                                                                                                                                                                        \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
      began_steady = std::chrono::steady_clock::now();                                                                                                                                                                                                                                                                         \
    else                                                                                                                                                                                                                                                                                                                       \
      end_utc = (d).to_time_point();                                                                                                                                                                                                                                        \
    timeout = &_timeout;                                                                                                                                                                                                                                                                                                       \
  }

#define LLFIO_POSIX_DEADLINE_TO_SLEEP_LOOP(d)                                                                                                                                                                                                                                                                                  \
  if((d))                                                                                                                                                                                                                                                                                                                      \
  {                                                                                                                                                                                                                                                                                                                            \
    std::chrono::nanoseconds ns;                                                                                                                                                                                                                                                                                               \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
      ns = std::chrono::duration_cast<std::chrono::nanoseconds>((began_steady + std::chrono::nanoseconds((d).nsecs)) - std::chrono::steady_clock::now());                                                                                                                                                                      \
    else                                                                                                                                                                                                                                                                                                                       \
      ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_utc - std::chrono::system_clock::now());                                                                                                                                                                                                                   \
    if(ns.count() < 0)                                                                                                                                                                                                                                                                                                         \
    {                                                                                                                                                                                                                                                                                                                          \
      _timeout.tv_sec = 0;                                                                                                                                                                                                                                                                                                     \
      _timeout.tv_nsec = 0;                                                                                                                                                                                                                                                                                                    \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      _timeout.tv_sec = ns.count() / 1000000000ULL;                                                                                                                                                                                                                                                                            \
      _timeout.tv_nsec = ns.count() % 1000000000ULL;                                                                                                                                                                                                                                                                           \
    }                                                                                                                                                                                                                                                                                                                          \
  }

#define LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d)                                                                                                                                                                                                                                                                                \
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

#define LLFIO_POSIX_CO_DEADLINE_TO_TIMEOUT_LOOP(d)                                                                                                                                                                                                                                                                                \
  if(d)                                                                                                                                                                                                                                                                                                                        \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::steady_clock::now() >= (began_steady + std::chrono::nanoseconds((d).nsecs)))                                                                                                                                                                                                                             \
        co_return errc::timed_out;                                                                                                                                                                                                                                                                                                \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::system_clock::now() >= end_utc)                                                                                                                                                                                                                                                                          \
        co_return errc::timed_out;                                                                                                                                                                                                                                                                                                \
    }                                                                                                                                                                                                                                                                                                                          \
  }

LLFIO_V2_NAMESPACE_END

#endif
