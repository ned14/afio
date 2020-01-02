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

template <bool threadsafe> class win_common_impl : public io_multiplexer_impl<threadsafe>
{
  using _base = io_multiplexer_impl<threadsafe>;

protected:
  using _lock_guard = typename _base::_lock_guard;

  // Deadlined i/o ONLY
  detail::io_operation_connection *_scheduled_begin{nullptr}, *_scheduled_end{nullptr};
  // All i/o for which the OS is done with the state
  detail::io_operation_connection *_completed_begin{nullptr}, *_completed_end{nullptr};

  // ONLY if _do_timeout_io() is called
  std::multimap<std::chrono::steady_clock::time_point, detail::io_operation_connection *> _durations;
  std::multimap<std::chrono::system_clock::time_point, detail::io_operation_connection *> _absolutes;


  virtual result<int> invoke_posted_items(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    return this->_execute_posted_items(max_items, d);
  }

  result<span<detail::io_operation_connection *>> _do_timeout_io(LARGE_INTEGER &_timeout, LARGE_INTEGER *&timeout, bool &need_to_wake_all, span<detail::io_operation_connection *> in) noexcept
  {
    try
    {
      span<detail::io_operation_connection *> out(in.data(), (size_t) 0);
      for(auto *i = _scheduled_end; i != nullptr && -1 == i->is_added_to_deadline_list; i = i->prev)
      {
        if(i->deadline_duration != std::chrono::steady_clock::time_point())
        {
          auto it = _durations.insert({i->deadline_duration, i});
          if(it == _durations.begin())
          {
            need_to_wake_all = true;
          }
          i->is_added_to_deadline_list = 1;
        }
        else if(i->d)
        {
          auto it = _absolutes.insert({i->d.to_time_point(), i});
          if(it == _absolutes.begin())
          {
            need_to_wake_all = true;
          }
          i->is_added_to_deadline_list = 1;
        }
        else
        {
          i->is_added_to_deadline_list = 0;
        }
      }

      // Process timed out pending operations, shortening the requested timeout if necessary
      const auto durations_now = _durations.empty() ? std::chrono::steady_clock::time_point() : std::chrono::steady_clock::now();
      const auto absolutes_now = _absolutes.empty() ? std::chrono::system_clock::time_point() : std::chrono::system_clock::now();
      auto durationit = _durations.begin();
      auto absoluteit = _absolutes.begin();
      while(out.size() < in.size())
      {
        int64_t togo1 = (durationit != _durations.end()) ? std::chrono::duration_cast<std::chrono::nanoseconds>(durationit->first - durations_now).count() : INT64_MAX;
        int64_t togo2 = (absoluteit != _absolutes.end()) ? std::chrono::duration_cast<std::chrono::nanoseconds>(absoluteit->first - absolutes_now).count() : INT64_MAX;
        if(togo1 > 0)
        {
          if(nullptr == timeout || togo1 / -100 > timeout->QuadPart)
          {
            timeout = &_timeout;
            timeout->QuadPart = togo1 / -100;
          }
          durationit = _durations.end();
          togo1 = INT64_MAX;
        }
        if(togo2 > 0)
        {
          if(nullptr == timeout || togo2 < -timeout->QuadPart)
          {
            timeout = &_timeout;
            *timeout = windows_nt_kernel::from_timepoint(absoluteit->first);
          }
          absoluteit = _absolutes.end();
          togo2 = INT64_MAX;
        }
        if(durationit != _durations.end() || absoluteit != _absolutes.end())
        {
          // Choose whichever is the earliest
          if(togo1 < togo2)
          {
            out = {out.data(), out.size() + 1};
            out[out.size() - 1] = durationit->second;
            ++durationit;
          }
          else
          {
            out = {out.data(), out.size() + 1};
            out[out.size() - 1] = absoluteit->second;
            ++absoluteit;
          }
        }
      }
      return out;
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

  virtual result<int> timeout_io(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    using poll_kind = typename detail::io_operation_connection::_poll_kind;
    LLFIO_LOG_FUNCTION_CALL(this);
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    _lock_guard g(this->_lock);
    LARGE_INTEGER _timeout{}, *timeout = nullptr;
    bool need_to_wake_all = false;
    detail::io_operation_connection *in[64];
    OUTCOME_TRY(out, _do_timeout_io(_timeout, timeout, need_to_wake_all, {(detail::io_operation_connection **) in, (size_t) std::min(64, max_items)}));
    if(out.empty())
    {
      auto total = _durations.size() + _absolutes.size();
      return -(int) total;
    }
    g.unlock();
    LLFIO_DEADLINE_TO_SLEEP_INIT(d);
    int count = 0;
    for(auto *i : out)
    {
      i->_poll(poll_kind::timeout);
      ++count;
      if(max_items == count)
      {
        break;
      }
      if(d)
      {
        if((d.steady && (d.nsecs == 0 || std::chrono::steady_clock::now() >= began_steady)) || (!d.steady && std::chrono::system_clock::now() >= end_utc))
        {
          break;
        }
      }
    }
    return count;
  }

  void _add_to_scheduled(detail::io_operation_connection *op, bool tofront = false) noexcept
  {
    assert(!op->is_scheduled);
    if(tofront)
    {
      op->next = _scheduled_begin;
      op->prev = nullptr;
      if(_scheduled_begin == nullptr)
      {
        _scheduled_end = op;
      }
      else
      {
        _scheduled_begin->prev = op;
      }
      _scheduled_begin = op;
    }
    else
    {
      op->next = nullptr;
      op->prev = _scheduled_end;
      if(_scheduled_end == nullptr)
      {
        _scheduled_begin = op;
      }
      else
      {
        _scheduled_end->next = op;
      }
      _scheduled_end = op;
    }
    op->is_scheduled = true;
  }
  void _remove_from_scheduled(detail::io_operation_connection *op) noexcept
  {
    assert(op->is_scheduled);
    if(op->prev == nullptr)
    {
      _scheduled_begin = op->next;
    }
    else
    {
      op->prev->next = op->next;
    }
    if(op->next == nullptr)
    {
      _scheduled_end = op->prev;
    }
    else
    {
      op->next->prev = op->prev;
    }
    op->is_scheduled = false;
  }
  void _add_to_completed(detail::io_operation_connection *op) noexcept
  {
    assert(!op->is_os_completed);
    op->next = nullptr;
    op->prev = _completed_end;
    if(_completed_end == nullptr)
    {
      _completed_begin = op;
    }
    else
    {
      _completed_end->next = op;
    }
    _completed_end = op;
    op->is_os_completed = true;
  }
  void _remove_from_completed(detail::io_operation_connection *op) noexcept
  {
    assert(op->is_os_completed);
    if(op->prev == nullptr)
    {
      _completed_begin = op->next;
    }
    else
    {
      op->prev->next = op->next;
    }
    if(op->next == nullptr)
    {
      _completed_end = op->prev;
    }
    else
    {
      op->next->prev = op->prev;
    }
    op->is_os_completed = false;
  }
  void _remove_from_deadline_list(detail::io_operation_connection *op) noexcept
  {
    if(1 == op->is_added_to_deadline_list)
    {
      if(op->deadline_duration != std::chrono::steady_clock::time_point())
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
      else
      {
        const auto deadline_absolute = op->d.to_time_point();
        auto it = _absolutes.find(deadline_absolute);
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
        } while(it != _absolutes.end() && it->first == deadline_absolute);
        if(!found)
        {
          abort();
        }
      }
      op->is_added_to_deadline_list = (op->d) ? -1 : 0;
    }
  }
  void _reschedule_io(detail::io_operation_connection *op) noexcept
  {
    // An unfinished i/o in the completed list is being put back into scheduled
    assert(op->is_os_completed);
    _remove_from_completed(op);
    // MUST add to front of list, else it breaks ordered list calculation
    _add_to_scheduled(op, true);
  }
  virtual void _scheduled_io(detail::io_operation_connection *op) noexcept override final
  {
    // std::cerr << "_scheduled_io " << op << std::endl;
    assert(!op->is_scheduled);
    _lock_guard g(this->_lock);
    _add_to_scheduled(op);
    if(op->d)
    {
      // FIXME On IOCP where we are threadsafe we need to wake any currently
      // idling thread to recalculate the global ordered timeout list
    }
  }
  virtual void _os_has_completed_io(detail::io_operation_connection *op) noexcept override final
  {
    // std::cerr << "_os_has_completed_io " << op << " delayed_completion=" << op->is_scheduled << std::endl;
    assert(op->is_scheduled);
    assert(!op->is_os_completed);
    _lock_guard g(this->_lock);
    _remove_from_scheduled(op);
    _add_to_completed(op);
  }
  void _done_io(detail::io_operation_connection *op, _lock_guard &g) noexcept
  {
    // std::cerr << "_done_io " << op << std::endl;
    assert(op->is_scheduled || op->is_os_completed);
    assert(!(op->is_scheduled && op->is_os_completed));
    assert(!op->is_done_set);
    _remove_from_deadline_list(op);
    if(op->is_os_completed)
    {
      _remove_from_completed(op);
    }
    if(op->is_scheduled)
    {
      _remove_from_scheduled(op);
    }
    // Won't cancel a completed operation, but will call the Receiver's .set_done()
    g.unlock();
    op->cancel();
    g.lock();
  }
};

class win_alertable_impl final : public win_common_impl<false>
{
  using _base = win_common_impl<false>;
  using _lock_guard = typename _base::_lock_guard;

  DWORD _owning_tid{0};

public:
  result<void> init()
  {
    // Duplicate a true handle to the calling thread
    if(!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &this->_v.h, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
      return win32_error();
    }
    _owning_tid = GetCurrentThreadId();
    return success();
  }
  virtual ~win_alertable_impl()
  {
    this->_lock.lock();
    if(_scheduled_begin != nullptr || _completed_begin != nullptr || !_durations.empty() || !_absolutes.empty())
    {
      LLFIO_LOG_FATAL(nullptr, "win_alertable_impl::~win_alertable_impl() called with i/o handles still doing work");
      abort();
    }
    (void) CloseHandle(this->_v.h);
  }

  static void _invoke_posted_items_thunk(ULONG_PTR p)
  {
    auto *self = (win_alertable_impl *) p;
    if(!self->invoke_posted_items())
    {
      abort();
    }
  }
  virtual void _post(function_ptr<void *(void *)> &&f) noexcept override final
  {
    _base::_post(std::move(f));
    // Poke my thread to execute posted items
    if(!QueueUserAPC(_invoke_posted_items_thunk, this->_v.h, (ULONG_PTR) this))
    {
      abort();
    }
  }

  virtual result<int> _register_io_handle(handle * /*unused*/) noexcept override final { return 0; }
  virtual result<void> _deregister_io_handle(handle * /*unused*/) noexcept override final { return success(); }

  result<int> _do_complete_io(LARGE_INTEGER *timeout, int max_items, deadline d) noexcept
  {
    using poll_kind = typename detail::io_operation_connection::_poll_kind;
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    if(_owning_tid != GetCurrentThreadId())
    {
      return errc::operation_not_permitted;
    }
    LLFIO_DEADLINE_TO_SLEEP_INIT(d);

    int count = 0;
    this_thread::delay_invoking_io_completion invoker(count);
    if(this->_scheduled_begin != nullptr)
    {
      /* This will execute all APCs enqueued. If new APCs are enqueued by the
      execution of the APCs, this will block forever. Our APC routines
      therefore simply call _completed_io(), which removes them from the
      scheduled i/o list adding them to the completed i/o list, which we
      process immediately afterwards.
      */
      NTSTATUS ntstat = NtDelayExecution(1u, timeout);
      if(ntstat < 0 && ntstat != STATUS_TIMEOUT)
      {
        return ntkernel_error(ntstat);
      }
      if(ntstat == STATUS_TIMEOUT)
      {
        return -1;
      }
    }
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    _lock_guard g(this->_lock);
    const auto *end = _completed_end;
    while(_completed_begin != nullptr && count++ < max_items)
    {
      auto *begin = _completed_begin;
      const bool done_all = (begin == end);
      g.unlock();
      // See if all i/o requests are now complete, if so complete the op
      invoker.remove(begin);
      if(begin->_poll(poll_kind::check) == io_state_status::completed)
      {
        g.lock();
        _done_io(begin, g);
      }
      else
      {
        // Not quite done yet
        g.lock();
        _reschedule_io(begin);
      }
      if(done_all)
      {
        break;
      }
      if(d)
      {
        if((d.steady && (d.nsecs == 0 || std::chrono::steady_clock::now() >= began_steady)) || (!d.steady && std::chrono::system_clock::now() >= end_utc))
        {
          break;
        }
      }
    }
    return (count == 0 && (this->_scheduled_begin != nullptr || _completed_begin != nullptr)) ? -1 : count;
  }

  virtual result<int> complete_io(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    LARGE_INTEGER timeout;
    memset(&timeout, 0, sizeof(timeout));  // poll don't block
    return _do_complete_io(&timeout, max_items, d);
  }

  result<int> run(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    using poll_kind = typename detail::io_operation_connection::_poll_kind;
    LLFIO_LOG_FUNCTION_CALL(this);
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    int count = 0;
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      count += this->_execute_posted_items(max_items, d);
      if(max_items == count)
      {
        return count;
      }
      LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d);

      // Figure out how long we can sleep the thread for
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);  // recalculate our timeout
      bool need_to_wake_all = false;
      detail::io_operation_connection *in[64];
      _lock_guard g(this->_lock);
      OUTCOME_TRY(out, _do_timeout_io(_timeout, timeout, need_to_wake_all, {(detail::io_operation_connection **) in, (size_t) std::min(64, max_items - count)}));
      g.unlock();
      if(!out.empty())
      {
        for(auto *i : out)
        {
          i->_poll(poll_kind::timeout);
          ++count;
          if(max_items == count)
          {
            return count;
          }
          LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d);
        }
        // No need to adjust timeout after executing completions as
        // we zero the timeout below anyway
      }

      // timeout will be the lesser of the next pending i/o to expire,
      // or the deadline passed into us. If we've done any work at all,
      // only poll for i/o completions so we return immediately after.
      if(count > 0)
      {
        timeout->QuadPart = 0;
      }
      deadline nd;
      LLFIO_DEADLINE_TO_PARTIAL_DEADLINE(nd, d);
      OUTCOME_TRY(items, _do_complete_io(timeout, max_items - count, nd));
      count += items;
      if(count > 0)
      {
        return count;
      }
      // Loop if no work done, as either there are new posted items or
      // we have timed out
    }
  }
};

template <bool threadsafe> class win_iocp_impl final : public win_common_impl<threadsafe>
{
  using _base = win_common_impl<threadsafe>;
  using _lock_guard = typename _base::_lock_guard;
  template <class T> using atomic_type = std::conditional_t<threadsafe, std::atomic<T>, detail::fake_atomic<T>>;

  atomic_type<size_t> _concurrent_run_instances{0};  // how many threads inside wait() there are right now

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
    if(_scheduled_begin != nullptr || _completed_begin != nullptr || !_durations.empty() || !_absolutes.empty())
    {
      LLFIO_LOG_FATAL(nullptr, "win_iocp_impl::~win_iocp_impl() called with i/o handles still doing work");
      abort();
    }
    (void) CloseHandle(this->_v.h);
  }

  virtual void _post(function_ptr<void *(void *)> &&f) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    _base::_post(std::move(f));
    // Poke my thread to execute posted items
    NtSetIoCompletion(this->_v.h, 0, nullptr, 0, 0);
  }

  virtual result<int> _register_io_handle(handle *h) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_COMPLETION_INFORMATION fci{};
    memset(&fci, 0, sizeof(fci));
    fci.Port = this->_v.h;
    fci.Key = nullptr;
    NTSTATUS ntstat = NtSetInformationFile(h->native_handle().h, &isb, &fci, sizeof(fci), FileCompletionInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h->native_handle().h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }
    return 1;
    // If this works, we can avoid IOCP entirely for immediately completing i/o
    //    return !SetFileCompletionNotificationModes(h->native_handle().h, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE) ? 1 : 2;
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
      return ntkernel_error(ntstat);
    }
    return success();
  }

  result<int> _do_complete_io(LARGE_INTEGER *timeout, int max_items) noexcept
  {
    using poll_kind = typename detail::io_operation_connection::_poll_kind;
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;

    // Delay i/o completions until destruction
    int count = 0;
    this_thread::delay_invoking_io_completion invoker(count);
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "_do_complete_io _scheduled_begin = " << this->_scheduled_begin << std::endl;
    #endif
    if(this->_scheduled_begin != nullptr)
    {
      FILE_IO_COMPLETION_INFORMATION entries[64];
      ULONG filled = 0;
      if(max_items < 0)
      {
        max_items = INT_MAX;
      }
      if(max_items > (int) (sizeof(entries) / sizeof(entries[0])))
      {
        max_items = (int) (sizeof(entries) / sizeof(entries[0]));
      }
      NTSTATUS ntstat;
      if(this->_scheduled_begin == this->_scheduled_end)
      {
        // NtRemoveIoCompletion is markedly quicker than NtRemoveIoCompletionEx,
        // so if it is just a single scheduled op don't pay the extra cost
        ntstat = NtRemoveIoCompletion(this->_v.h, &entries[0].KeyContext, &entries[0].ApcContext, &entries[0].IoStatusBlock, timeout);
        filled = 1;
      }
      else
      {
        ntstat = NtRemoveIoCompletionEx(this->_v.h, entries, (unsigned) max_items, &filled, timeout, false);
      }
      if(ntstat < 0 && ntstat != STATUS_TIMEOUT)
      {
        return ntkernel_error(ntstat);
      }
      if(filled == 0 || ntstat == STATUS_TIMEOUT)
      {
        return -1;
      }
      for(ULONG n = 0; n < filled; n++)
      {
        // The context is the i/o state
        auto *op = (detail::io_operation_connection *) entries[n].ApcContext;
        if(op == nullptr)
        {
          // post() poke
          continue;
        }
        // See if all i/o requests are now complete, if so complete the op
        invoker.remove(op);
        if(op->_poll(poll_kind::check) == io_state_status::completed)
        {
          _lock_guard g(this->_lock);
          _done_io(op, g);
        }
        ++count;
      }
    }
    return (count == 0 && this->_scheduled_begin != nullptr) ? -1 : count;
  }

  virtual result<int> complete_io(int max_items = -1, deadline /*unused*/ = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    LARGE_INTEGER timeout;
    memset(&timeout, 0, sizeof(timeout));  // poll don't block
    return _do_complete_io(&timeout, max_items);
  }

  result<int> run(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    using poll_kind = typename detail::io_operation_connection::_poll_kind;
    LLFIO_LOG_FUNCTION_CALL(this);
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    int count = 0;
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      count += this->_execute_posted_items(max_items, d);
      if(max_items == count)
      {
        return count;
      }
      LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d);

      // Figure out how long we can sleep the thread for
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);  // recalculate our timeout
      bool need_to_wake_all = false;
      detail::io_operation_connection *in[64];
      _lock_guard g(this->_lock);
      // Indicate to any concurrent run() that we are about to calculate timeouts,
      // this will decrement on exit
      _concurrent_run_instances.fetch_add(1, std::memory_order_acq_rel);
      auto un_concurrent_run_instances = undoer([this] { _concurrent_run_instances.fetch_sub(1, std::memory_order_acq_rel); });
      OUTCOME_TRY(out, _do_timeout_io(_timeout, timeout, need_to_wake_all, {(detail::io_operation_connection **) in, (size_t) std::min(64, max_items - count)}));
      if(need_to_wake_all)
      {
        // Timeouts ought to be processed by all idle threads concurrently, so wake everything
        auto threads_sleeping = _concurrent_run_instances.load(std::memory_order_acquire);
        for(size_t n = 0; n < threads_sleeping; n++)
        {
          PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
        }
      }
      g.unlock();
      if(!out.empty())
      {
        for(auto *i : out)
        {
          i->_poll(poll_kind::timeout);
          ++count;
          if(max_items == count)
          {
            return count;
          }
          LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d);
        }
        // No need to adjust timeout after executing completions as
        // we zero the timeout below anyway
      }

      // timeout will be the lesser of the next pending i/o to expire,
      // or the deadline passed into us. If we've done any work at all,
      // only poll for i/o completions so we return immediately after.
      if(count > 0)
      {
        timeout->QuadPart = 0;
      }
      OUTCOME_TRY(items, _do_complete_io(timeout, max_items - count));
      count += items;
      if(count > 0)
      {
        return count;
      }
      // Loop if no work done, as either there are new posted items or
      // we have timed out
    }
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> io_multiplexer::win_alertable() noexcept
{
  try
  {
    auto ret = std::make_unique<win_alertable_impl>();
    OUTCOME_TRY(ret->init());
    return ret;
  }
  catch(...)
  {
    return error_from_exception();
  }
}

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
