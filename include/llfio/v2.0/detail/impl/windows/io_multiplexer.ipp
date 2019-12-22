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

#include "../../../io_multiplexer.hpp"

#include "import.hpp"

#ifndef _WIN32
#error This implementation file is for Microsoft Windows only
#endif

#include <chrono>
#include <map>
#include <unordered_map>

LLFIO_V2_NAMESPACE_BEGIN

namespace detail
{
#if LLFIO_EXPERIMENTAL_STATUS_CODE
#else
  LLFIO_HEADERS_ONLY_FUNC_SPEC error_info ntkernel_error_from_overlapped(size_t code) { return ntkernel_error((NTSTATUS) code); }
#endif
}  // namespace detail

template <bool threadsafe> class win_iocp_impl final : public io_multiplexer_impl<threadsafe>
{
  using _base = io_multiplexer_impl<threadsafe>;
  using _lock_guard = typename _base::_lock_guard;

  static_assert(sizeof(typename detail::io_operation_connection::_OVERLAPPED) == sizeof(OVERLAPPED), "detail::io_operation_connection::_OVERLAPPED does not match OVERLAPPED!");

  std::atomic<size_t> _concurrent_run_instances{0};  // how many threads inside run() there are right now
  // Linked list of all deadlined i/o's pending completion
  detail::io_operation_connection *_pending_begin{nullptr}, *_pending_end{nullptr};

  // ONLY if _do_check_deadlined_io() is called
  std::multimap<std::chrono::steady_clock::time_point, detail::io_operation_connection *> _durations;
  std::multimap<std::chrono::system_clock::time_point, detail::io_operation_connection *> _absolutes;

public:
  result<void> init(size_t threads)
  {
    this->_v.h = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, (DWORD) threads);
    if(nullptr == this->_v.h)
    {
      return win32_error();
    }
    return success();
  }
  virtual ~win_iocp_impl()
  {
    this->_lock.lock();
    if(!_durations.empty() || !_absolutes.empty())
    {
      LLFIO_LOG_FATAL(nullptr, "win_iocp_impl::~win_iocp_impl() called with i/o handles still doing work");
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

  virtual result<void> _register_io_handle(handle *h) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_COMPLETION_INFORMATION fci{};
    memset(&fci, 0, sizeof(fci));
    fci.Port = this->_v.h;
    fci.Key = (void *) 1;  // not null
    NTSTATUS ntstat = NtSetInformationFile(h->native_handle().h, &isb, &fci, sizeof(fci), FileCompletionInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h->native_handle().h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }
    // Don't wake wait() for i/o which completes immediately. We ignore
    // failure as not all handles support this, and we are idempotent to
    // spurious wakes in any case.
    SetFileCompletionNotificationModes(h->native_handle().h, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE);
    return success();
  }
  virtual result<void> _deregister_io_handle(handle *h) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_COMPLETION_INFORMATION fci{};
    memset(&fci, 0, sizeof(fci));
    fci.Port = nullptr;
    fci.Key = nullptr;
    NTSTATUS ntstat = NtSetInformationFile(h->native_handle().h, &isb, &fci, sizeof(fci), FileReplaceCompletionInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h->native_handle().h, isb, deadline());
    }
    if(ntstat < 0)
    {
      abort();
    }
    return success();
  }

  virtual bool check_posted_items() noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(this->_execute_posted_items())
    {
      return true;
    }
    return false;
  }

  virtual result<detail::io_operation_connection *> _do_check_deadlined_io(LARGE_INTEGER &_timeout, LARGE_INTEGER *&timeout, bool &need_to_wake_all) noexcept
  {
    try
    {
      detail::io_operation_connection *resume_timed_out = nullptr;
      for(auto *i = _pending_end; i != nullptr && !i->is_added_to_deadline_list; i = i->prev)
      {
        if(i->deadline_absolute != std::chrono::system_clock::time_point())
        {
          auto it = _absolutes.insert({i->deadline_absolute, i});
          if(it == _absolutes.begin())
          {
            need_to_wake_all = true;
          }
        }
        else if(i->deadline_duration != std::chrono::steady_clock::time_point())
        {
          auto it = _durations.insert({i->deadline_duration, i});
          if(it == _durations.begin())
          {
            need_to_wake_all = true;
          }
        }
        i->is_added_to_deadline_list = true;
      }

      // Process timed out pending operations, shortening the requested timeout if necessary
      std::chrono::steady_clock::time_point deadline_duration;
      std::chrono::system_clock::time_point deadline_absolute;
      if(!_durations.empty())
      {
        deadline_duration = _durations.begin()->first;
        auto togo = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_duration - std::chrono::steady_clock::now()).count();
        if(togo <= 0)
        {
          resume_timed_out = _durations.begin()->second;
        }
        else if(nullptr == timeout || togo / -100 > timeout->QuadPart)
        {
          timeout = &_timeout;
          timeout->QuadPart = togo / -100;
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
        else if(nullptr == timeout || togo < -timeout->QuadPart)
        {
          timeout = &_timeout;
          *timeout = windows_nt_kernel::from_timepoint(deadline_absolute);
        }
      }
      return resume_timed_out;
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

  virtual result<bool> check_deadlined_io() noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    _lock_guard g(this->_lock);
    LARGE_INTEGER _timeout{}, *timeout = nullptr;
    bool need_to_wake_all = false;
    OUTCOME_TRY(resume_timed_out, _do_check_deadlined_io(_timeout, timeout, need_to_wake_all));
    if(resume_timed_out != nullptr)
    {
      g.unlock();
      resume_timed_out->poll();
      return true;
    }
    return false;
  }


  virtual result<size_t> wait(deadline d = deadline()) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      if(check_posted_items())
      {
        return 1;
      }
      _lock_guard g(this->_lock);

      // First check if any timeouts have passed, complete those if they have
      bool need_to_wake_all = false;
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);  // recalculate our timeout
      OUTCOME_TRY(resume_timed_out, _do_check_deadlined_io(_timeout, timeout, need_to_wake_all));
      if(need_to_wake_all)
      {
        // Timeouts ought to be processed by all idle threads concurrently, so wake everything
        auto threads_sleeping = _concurrent_run_instances.load(std::memory_order_acquire);
        for(size_t n = 0; n < threads_sleeping; n++)
        {
          PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
        }
      }
      if(resume_timed_out != nullptr)
      {
        g.unlock();
        // We need to do one more check if the i/o has completed before timing out
        resume_timed_out->poll();
        return 1;
      }

      OVERLAPPED_ENTRY entries[64];
      ULONG filled = 0;
      NTSTATUS ntstat;
      {
        _concurrent_run_instances.fetch_add(1, std::memory_order_acq_rel);
        g.unlock();
        // Use this instead of GetQueuedCompletionStatusEx() as this implements absolute timeouts
        ntstat = NtRemoveIoCompletionEx(this->_v.h, entries, sizeof(entries) / sizeof(entries[0]), &filled, timeout, false);
        _concurrent_run_instances.fetch_sub(1, std::memory_order_acq_rel);
      }
      if(STATUS_TIMEOUT == ntstat)
      {
        // If the supplied deadline has passed, return errc::timed_out
        LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d);
        continue;
      }
      if(ntstat < 0)
      {
        return ntkernel_error(ntstat);
      }
      size_t post_wakeups = 0;
      for(ULONG n = 0; n < filled; n++)
      {
        // If it's null, this is a post() wakeup
        if(entries[n].lpCompletionKey == 0)
        {
          ++post_wakeups;
          continue;
        }
        auto *states = (std::vector<detail::io_operation_connection *> *) entries[n].lpCompletionKey;
        (void) states;
        // Complete the i/o
        auto *op = (typename detail::io_operation_connection *) entries[n].lpOverlapped->hEvent;
        op->poll();
      }
      if(filled - post_wakeups > 0)
      {
        return filled - post_wakeups;
      }
    }
  }

  virtual void _register_pending_io(detail::io_operation_connection *op) noexcept
  {
    // We only get called for i/o which didn't immediately complete
    // and which has a deadline
    // Add this state to the list of pending i/o
    assert(op->deadline_absolute != std::chrono::system_clock::time_point() || op->deadline_duration != std::chrono::steady_clock::time_point());
    bool need_to_wake = false;
    {
      _lock_guard g(this->_lock);
      op->next = nullptr;
      op->prev = _pending_end;
      if(_pending_end == nullptr)
      {
        _pending_begin = op;
      }
      else
      {
        _pending_end->next = op;
      }
      _pending_end = op;
      op->is_registered_with_io_multiplexer = true;
      if(_concurrent_run_instances.load(std::memory_order_acquire) > 0)
      {
        need_to_wake = true;
      }
    }
    /* If there are wait() instances running right now, wake any one of them
    to recalculate timeouts.
    */
    if(need_to_wake)
    {
      PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
    }
  }
  virtual void _deregister_pending_io(detail::io_operation_connection *op) noexcept
  {
    _lock_guard g(this->_lock);
    if(op->is_registered_with_io_multiplexer)
    {
      if(op->is_added_to_deadline_list)
      {
        if(op->deadline_absolute != std::chrono::system_clock::time_point())
        {
          auto it = _absolutes.find(op->deadline_absolute);
          if(it == _absolutes.end())
          {
            abort();
          }
          bool found = false;
          do
          {
            if(it->second == op)
            {
              _absolutes.erase(it);
              found = true;
              break;
            }
            ++it;
          } while(it != _absolutes.end() && it->first == op->deadline_absolute);
          if(!found)
          {
            abort();
          }
        }
        else if(op->deadline_duration != std::chrono::steady_clock::time_point())
        {
          auto it = _durations.find(op->deadline_duration);
          if(it == _durations.end())
          {
            abort();
          }
          bool found = false;
          do
          {
            if(it->second == op)
            {
              _durations.erase(it);
              found = true;
              break;
            }
            ++it;
          } while(it != _durations.end() && it->first == op->deadline_duration);
          if(!found)
          {
            abort();
          }
        }
        if(op->prev == nullptr)
        {
          _pending_begin = op->next;
        }
        else
        {
          op->prev->next = op->next;
        }
        if(op->next == nullptr)
        {
          _pending_end = op->prev;
        }
        else
        {
          op->next->prev = op->prev;
        }
      }
    }
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> io_multiplexer::win_iocp(size_t threads) noexcept
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
      OUTCOME_TRY(ret->init(1));
      return ret;
    }
  }
  catch(...)
  {
    return error_from_exception();
  }
}

LLFIO_V2_NAMESPACE_END
