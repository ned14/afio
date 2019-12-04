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
  using _co_read_awaitable = typename _base::_co_read_awaitable<use_atomic>;
  using _co_write_awaitable = typename _base::_co_write_awaitable<use_atomic>;
  using _co_barrier_awaitable = typename _base::_co_barrier_awaitable<use_atomic>;
  using _co_read_promise_type = typename _co_read_awaitable::promise_type;
  using _co_write_promise_type = typename _co_write_awaitable::promise_type;
  using _co_barrier_promise_type = typename _co_barrier_awaitable::promise_type;

  struct registered_handle
  {
    static constexpr size_t max_ios_outstanding = 64;
    enum io_kind{unused, read, write, barrier};
    struct epoll_event ev;
    struct io_outstanding_t
    {
      io_outstanding_t *prev{nullptr}, *next{nullptr};
      std::chrono::steady_clock::time_point deadline_duration;
      std::chrono::system_clock::time_point deadline_absolute;
      io_kind kind{io_kind::unused};
      union {
        OUTCOME_V2_NAMESPACE::detail::empty_type _default{};
        _co_read_promise_type read_promise;
        _co_write_promise_type write_promise;
        _co_barrier_promise_type barrier_promise;
      };
    } * next_io_outstanding{nullptr}, *free_io_outstanding{nullptr};

    registered_handle() = default;
    explicit registered_handle(struct epoll_event _ev)
        : ev(_ev)
    {
    }
  };
  using _registered_handles_map_type = std::unordered_map<int, registered_handle>;
  _registered_handles_map_type _registered_handles;
  std::multimap<std::chrono::steady_clock::time_point, typename registered_handle::io_outstanding_t *> _durations;
  std::multimap<std::chrono::system_clock::time_point, typename registered_handle::io_outstanding_t *> _absolutes;
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
    if(!_registered_handles.empty())
    {
      LLFIO_LOG_FATAL(nullptr, "linux_epoll_impl::~linux_epoll_impl() called with registered i/o handles");
      abort();
    }
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
      registered_handle r(ev);
      // Preallocate i/o structures
      for(size_t n = 0; n < registered_handle::max_ios_outstanding; n++)
      {
        auto *i = new registered_handle::io_outstanding_t;
        i->next = r.free_io_outstanding;
        r.free_io_outstanding = i;
      }
      _lock_guard g(this->_lock);
      _registered_handles.insert({h->native_handle().fd, std::move(r)});
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
      registered_handle r;
      {
        _lock_guard g(this->_lock);
        auto it = _registered_handles.find(h->native_handle().fd);
        if(it == _registered_handles.end())
        {
          abort();
        }
        if(it->second.next_io_outstanding != nullptr)
        {
          return errc::operation_in_progress;
        }
        r = std::move(it->second);
        _registered_handles.erase(it);
      }
      if(-1 == epoll_ctl(_epollh, EPOLL_CTL_DEL, h->native_handle().fd, &r.ev))
      {
        return posix_error();
      }
      assert(r.next_io_outstanding == nullptr);
      while(r.free_io_outstanding != nullptr)
      {
        auto *i = r.free_io_outstanding;
        r.free_io_outstanding = i->next;
        delete i;
      }
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
        typename registered_handle::io_outstanding_t *resume_timed_out = nullptr;
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
        auto io_completed = [&](auto it, registered_handle::io_outstanding_t *i, auto value) {
          // Detach myself from the pending lists
          if(i->deadline_duration != std::chrono::steady_clock::time_point())
          {
            auto dit = _durations.find(i->deadline_duration);
            if(dit == _durations.end())
            {
              abort();
            }
            while(dit->first == i->deadline_duration && dit->second != it)
            {
              ++dit;
            }
            if(dit->first != i->deadline_duration)
            {
              abort();
            }
            _durations.erase(dit);
            i->deadline_duration = {};
          }
          if(i->deadline_absolute != std::chrono::system_clock::time_point())
          {
            auto dit = _absolutes.find(i->deadline_absolute);
            if(dit == _absolutes.end())
            {
              abort();
            }
            while(dit->first == i->deadline_absolute && dit->second != it)
            {
              ++dit;
            }
            if(dit->first != i->deadline_absolute)
            {
              abort();
            }
            _absolutes.erase(dit);
            i->deadline_absolute = {};
          }
          if(i->next != nullptr)
          {
            i->next->prev = i->prev;
          }
          if(i->prev != nullptr)
          {
            i->prev->next = i->next;
          }
          else
          {
            assert(it->second.next_io_outstanding == i);
            it->second.next_io_outstanding = nullptr;
          }
          i->next = nullptr;
          i->prev = nullptr;
          g.unlock();
          switch(i->kind)
          {
          default:
            abort();
          case registered_handle::read:
            i->read_promise.return_value(std::move(value));
            if(i->read_promise.continuation)
            {
              i->read_promise.continuation.resume();
            }
            i->read_promise.~_co_read_promise_type();
            break;
          case registered_handle::write:
            i->write_promise.return_value(std::move(value));
            if(i->write_promise.continuation)
            {
              i->write_promise.continuation.resume();
            }
            i->write_promise.~_co_write_promise_type();
            break;
          }
          i->kind = registered_handle::unused;
          g.lock();
          i->next = it->free_io_outstanding;
          it->free_io_outstanding = i;
        };
        // Set timed out
        if(resume_timed_out != nullptr)
        {
          if((resume_timed_out->deadline_duration != std::chrono::steady_clock::time_point() && resume_timed_out->deadline_duration == deadline_duration) || (resume_timed_out->deadline_absolute != std::chrono::system_clock::time_point() && resume_timed_out->deadline_absolute == deadline_absolute))
          {
            typename _registered_handles_map_type::iterator it;
            switch(i->kind)
            {
            default:
              abort();
            case registered_handle::read:
              it = _registered_handles.find(resume_timed_out->read_promise.nativeh.fd);
              break;
            case registered_handle::write:
              it = _registered_handles.find(resume_timed_out->write_promise.nativeh.fd);
              break;
            }
            if(it == _registered_handles.end())
            {
              abort();
            }
            io_completed(it, resume_timed_out, errc::timed_out);
            return true;
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
        for(registered_handle::io_outstanding_t *i = it->second.next_io_outstanding; i != nullptr; i = i->next)
        {
          // Does this i/o match? If it's an error, wake the earliest irrespective.
          switch(i->kind)
          {
          default:
            abort();
          case registered_handle::read:
            if((ev.events & (EPOLLHUP | EPOLLERR)) != 0 || (i->kind == registered_handle::read && (ev.events & EPOLLIN) != 0))
            {
              // Reattempt the i/o
              io_handle wrapper(i->read_promise.nativeh);
              g.unlock();
              io_result<buffers_type> result = wrapper.read(i->read_promise.reqs, std::chrono::seconds(0));
              wrapper.release();
              g.lock();
              if(result)
              {
                // Complete with the result
                io_completed(it, i, std::move(result));
                return true;
              }
            }
            break;
          case registered_handle::write:
            if((ev.events & (EPOLLHUP | EPOLLERR)) != 0 || (i->kind == registered_handle::write && (ev.events & EPOLLOUT) != 0))
            {
              // Reattempt the i/o
              io_handle wrapper(i->write_promise.nativeh);
              g.unlock();
              io_result<buffers_type> result = wrapper.write(i->write_promise.reqs, std::chrono::seconds(0));
              wrapper.release();
              g.lock();
              if(result)
              {
                // Complete with the result
                io_completed(it, i, std::move(result));
                return true;
              }
            }
            break;
          }
        }
      }
#endif
    }
  }
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
  template <class D, class S> typename S::awaitable_type _move_if_same_and_return_awaitable(D *dest, S &&s) { abort(); }
  template <class D> typename D::awaitable_type _move_if_same_and_return_awaitable(D *dest, D &&s)
  {
    // Put the promise into its final resting place, and return an awaitable pointing at that promise
    auto *p = new(dest) D(std::move(s));
    return p->get_return_object();
  }
  template <class Promise> typename Promise::awaitable_type _add_promise_to_wake_list(typename registered_handle::io_kind kind, Promise &&p) noexcept
  {
    try
    {
      LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(p.d);
      _lock_guard g(this->_lock);
      auto it = _registered_handles.find(p.h->native_handle().fd);
      if(it == _registered_handles.end())
      {
        abort();
      }
      for(auto &i : it->second.io_outstanding)
      {
        if(i.kind == registered_handle::unused)
        {
          i.kind = kind;
          if(p.d)
          {
            if(p.d.steady)
            {
              i.deadline_duration = std::chrono::steady_clock::now() + std::chrono::nanoseconds(p.d.nsecs);
              i.deadline_absolute = {};
              _durations.insert({i.deadline_duration, it});
            }
            else
            {
              i.deadline_duration = {};
              i.deadline_absolute = p.d.to_time_point();
              _absolutes.insert({i.deadline_absolute, it});
            }
          }
          else
          {
            i.deadline_duration = {};
            i.deadline_absolute = {};
          }
          if((&i - io_outstanding) + 1 > io_outstanding_top)
          {
            io_outstanding_top = (&i - io_outstanding) + 1;
          }
          switch(kind)
          {
          default:
            abort();
          case registered_handle::read:
            return _move_if_same_and_return_awaitable(&i.read_promise, std::move(p));
          case registered_handle::write:
            return _move_if_same_and_return_awaitable(&i.write_promise, std::move(p));
          }
        }
      }
      return errc::resource_unavailable_try_again;  // not enough i/o slots
    }
    catch(...)
    {
      return error_from_exception();
    }
  }
  virtual _co_read_awaitable _run_until_read_ready(_co_read_promise_type &&p) noexcept { return _add_promise_to_wake_list(registered_handle::read, std::move(p)); }
  virtual _co_write_awaitable _run_until_write_ready(_co_write_promise_type &&p) noexcept { return _add_promise_to_wake_list(registered_handle::write, std::move(p)); }
  virtual _co_barrier_awaitable _run_until_barrier_ready(_co_barrier_promise_type && /*unused*/) noexcept
  {
    // Not implemented for the epoll() context
    abort();
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
