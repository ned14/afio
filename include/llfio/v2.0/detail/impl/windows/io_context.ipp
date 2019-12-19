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

template <bool threadsafe> class win_iocp_impl final : public io_context_impl<threadsafe>
{
  using _base = io_context_impl<threadsafe>;
  using _lock_guard = typename _base::_lock_guard;

  static_assert(sizeof(typename detail::io_operation_connection::_OVERLAPPED) == sizeof(OVERLAPPED), "detail::io_operation_connection::_OVERLAPPED does not match OVERLAPPED!");

  std::atomic<size_t> _concurrent_run_instances{0};
  detail::io_operation_connection *_pending_begin{nullptr}, *_pending_end{nullptr};
  std::multimap<std::chrono::steady_clock::time_point, detail::io_operation_connection *> _durations;
  std::multimap<std::chrono::system_clock::time_point, detail::io_operation_connection *> _absolutes;

  // Lock must be held on entry!
  void _remove_io(detail::io_operation_connection *op)
  {
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
    if(op->deadline_absolute != std::chrono::system_clock::time_point())
    {
      auto it = _absolutes.find(op->deadline_absolute);
      if(it == _absolutes.end())
      {
        abort();
      }
      do
      {
        if(it->second == op)
        {
          _absolutes.erase(it);
          return;
        }
        ++it;
      } while(it != _absolutes.end() && it->first == op->deadline_absolute);
    }
    else if(op->deadline_duration != std::chrono::steady_clock::time_point())
    {
      auto it = _durations.find(op->deadline_duration);
      if(it == _durations.end())
      {
        abort();
      }
      do
      {
        if(it->second == op)
        {
          _durations.erase(it);
          return;
        }
        ++it;
      } while(it != _durations.end() && it->first == op->deadline_duration);
    }
    abort();
  }

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

  virtual result<void> _register_io_handle(handle * /*unused*/) noexcept override final
  {
    return success();
  }
  virtual result<void> _deregister_io_handle(handle * /*unused*/) noexcept override final { return success(); }
  virtual result<size_t> run(deadline d = deadline()) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    _concurrent_run_instances.fetch_add(1, std::memory_order_acq_rel);
    auto unconcurrent_run_instances = undoer([this] { _concurrent_run_instances.fetch_sub(1, std::memory_order_acq_rel); });
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      if(this->_execute_posted_items())
      {
        return 1;
      }
      _lock_guard g(this->_lock);

      // Firstly register any recently added pending i/o with IOCP
      for(auto *i = _pending_end; i != nullptr && !i->is_registered_with_io_context; i = i->prev)
      {
        IO_STATUS_BLOCK isb = make_iostatus();
        FILE_COMPLETION_INFORMATION fci{};
        memset(&fci, 0, sizeof(fci));
        fci.Port = this->_v.h;
        fci.Key = i;
        NTSTATUS ntstat = NtSetInformationFile(i->nativeh.h, &isb, &fci, sizeof(fci), FileReplaceCompletionInformation);
        if(STATUS_PENDING == ntstat)
        {
          ntstat = ntwait(i->nativeh.h, isb, deadline());
        }
        if(ntstat < 0)
        {
          return ntkernel_error(ntstat);
        }
      }

      // Process timed out pending operations, shortening the requested timeout if necessary
      std::chrono::steady_clock::time_point deadline_duration;
      std::chrono::system_clock::time_point deadline_absolute;
      detail::io_operation_connection *resume_timed_out = nullptr;
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
      g.unlock();
      // Complete with timed out
      if(resume_timed_out != nullptr)
      {
        resume_timed_out->_complete_io(errc::timed_out);
        return 1;
      }
      OVERLAPPED_ENTRY entries[64];
      ULONG filled = 0;
      // Use this instead of GetQueuedCompletionStatusEx() as this implements absolute timeouts
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);  // recalculate our timeout
      NTSTATUS ntstat = NtRemoveIoCompletionEx(this->_v.h, entries, sizeof(entries) / sizeof(entries[0]), &filled, timeout, false);
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
      g.lock();
      for(ULONG n = 0; n < filled; n++)
      {
        // If it's null, this is a post() wakeup
        if(entries[n].lpCompletionKey == 0)
        {
          continue;
        }
        // The hEvent member is the io_operation_connection used
        auto *op = (typename detail::io_operation_connection *) entries[n].lpOverlapped->hEvent;
        if(op->deadline_absolute != std::chrono::system_clock::time_point() || op->deadline_duration != std::chrono::steady_clock::time_point())
        {
          _remove_io(op);
        }
      }
      g.unlock();
      size_t post_wakeups = 0;
      for(ULONG n = 0; n < filled; n++)
      {
        if(entries[n].lpCompletionKey == 0)
        {
          ++post_wakeups;
          continue;
        }
        auto *op = (typename detail::io_operation_connection *) entries[n].lpOverlapped->hEvent;
        op->_complete_io(entries[n].dwNumberOfBytesTransferred);
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
    // Add this state to the list of pending i/o
    bool need_to_wake_all = false;
    try
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
      if(op->deadline_absolute != std::chrono::system_clock::time_point())
      {
        auto it = _absolutes.insert({op->deadline_absolute, op});
        if(it == _absolutes.begin())
        {
          need_to_wake_all = true;
        }
      }
      else if(op->deadline_duration != std::chrono::steady_clock::time_point())
      {
        auto it = _durations.insert({op->deadline_duration, op});
        if(it == _durations.begin())
        {
          need_to_wake_all = true;
        }
      }
    }
    catch(...)
    {
      op->_complete_io(error_from_exception());
    }
    /* If there are run() instances running right now, wake either one of them
    to register this handle with IOCP, or wake all of them to recalculate their
    timers.
    */
    auto concurrent_run_instances = _concurrent_run_instances.load(std::memory_order_acquire);
    if(!need_to_wake_all)
    {
      concurrent_run_instances = 1;
    }
    for(size_t n = 0; n < concurrent_run_instances; n++)
    {
      PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
    }
  }
  virtual void _deregister_pending_io(detail::io_operation_connection *op) noexcept
  {
    _lock_guard g(this->_lock);
    if(op->is_registered_with_iocp)
    {
      windows_nt_kernel::init();
      using namespace windows_nt_kernel;
      IO_STATUS_BLOCK isb = make_iostatus();
      FILE_COMPLETION_INFORMATION fci{};
      memset(&fci, 0, sizeof(fci));
      fci.Port = nullptr;
      fci.Key = nullptr;
      NTSTATUS ntstat = NtSetInformationFile(op->nativeh.h, &isb, &fci, sizeof(fci), FileReplaceCompletionInformation);
      if(STATUS_PENDING == ntstat)
      {
        ntstat = ntwait(op->nativeh.h, isb, deadline());
      }
      if(ntstat < 0)
      {
        abort();
      }
    }
    _remove_io(op);
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
