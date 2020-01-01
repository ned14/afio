/* A handle to something
(C) 2015-2019 Niall Douglas <http://www.nedproductions.biz/> (11 commits)
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

#include "../../../io_handle.hpp"
#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

size_t io_handle::max_buffers() const noexcept
{
  return 1;  // TODO FIXME support ReadFileScatter/WriteFileGather
}

template <class BuffersType> inline bool do_cancel(const native_handle_type &nativeh, span<typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK> ols, io_handle::io_request<BuffersType> reqs) noexcept
{
  using namespace windows_nt_kernel;
  using EIOSB = typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK;
  bool did_cancel = false;
  ols = span<EIOSB>(ols.data(), reqs.buffers.size());
  for(auto &ol : ols)
  {
    if(ol.Status == -1)
    {
      // No need to cancel an i/o never begun
      continue;
    }
    NTSTATUS ntstat = ntcancel_pending_io(nativeh.h, (IO_STATUS_BLOCK &) ol);
    if(ntstat < 0 && ntstat != (NTSTATUS) 0xC0000120 /*STATUS_CANCELLED*/)
    {
      LLFIO_LOG_FATAL(nullptr, "Failed to cancel earlier i/o");
      abort();
    }
    if(ntstat == (NTSTATUS) 0xC0000120 /*STATUS_CANCELLED*/)
    {
      did_cancel = true;
    }
  }
  return did_cancel;
}

template <bool blocking, class Syscall, class BuffersType>
inline io_handle::io_result<BuffersType> do_read_write(size_t &scheduled, Syscall &&syscall, const native_handle_type &nativeh, windows_nt_kernel::PIO_APC_ROUTINE routine, detail::io_operation_connection *op, span<typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK> ols,
                                                       io_handle::io_request<BuffersType> reqs, deadline d) noexcept
{
  using namespace windows_nt_kernel;
  using EIOSB = typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK;
  if(d && !nativeh.is_nonblocking())
  {
    return errc::not_supported;
  }
  if(reqs.buffers.size() > 64)
  {
    return errc::argument_list_too_long;
  }
  LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
  ols = span<EIOSB>(ols.data(), reqs.buffers.size());
  memset(ols.data(), 0, reqs.buffers.size() * sizeof(EIOSB));
  auto ol_it = ols.begin();
  for(auto &req : reqs.buffers)
  {
    EIOSB &ol = *ol_it++;
    ol.Status = -1;
  }
  auto cancel_io = undoer([&] {
    if(nativeh.is_nonblocking())
    {
      if(ol_it != ols.begin() + 1)
      {
        do_cancel(nativeh, ols, reqs);
      }
    }
  });
  ol_it = ols.begin();
  for(auto &req : reqs.buffers)
  {
    EIOSB &ol = *ol_it++;
    LARGE_INTEGER offset;
    if(nativeh.is_append_only())
    {
      offset.QuadPart = -1;
    }
    else
    {
#ifndef NDEBUG
      if(nativeh.requires_aligned_io())
      {
        assert((reqs.offset & 511) == 0);
      }
#endif
      offset.QuadPart = reqs.offset;
    }
#ifndef NDEBUG
    if(nativeh.requires_aligned_io())
    {
      assert(((uintptr_t) req.data() & 511) == 0);
      assert((req.size() & 511) == 0);
    }
#endif
    reqs.offset += req.size();
    ol.Status = 0x103 /*STATUS_PENDING*/;
    NTSTATUS ntstat = syscall(nativeh.h, nullptr, routine, op, (PIO_STATUS_BLOCK) &ol, (PVOID) req.data(), static_cast<DWORD>(req.size()), &offset, nullptr);
    if(ntstat < 0 && ntstat != 0x103 /*STATUS_PENDING*/)
    {
      InterlockedCompareExchange(&ol.Status, ntstat, 0x103 /*STATUS_PENDING*/);
      return ntkernel_error(ntstat);
    }
    ++scheduled;
  }
  // If handle is overlapped, wait for completion of each i/o.
  if(nativeh.is_nonblocking() && blocking)
  {
    for(auto &ol : ols)
    {
      deadline nd;
      LLFIO_DEADLINE_TO_PARTIAL_DEADLINE(nd, d);
      if(STATUS_TIMEOUT == ntwait(nativeh.h, (IO_STATUS_BLOCK &) ol, nd))
      {
        // ntwait cancels the i/o, undoer will cancel all the other i/o
        LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d);
      }
    }
  }
  cancel_io.dismiss();
  if(!blocking)
  {
    // If all the operations already completed, great
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      if(ols[n].Status == static_cast<ULONG_PTR>(0x103 /*STATUS_PENDING*/))
      {
        return success();  // empty buffers means at least one buffer is not completed yet
      }
    }
  }
  auto ret(reqs.buffers);
  for(size_t n = 0; n < reqs.buffers.size(); n++)
  {
    assert(ols[n].Status != -1);
    if(ols[n].Status != 0)
    {
      return ntkernel_error(static_cast<NTSTATUS>(ols[n].Status));
    }
    reqs.buffers[n] = {reqs.buffers[n].data(), ols[n].Information};
    if(reqs.buffers[n].size() != 0)
    {
      ret = {ret.data(), n + 1};
    }
  }
  return io_handle::io_result<BuffersType>(std::move(ret));
}

io_handle::io_result<io_handle::buffers_type> io_handle::read(io_handle::io_request<io_handle::buffers_type> reqs, deadline d) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  using EIOSB = typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK;
  std::array<EIOSB, 64> _ols{};
  size_t scheduled = 0;
  return do_read_write<true>(scheduled, NtReadFile, _v, nullptr, nullptr, {_ols.data(), _ols.size()}, reqs, d);
}

io_handle::io_result<io_handle::const_buffers_type> io_handle::write(io_handle::io_request<io_handle::const_buffers_type> reqs, deadline d) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  using EIOSB = typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK;
  std::array<EIOSB, 64> _ols{};
  size_t scheduled = 0;
  return do_read_write<true>(scheduled, NtWriteFile, _v, nullptr, nullptr, {_ols.data(), _ols.size()}, reqs, d);
}

io_handle::io_result<io_handle::const_buffers_type> io_handle::barrier(io_handle::io_request<io_handle::const_buffers_type> reqs, barrier_kind kind, deadline d) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  if(d && !_v.is_nonblocking())
  {
    return errc::not_supported;
  }
  LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
  using EIOSB = typename detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK;
  EIOSB ol{};
  memset(&ol, 0, sizeof(ol));
  auto *isb = reinterpret_cast<IO_STATUS_BLOCK *>(&ol);
  *isb = make_iostatus();
  ULONG flags = 0;
  if(kind == barrier_kind::nowait_data_only)
  {
    flags = 1 /*FLUSH_FLAGS_FILE_DATA_ONLY*/;  // note this doesn't block
  }
  else if(kind == barrier_kind::nowait_all)
  {
    flags = 2 /*FLUSH_FLAGS_NO_SYNC*/;
  }
  NTSTATUS ntstat = NtFlushBuffersFileEx(_v.h, flags, nullptr, 0, isb);
  if(STATUS_PENDING == ntstat)
  {
    ntstat = ntwait(_v.h, (IO_STATUS_BLOCK &) ol, d);
    if(STATUS_TIMEOUT == ntstat)
    {
      return errc::timed_out;
    }
  }
  if(ntstat < 0)
  {
    return ntkernel_error(ntstat);
  }
  return {reqs.buffers};
}

struct apc_completion_routine
{
  static void __stdcall thunk(void *ApcContext, detail::io_operation_connection::_EXTENDED_IO_STATUS_BLOCK *IoStatusBlock, unsigned long /*unused*/)
  {
    // The context is the i/o state
    auto *op = (detail::io_operation_connection *) ApcContext;
    // Add this to the completed list
    op->h->multiplexer()->_os_has_completed_io(op);
  }
};

void io_handle::_apc_begin(_async_op what, detail::io_operation_connection *op, void *_reqs) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  auto *reqs = (io_request<buffers_type> *) _reqs;
  if(reqs->buffers.empty())
  {
    // The i/o completed immediately with success
    op->_complete_io(result<size_t>(0));
    return;
  }
  size_t scheduled = 0;
  switch(what)
  {
  case _async_op::unknown:
    abort();
  case _async_op::read:
  {
    auto r = do_read_write<false>(scheduled, NtReadFile, _v, (PIO_APC_ROUTINE) apc_completion_routine::thunk, op, {&op->ols[0], op->max_overlappeds}, *reqs, deadline());
    if(!r || !r.value().empty())
    {
      // If the i/o completed immediately, complete it either now or at base of stack
      this_thread::delay_invoking_io_completion::add(op);
      return;
    }
    break;
  }
  case _async_op::write:
  {
    auto r = do_read_write<false>(scheduled, NtWriteFile, _v, (PIO_APC_ROUTINE) apc_completion_routine::thunk, op, {&op->ols[0], op->max_overlappeds}, *reqs, deadline());
    if(!r || !r.value().empty())
    {
      // If the i/o completed immediately, complete it either now or at base of stack
      this_thread::delay_invoking_io_completion::add(op);
      return;
    }
    break;
  }
  case _async_op::barrier:
  {
    abort();  // FIXME later
#if 0
    memset(&op->ols[0], 0, sizeof(OVERLAPPED));
    auto *isb = reinterpret_cast<IO_STATUS_BLOCK *>(&op->ols[0]);
    *isb = make_iostatus();
    ULONG flags = 0;
    if(kind == barrier_kind::nowait_data_only)
    {
      flags = 1 /*FLUSH_FLAGS_FILE_DATA_ONLY*/;  // note this doesn't block
    }
    else if(kind == barrier_kind::nowait_all)
    {
      flags = 2 /*FLUSH_FLAGS_NO_SYNC*/;
    }
    NTSTATUS ntstat = NtFlushBuffersFileEx(_v.h, flags, nullptr, 0, isb);
#endif
    break;
  }
  }
  // If we never scheduled anything, we can complete immediately.
  if(0 == scheduled)
  {
    // The implementation will scan the status blocks and report the failure
    op->_complete_io(result<size_t>(0));
    return;
  }
  // Add this scheduled i/o to the multiplexer
  multiplexer()->_scheduled_io(op);
}
void io_handle::_apc_cancel(detail::io_operation_connection *op, void *_reqs) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  auto *reqs = (io_request<buffers_type> *) _reqs;
  do_cancel(_v, {&op->ols[0], op->max_overlappeds}, *reqs);
}
void io_handle::_iocp_begin(_async_op what, detail::io_operation_connection *op, void *_reqs) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  auto *reqs = (io_request<buffers_type> *) _reqs;
  if(reqs->buffers.empty())
  {
    // The i/o completed immediately with success
    op->_complete_io(result<size_t>(0));
    return;
  }
  size_t scheduled = 0;
  switch(what)
  {
  case _async_op::unknown:
    abort();
  case _async_op::read:
  {
    auto r = do_read_write<false>(scheduled, NtReadFile, _v, nullptr /*for IOCP must be null*/, op, {&op->ols[0], op->max_overlappeds}, *reqs, deadline());
    if(!r || !r.value().empty())
    {
      // If the i/o completed immediately, complete it either now or at base of stack
      this_thread::delay_invoking_io_completion::add(op);
      return;
    }
    break;
  }
  case _async_op::write:
  {
    auto r = do_read_write<false>(scheduled, NtWriteFile, _v, nullptr /*for IOCP must be null*/, op, {&op->ols[0], op->max_overlappeds}, *reqs, deadline());
    if(!r || !r.value().empty())
    {
      // If the i/o completed immediately, complete it either now or at base of stack
      this_thread::delay_invoking_io_completion::add(op);
      return;
    }
    break;
  }
  case _async_op::barrier:
  {
    abort();  // FIXME later
#if 0
    memset(&op->ols[0], 0, sizeof(OVERLAPPED));
    auto *isb = reinterpret_cast<IO_STATUS_BLOCK *>(&op->ols[0]);
    *isb = make_iostatus();
    ULONG flags = 0;
    if(kind == barrier_kind::nowait_data_only)
    {
      flags = 1 /*FLUSH_FLAGS_FILE_DATA_ONLY*/;  // note this doesn't block
    }
    else if(kind == barrier_kind::nowait_all)
    {
      flags = 2 /*FLUSH_FLAGS_NO_SYNC*/;
    }
    NTSTATUS ntstat = NtFlushBuffersFileEx(_v.h, flags, nullptr, 0, isb);
#endif
    break;
  }
  }
  // If we never scheduled anything, we can complete immediately.
  if(0 == scheduled)
  {
    // The implementation will scan the status blocks and report the failure
    op->_complete_io(result<size_t>(0));
    return;
  }
  // Add this scheduled i/o to the multiplexer
  multiplexer()->_scheduled_io(op);
}
void io_handle::_iocp_cancel(detail::io_operation_connection *op, void * _reqs) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  auto *reqs = (io_request<buffers_type> *) _reqs;
  do_cancel(_v, {&op->ols[0], op->max_overlappeds}, *reqs);
}


#if 0
const detail::io_operation_visitor &io_handle::_get_async_io_visitor() noexcept
{
  static struct _ final : public detail::io_operation_visitor
  {
    void begin_read(detail::io_operation_connection *state, io_request<buffers_type> reqs) const noexcept override
    {
      windows_nt_kernel::init();
      using namespace windows_nt_kernel;
      LLFIO_LOG_FUNCTION_CALL(this);
      if(reqs.buffers.empty())
      {
        // The i/o completed immediately with success
        state->_complete_io(result<size_t>(0));
        return;
      }
      auto r = do_read_write<false>({&state->ols[0], state->max_overlappeds}, state->nativeh, NtReadFile, std::move(reqs), deadline(), state);
#if 0
      const bool inside_complete_io = (state->this_thread_inside_complete_io() != 0);
      // If we are being called from within a _complete_io because somebody
      // is beginning a new i/o, don't permit _complete_io recursion
      if(!inside_complete_io && !r)
      {
        // The i/o completed immediately with failure
        state->_complete_io(result<size_t>(r.error()));
        return;
      }
      if(!inside_complete_io && !r.value().empty())
      {
        // The i/o completed immediately with success
        state->_complete_io(result<size_t>(0));
        return;
      }
#endif
      state->ctx->_register_pending_io(state);
    }

    void begin_write(detail::io_operation_connection *state, io_request<const_buffers_type> reqs) const noexcept override
    {
      windows_nt_kernel::init();
      using namespace windows_nt_kernel;
      LLFIO_LOG_FUNCTION_CALL(this);
      if(reqs.buffers.empty())
      {
        // The i/o completed immediately with success
        state->_complete_io(result<size_t>(0));
        return;
      }
      auto r = do_read_write<false>({&state->ols[0], state->max_overlappeds}, state->nativeh, NtWriteFile, std::move(reqs), deadline(), state);
#if 0
      const bool inside_complete_io = (state->this_thread_inside_complete_io() != 0);
      // If we are being called from within a _complete_io because somebody
      // is beginning a new i/o, don't permit _complete_io recursion
      if(!inside_complete_io && !r)
      {
        // The i/o completed immediately with failure
        state->_complete_io(result<size_t>(r.error()));
        return;
      }
      if(!inside_complete_io && !r.value().empty())
      {
        // The i/o completed immediately with success
        state->_complete_io(result<size_t>(0));
        return;
      }
#endif
      state->ctx->_register_pending_io(state);
    }

    void begin_barrier(detail::io_operation_connection *state, io_request<const_buffers_type> /*unused*/, barrier_kind kind) const noexcept override
    {
      windows_nt_kernel::init();
      using namespace windows_nt_kernel;
      LLFIO_LOG_FUNCTION_CALL(this);
      memset(&state->ols[0], 0, sizeof(OVERLAPPED));
      auto *isb = reinterpret_cast<IO_STATUS_BLOCK *>(&state->ols[0]);
      *isb = make_iostatus();
      state->ols[0].state = state;
      ULONG flags = 0;
      if(kind == barrier_kind::nowait_data_only)
      {
        flags = 1 /*FLUSH_FLAGS_FILE_DATA_ONLY*/;  // note this doesn't block
      }
      else if(kind == barrier_kind::nowait_all)
      {
        flags = 2 /*FLUSH_FLAGS_NO_SYNC*/;
      }
      NTSTATUS ntstat = NtFlushBuffersFileEx(state->nativeh.h, flags, nullptr, 0, isb);
      if(STATUS_PENDING == ntstat)
      {
        state->ctx->_register_pending_io(state);
        return;
      }
      state->ctx->_register_pending_io(state);
#if 0
      if(ntstat < 0)
      {
        // The i/o completed immediately with failure
        state->_complete_io(result<size_t>(ntkernel_error(ntstat)));
        return;
      }
      // The i/o completed immediately with success. Pass through the buffers unmodified
      state->_complete_io(result<size_t>((size_t) -1));
#endif
    }

    void cancel_read(detail::io_operation_connection *state, io_request<buffers_type> reqs) const noexcept override
    {
      LLFIO_LOG_FUNCTION_CALL(this);
      if(do_cancel({&state->ols[0], state->max_overlappeds}, state->nativeh, std::move(reqs)))
      {
        state->is_cancelled_io = true;
      }
    }

    void cancel_write(detail::io_operation_connection *state, io_request<const_buffers_type> reqs) const noexcept override
    {
      LLFIO_LOG_FUNCTION_CALL(this);
      if(do_cancel({&state->ols[0], state->max_overlappeds}, state->nativeh, std::move(reqs)))
      {
        state->is_cancelled_io = true;
      }
    }

    void cancel_barrier(detail::io_operation_connection *state, io_request<const_buffers_type> /*unused*/, barrier_kind /*unused*/) const noexcept override
    {
      LLFIO_LOG_FUNCTION_CALL(this);
      if(do_cancel({&state->ols[0], 1}, state->nativeh, io_request<const_buffers_type>{{nullptr, 0}, 0}))
      {
        state->is_cancelled_io = true;
      }
    }
  } visitor;
  return visitor;
}
#endif

LLFIO_V2_NAMESPACE_END
