/* Multiplex file i/o
(C) 2019 Niall Douglas <http://www.nedproductions.biz/> (9 commits)
File Created: Nov 2019


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

#include "../../../io_context.hpp"

#include "import.hpp"

#ifdef __linux__
#include <sys/epoll.h>

#include <chrono>
#include <map>
#include <unordered_map>

LLFIO_V2_NAMESPACE_BEGIN

template <bool threadsafe> class linux_epoll_impl final : public io_context_impl<threadsafe>
{
  using _base = io_context_impl<threadsafe>;
  using _lock_guard = typename _base::_lock_guard;
  int _epollh{-1};
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
  using _io_kind = typename _base::_io_kind;
  template <class Promise = void> using _coroutine_handle = typename _base::template _coroutine_handle<Promise>;
  struct registered_handle
  {
    static constexpr size_t max_ios_outstanding = 64;
    struct epoll_event ev;
    struct io_outstanding_t
    {
      _io_kind kind{_io_kind::unknown};
      std::chrono::steady_clock::time_point deadline_duration;
      std::chrono::system_clock::time_point deadline_absolute;
      _coroutine_handle<> co;  // coroutine to resume when i/o completes
    } io_outstanding[max_ios_outstanding];
    size_t io_with_deadlines{0};

    registered_handle() = default;
    explicit registered_handle(struct epoll_event _ev)
        : ev(_ev)
    {
    }
    void remove_io(io_outstanding_t &i)
    {
      if(i.deadline_duration != std::chrono::steady_clock::time_point() || i.deadline_absolute != std::chrono::system_clock::time_point())
      {
        assert(false);
      }
      memmove(&i, &i + 1, (&io_outstanding[max_ios_outstanding - 1] - &i) * sizeof(io_outstanding_t));
      io_outstanding[max_ios_outstanding - 1].kind = _io_kind::unknown;
      io_outstanding[max_ios_outstanding - 1].co = {};
    }
  };
  using _registered_handles_map_type = std::unordered_map<int, registered_handle>;
  _registered_handles_map_type _registered_handles;
  std::multimap<std::chrono::steady_clock::time_point, typename _registered_handles_map_type::iterator> _durations;
  std::multimap<std::chrono::system_clock::time_point, typename _registered_handles_map_type::iterator> _absolutes;

  void _remove_io(typename _registered_handles_map_type::iterator it, typename registered_handle::io_outstanding_t &i)
  {
    if(i.deadline_duration != std::chrono::steady_clock::time_point())
    {
      auto dit = _durations.find(i.deadline_duration);
      if(dit == _durations.end())
      {
        abort();
      }
      while(dit->first == i.deadline_duration && dit->second != it)
      {
        ++dit;
      }
      if(dit->first != i.deadline_duration)
      {
        abort();
      }
      _durations.erase(dit);
      i.deadline_duration = {};
    }
    if(i.deadline_absolute != std::chrono::system_clock::time_point())
    {
      auto dit = _absolutes.find(i.deadline_absolute);
      if(dit == _absolutes.end())
      {
        abort();
      }
      while(dit->first == i.deadline_absolute && dit->second != it)
      {
        ++dit;
      }
      if(dit->first != i.deadline_absolute)
      {
        abort();
      }
      _absolutes.erase(dit);
      i.deadline_absolute = {};
    }
    it->second.remove_io(i);
  }
#endif

public:
  result<void> init(size_t /*unused*/)
  {
    _epollh = epoll_create1(EPOLL_CLOEXEC);
    if(-1 == _epollh)
    {
      return posix_error();
    }
    return success();
  }
  virtual ~linux_epoll_impl()
  {
    this->_lock.lock();
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
    for(auto &x : _registered_handles)
    {
      (void) epoll_ctl(_epollh, EPOLL_CTL_DEL, x.first, &x.second.ev);
      for(auto &i : x.second.io_outstanding)
      {
        if(i.kind == _io_kind::unknown)
        {
          break;
        }
        if(i.co)
        {
          i.co.destroy();
        }
      }
    }
    _registered_handles.clear();
#endif
    (void) ::close(_epollh);
  }

  virtual result<void> _register_io_handle(handle *h) noexcept override final
  {
    (void) h;
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
    try
    {
      struct epoll_event ev;
      memset(&ev, 0, sizeof(ev));
      ev.data.fd = h->native_handle().fd;
      ev.events = EPOLLHUP | EPOLLERR | EPOLLET;  // edge triggered
      if(h->is_readable())
      {
        ev.events |= EPOLLIN;
      }
      if(h->is_writable())
      {
        ev.events |= EPOLLOUT;
      }
      if(threadsafe)
      {
        ev.events |= EPOLLEXCLUSIVE;
      }
      // Begin watching this fd for changes
      if(-1 == epoll_ctl(_epollh, EPOLL_CTL_ADD, h->native_handle().fd, &ev))
      {
        return posix_error();
      }
      _lock_guard g(this->_lock);
      _registered_handles.insert({h->native_handle().fd, registered_handle(ev)});
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
#else
    return errc::not_supported;
#endif
  }
  virtual result<void> _deregister_io_handle(handle *h) noexcept override final
  {
    (void) h;
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
    try
    {
      struct epoll_event ev;
      if(-1 == epoll_ctl(_epollh, EPOLL_CTL_DEL, h->native_handle().fd, &ev))
      {
        return posix_error();
      }
      _lock_guard g(this->_lock);
      _registered_handles.erase(h->native_handle().fd);
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
#else
    return errc::not_supported;
#endif
  }
  virtual result<bool> run(deadline d = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(this->_execute_posted_items())
    {
      return true;
    }
    LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      struct epoll_event ev;
      memset(&ev, 0, sizeof(ev));
      LLFIO_POSIX_DEADLINE_TO_SLEEP_LOOP(d);
      int mstimeout = (timeout == nullptr) ? -1 : (timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000LL);
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
      std::chrono::steady_clock::time_point deadline_duration;
      std::chrono::system_clock::time_point deadline_absolute;
      // Shorten the timeout if necessary
      {
        typename _registered_handles_map_type::iterator resume_timed_out = _registered_handles.end();
        _lock_guard g(this->_lock);
        if(!_durations.empty())
        {
          deadline_duration = _durations.begin()->first;
          auto togo = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_duration - std::chrono::steady_clock::now()).count();
          if(togo <= 0)
          {
            resume_timed_out = _durations.begin()->second;
          }
          else if(-1 == mstimeout || togo < mstimeout)
          {
            mstimeout = togo;
          }
        }
        if(!_absolutes.empty())
        {
          deadline_absolute = _absolutes.begin()->first;
          auto togo = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_absolute - std::chrono::system_clock::now()).count();
          if(togo <= 0)
          {
            resume_timed_out = _absolutes.begin()->second;
          }
          else if(-1 == mstimeout || togo < mstimeout)
          {
            mstimeout = togo;
          }
        }
        // Somebody has already timed out, so resume them
        if(resume_timed_out != _registered_handles.end())
        {
          for(auto &i : resume_timed_out->second.io_outstanding)
          {
            if(i.kind == _io_kind::unknown)
            {
              break;
            }
            if((i.deadline_duration != std::chrono::steady_clock::time_point() && i.deadline_duration == deadline_duration) || (i.deadline_absolute != std::chrono::system_clock::time_point() && i.deadline_absolute == deadline_absolute))
            {
              auto co = i.co;
              _remove_io(resume_timed_out, i);
              g.unlock();
              co.resume();
              return true;
            }
          }
        }
      }
#endif
      int ret = epoll_wait(_epollh, &ev, 1, mstimeout);
      if(-1 == ret)
      {
        return posix_error();
      }
      if(ret == 0)
      {
        // If the supplied deadline has passed, return errc::timed_out
        LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d);
      }
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
      _lock_guard g(this->_lock);
      if(ret > 0)
      {
        auto it = _registered_handles.find(ev.data.fd);
        if(it == _registered_handles.end())
        {
          abort();
        }
        // Resume the earliest pending i/o matching read/write
        for(auto &i : it->second.io_outstanding)
        {
          if(i.kind == _io_kind::unknown)
          {
            break;
          }
          if((ev.events & (EPOLLHUP | EPOLLERR)) != 0)
          {
            // wake everything
            auto co = i.co;
            _remove_io(it, i);
            g.unlock();
            co.resume();
            g.lock();
            continue;
          }
          if(i.kind == _io_kind::read && (ev.events & EPOLLIN) != 0)
          {
            auto co = i.co;
            _remove_io(it, i);
            g.unlock();
            co.resume();
            return true;
          }
          if(i.kind == _io_kind::write && (ev.events & EPOLLOUT) != 0)
          {
            auto co = i.co;
            _remove_io(it, i);
            g.unlock();
            co.resume();
            return true;
          }
        }
      }
#endif
    }
  }
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
  virtual result<void> _await_io_handle_ready(typename _base::_await_io_handle_ready_awaitable *aw, _coroutine_handle<> co) noexcept override final
  {
    try
    {
      LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(aw->_d);
      // Set this coroutine to be resumed when the i/o completes
      bool nospace = true;
      _lock_guard g(this->_lock);
      auto it = _registered_handles.find(aw->_h->native_handle().fd);
      if(it == _registered_handles.end())
      {
        abort();
      }
      for(auto &i : it->second.io_outstanding)
      {
        if(i.kind == _io_kind::unknown)
        {
          i.kind = aw->_kind;
          if(aw->_d)
          {
            if(aw->_d.steady)
            {
              i.deadline_duration = std::chrono::steady_clock::now() + std::chrono::nanoseconds(aw->_d.nsecs);
              i.deadline_absolute = {};
              _durations.insert({i.deadline_duration, it});
            }
            else
            {
              i.deadline_duration = {};
              i.deadline_absolute = aw->_d.to_time_point();
              _absolutes.insert({i.deadline_absolute, it});
            }
          }
          else
          {
            i.deadline_duration = {};
            i.deadline_absolute = {};
          }
          i.co = co;
          nospace = false;
          break;
        }
      }
      if(nospace)
      {
        return errc::resource_unavailable_try_again;  // not enough i/o slots
      }
      // This i/o is registered for later resumption!
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
  }
#endif
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> io_context::linux_epoll(size_t threads) noexcept
{
  try
  {
    if(threads > 1)
    {
      auto ret = std::make_unique<linux_epoll_impl<true>>();
      OUTCOME_TRY(ret->init(threads));
      return ret;
    }
    else
    {
      auto ret = std::make_unique<linux_epoll_impl<false>>();
      OUTCOME_TRY(ret->init(threads));
      return ret;
    }
  }
  catch(...)
  {
    return error_from_exception();
  }
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> io_context::linux_io_uring() noexcept
{
  return errc::not_supported;
}

LLFIO_V2_NAMESPACE_END

#elif defined(__FreeBSD__) || defined(__APPLE__)
#error bsd_kqueue i/o context not implemented yet!
#else
#error Unknown POSIX platform
#endif
