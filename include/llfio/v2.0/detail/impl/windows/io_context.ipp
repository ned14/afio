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

#ifndef _WIN32
#error This implementation file is for Microsoft Windows
#endif

#include <chrono>
#include <map>
#include <unordered_map>

LLFIO_V2_NAMESPACE_BEGIN

template <bool threadsafe> class win_iocp_impl final : public io_context_impl<threadsafe>
{
  using _base = io_context_impl<threadsafe>;
  using _lock_guard = typename _base::_lock_guard;
  using _co_read_awaitable = typename _base::template _co_read_awaitable<threadsafe>;
  using _co_write_awaitable = typename _base::template _co_write_awaitable<threadsafe>;
  using _co_barrier_awaitable = typename _base::template _co_barrier_awaitable<threadsafe>;
  using _co_read_promise_type = typename _co_read_awaitable::promise_type;
  using _co_write_promise_type = typename _co_write_awaitable::promise_type;
  using _co_barrier_promise_type = typename _co_barrier_awaitable::promise_type;

  struct registered_handle
  {
    enum io_kind
    {
      unused,
      read,
      write,
      barrier
    };
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
      io_outstanding_t() {}
      ~io_outstanding_t() {}
    } * next_io_outstanding{nullptr}, *last_io_outstanding{nullptr}, *free_io_outstanding{nullptr};

    registered_handle() = default;
  };
  using _registered_handles_map_type = std::unordered_map<HANDLE, registered_handle>;
  _registered_handles_map_type _registered_handles;
  std::multimap<std::chrono::steady_clock::time_point, typename registered_handle::io_outstanding_t *> _durations;
  std::multimap<std::chrono::system_clock::time_point, typename registered_handle::io_outstanding_t *> _absolutes;

  void _remove_pending_io(typename _registered_handles_map_type::value_type *rh, typename registered_handle::io_outstanding_t *i)
  {
    if(i->next == nullptr && i->prev == nullptr)
    {
      return;
    }
    // Detach myself from the pending lists
    if(i->deadline_duration != std::chrono::steady_clock::time_point())
    {
      auto dit = _durations.find(i->deadline_duration);
      if(dit == _durations.end())
      {
        abort();
      }
      while(dit->first == i->deadline_duration && dit->second != i)
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
      while(dit->first == i->deadline_absolute && dit->second != i)
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
    else
    {
      assert(it->second.last_io_outstanding == i);
      it->second.last_io_outstanding = i->prev;
    }
    if(i->prev != nullptr)
    {
      i->prev->next = i->next;
    }
    else
    {
      assert(it->second.next_io_outstanding == i);
      it->second.next_io_outstanding = i->next;
    }
    i->next = nullptr;
    i->prev = nullptr;
  }

public:
  result<void> init(size_t threads)
  {
    this->_v.h = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, threads);
    if(nullptr == this->_v.h)
    {
      return win32_error();
    }
    return success();
  }
  virtual ~win_iocp_impl()
  {
    this->_lock.lock();
    if(!_registered_handles.empty())
    {
      LLFIO_LOG_FATAL(nullptr, "win_iocp_impl::~win_iocp_impl() called with registered i/o handles");
      abort();
    }
    (void) CloseHandle(this->_v.h);
  }

  virtual void _post(function_ptr<void *(void *)> &&f) noexcept override final
  {
    _base::_post(std::move(f));
    // Poke IOCP to wake
    PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
  }

  virtual result<void *> _register_io_handle(handle *h) noexcept override final
  {
    try
    {
      registered_handle r;
      // Preallocate i/o structures
      for(size_t n = 0; n < this->maximum_pending_io(); n++)
      {
        auto *i = new typename registered_handle::io_outstanding_t;
        i->next = r.free_io_outstanding;
        r.free_io_outstanding = i;
      }
      _lock_guard g(this->_lock);
      auto it = _registered_handles.insert({h->native_handle().h, std::move(r)});
      if(nullptr == CreateIoCompletionPort(h->native_handle().h, this->_v.h, &(*it), 0))
      {
        _registered_handles.erase(it);
        return win32_error();
      }
      return it->second.ols;
    }
    catch(...)
    {
      return error_from_exception();
    }
  }
  virtual result<void> _deregister_io_handle(handle *h) noexcept override final
  {
    try
    {
      registered_handle r;
      {
        _lock_guard g(this->_lock);
        auto it = _registered_handles.find(h->native_handle().h);
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
  }
  virtual result<size_t> run(deadline d = deadline()) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      if(this->_execute_posted_items())
      {
        return 1;
      }
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);
      _lock_guard g(this->_lock);
      std::chrono::steady_clock::time_point deadline_duration;
      std::chrono::system_clock::time_point deadline_absolute;
      // Shorten the timeout if necessary
      typename registered_handle::io_outstanding_t *resume_timed_out = nullptr;
      if(!_durations.empty())
      {
        deadline_duration = _durations.begin()->first;
        auto togo = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_duration - std::chrono::steady_clock::now()).count();
        if(togo <= 0)
        {
          resume_timed_out = _durations.begin()->second;
        }
        else if(nullptr == timeout || togo.count() / -100 > timeout->QuadPart)
        {
          timeout = &_timeout;
          timeout->QuadPart = togo.count() / -100;
        }
      }
      if(!_absolutes.empty())
      {
        deadline_absolute = _absolutes.begin()->first;
        auto togo = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_absolute - std::chrono::system_clock::now()).count();
        if(togo <= 0)
        {
          resume_timed_out = _absolutes.begin()->second;
        }
        else if(nullptr == timeout || togo.count() < -timeout->QuadPart)
        {
          timeout = &_timeout;
          timeout->QuadPart = windows_nt_kernel::from_timepoint(deadline_absolute);
        }
      }
      // Set timed out
      if(resume_timed_out != nullptr)
      {
        if((resume_timed_out->deadline_duration != std::chrono::steady_clock::time_point() && resume_timed_out->deadline_duration == deadline_duration) || (resume_timed_out->deadline_absolute != std::chrono::system_clock::time_point() && resume_timed_out->deadline_absolute == deadline_absolute))
        {
          typename _registered_handles_map_type::iterator it;
          switch(resume_timed_out->kind)
          {
          default:
            abort();
          case registered_handle::read:
            it = _registered_handles.find(resume_timed_out->read_promise.nativeh.fd);
            if(it == _registered_handles.end())
            {
              abort();
            }
            _remove_pending_io(it, resume_timed_out);
            resume_timed_out->read_promise.return_value(errc::timed_out);
            break;
          case registered_handle::write:
            it = _registered_handles.find(resume_timed_out->write_promise.nativeh.fd);
            if(it == _registered_handles.end())
            {
              abort();
            }
            _remove_pending_io(it, resume_timed_out);
            resume_timed_out->write_promise.return_value(errc::timed_out);
            break;
          }
          return 1;
        }
      }
      g.unlock();
      OVERLAPPED_ENTRY entries[64];
      ULONG filled = 0;
      // Use this instead of GetQueuedCompletionStatusEx() as this implements absolute timeouts
      NTSTATUS ntstat = NtRemoveIoCompletionEx(this->_v.h, entries, sizeof(entries)/sizeof(entries[0]), &filled, timeout, false));
      if(STATUS_TIMEOUT == ntstat)
      {
        // If the supplied deadline has passed, return errc::timed_out
        LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d);
        continue;
      }
      if(ntstat < 0)
      {
        return ntkernel_error();
      }
      g.lock();
      size_t post_wakeups = 0;
      for(ULONG n = 0; n < filled; n++)
      {
        // Get the direct pointer to the HANDLE's tracked state
        auto *rh = (typename _registered_handles_map_type::value_type *) entries[n].lpCompletionKey;
        // If it's null, this is a post() wakeup
        if(rh == nullptr)
        {
          ++post_wakeups;
          continue;
        }
        // The hEvent member is the io_outstanding_t used
        auto *i = (typename registered_handle::io_outstanding_t *) rh->lpOverlapped->hEvent;
        switch(i->kind)
        {
        default:
          abort();
        case registered_handle::read:
          auto result = calculate_result(i->read_promise, entries[n].dwNumberOfBytesTransferred);
          _remove_pending_io(rh, i);
          i->read_promise.return_value(std::move(result));
          break;
        case registered_handle::write:
          auto result = calculate_result(i->write_promise, entries[n].dwNumberOfBytesTransferred);
          _remove_pending_io(rh, i);
          i->write_promise.return_value(std::move(result));
          break;
        case registered_handle::barrier:
          auto result = calculate_result(i->barrier_promise, entries[n].dwNumberOfBytesTransferred);
          _remove_pending_io(rh, i);
          i->barrier_promise.return_value(std::move(result));
          break;
        }
      }
      if(filled - post_wakeups > 0)
      {
        return filled - post_wakeups;
      }
    }
  }
  template <class D, class S> typename S::awaitable_type _move_if_same_and_return_awaitable(D * /*unused*/, S && /*unused*/) { abort(); }
  template <class D> typename D::awaitable_type _move_if_same_and_return_awaitable(D *dest, D &&s)
  {
    // Put the promise into its final resting place, and return an awaitable pointing at that promise
    auto *p = new(dest) D(std::move(s));
    return p->get_return_object();
  }
  template <class Promise> typename Promise::awaitable_type _add_promise_to_wake_list(typename registered_handle::io_kind kind, Promise &&p, deadline d) noexcept
  {
    try
    {
      LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d);
      _lock_guard g(this->_lock);
      auto it = _registered_handles.find(p.nativeh.fd);
      if(it == _registered_handles.end())
      {
        abort();
      }
      if(it->second.free_io_outstanding == nullptr)
      {
        return errc::resource_unavailable_try_again;  // not enough i/o slots
      }
      auto *i = it->second.free_io_outstanding;
      p.internal_reference = i;
      if(d)
      {
        if(d.steady)
        {
          i->deadline_duration = std::chrono::steady_clock::now() + std::chrono::nanoseconds(d.nsecs);
          i->deadline_absolute = {};
          _durations.insert({i->deadline_duration, i});
        }
        else
        {
          i->deadline_duration = {};
          i->deadline_absolute = d.to_time_point();
          _absolutes.insert({i->deadline_absolute, i});
        }
      }
      else
      {
        i->deadline_duration = {};
        i->deadline_absolute = {};
      }
      i->kind = kind;
      it->second.free_io_outstanding = i->next;
      if(it->second.last_io_outstanding != nullptr)
      {
        it->second.last_io_outstanding->next = i;
      }
      i->prev = it->second.last_io_outstanding;
      i->next = nullptr;
      switch(kind)
      {
      default:
        abort();
      case registered_handle::read:
        return _move_if_same_and_return_awaitable(&i->read_promise, std::move(p));
      case registered_handle::write:
        return _move_if_same_and_return_awaitable(&i->write_promise, std::move(p));
      }
    }
    catch(...)
    {
      return error_from_exception();
    }
  }
  virtual typename _base::template _co_read_awaitable<false> _submit_read(typename _base::template _co_read_awaitable_promise_type<false> &&p, deadline d) noexcept override final { return _add_promise_to_wake_list(registered_handle::read, std::move(p), d); }
  virtual typename _base::template _co_write_awaitable<false> _submit_write(typename _base::template _co_write_awaitable_promise_type<false> &&p, deadline d) noexcept override final { return _add_promise_to_wake_list(registered_handle::write, std::move(p), d); }
  virtual typename _base::template _co_barrier_awaitable<false> _submit_barrier(typename _base::template _co_barrier_awaitable_promise_type<false> && /*unused*/, deadline /*unused*/) noexcept override final
  {
    // Not implemented for the epoll() context
    abort();
  }

  virtual result<void> _cancel_io(void *_i) noexcept override final
  {
    auto *i = (typename registered_handle::io_outstanding_t *) _i;
    typename _registered_handles_map_type::iterator it;
    _lock_guard g(this->_lock);
    switch(i->kind)
    {
    default:
      abort();
    case registered_handle::read:
      // He's still pending i/o, so destroy promise and remove
      it = _registered_handles.find(i->read_promise.nativeh.fd);
      if(it == _registered_handles.end())
      {
        abort();
      }
      i->read_promise.~_co_read_promise_type();
      _remove_pending_io(it, i);
      i->kind = registered_handle::unused;
      break;
    case registered_handle::write:
      // He's still pending i/o, so destroy promise and remove
      it = _registered_handles.find(i->write_promise.nativeh.fd);
      if(it == _registered_handles.end())
      {
        abort();
      }
      i->write_promise.~_co_write_promise_type();
      _remove_pending_io(it, i);
      i->kind = registered_handle::unused;
      break;
    }
    // Return to free lists
    i->next = it->second.free_io_outstanding;
    it->second.free_io_outstanding = i;
    return success();
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> io_context::win_iocp(size_t threads) noexcept
{
  try
  {
    if(threads > 1)
    {
      auto ret = std::make_unique<win_iocp_impl<true>>();
      OUTCOME_TRY(ret->init(threads));
      return ret;
    }
    else
    {
      auto ret = std::make_unique<win_iocp_impl<false>>();
      OUTCOME_TRY(ret->init(threads));
      return ret;
    }
  }
  catch(...)
  {
    return error_from_exception();
  }
}

LLFIO_V2_NAMESPACE_END
