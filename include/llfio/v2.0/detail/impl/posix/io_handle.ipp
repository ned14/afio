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

#include <climits>  // for IOV_MAX
#include <fcntl.h>
#include <poll.h>
#include <sys/uio.h>  // for preadv etc
#include <unistd.h>

#include "quickcpplib/signal_guard.hpp"

LLFIO_V2_NAMESPACE_BEGIN

constexpr inline void _check_iovec_match()
{
  static_assert(sizeof(io_handle::buffer_type) == sizeof(iovec), "buffer_type and struct iovec do not match in size");
  static_assert(offsetof(io_handle::buffer_type, _data) == offsetof(iovec, iov_base), "buffer_type and struct iovec do not have same offset of data member");
  static_assert(offsetof(io_handle::buffer_type, _len) == offsetof(iovec, iov_len), "buffer_type and struct iovec do not have same offset of len member");
}

size_t io_handle::max_buffers() const noexcept
{
  static size_t v;
  if(v == 0u)
  {
#ifdef __APPLE__
    v = 1;
#else
    long r = sysconf(_SC_IOV_MAX);
    if(r == -1)
    {
#ifdef IOV_MAX
      r = IOV_MAX;
#else
      r = 1;
#endif
    }
    v = r;
#endif
  }
  return v;
}

io_handle::io_result<io_handle::buffers_type> io_handle::read(io_handle::io_request<io_handle::buffers_type> reqs, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  if(d && !_v.is_nonblocking())
  {
    return errc::not_supported;
  }
  if(reqs.buffers.size() > IOV_MAX)
  {
    return errc::argument_list_too_long;
  }
  LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d);
#if 0
  struct iovec *iov = (struct iovec *) alloca(reqs.buffers.size() * sizeof(struct iovec));
  for(size_t n = 0; n < reqs.buffers.size(); n++)
  {
    iov[n].iov_base = reqs.buffers[n].data;
    iov[n].iov_len = reqs.buffers[n].len;
  }
#else
  auto *iov = reinterpret_cast<struct iovec *>(reqs.buffers.data());
#endif
#ifndef NDEBUG
  if(_v.requires_aligned_io())
  {
    assert((reqs.offset & 511) == 0);
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      assert((reinterpret_cast<uintptr_t>(iov[n].iov_base) & 511) == 0);
      assert((iov[n].iov_len & 511) == 0);
    }
  }
#endif
  ssize_t bytesread = 0;
  if(is_seekable())
  {
#if LLFIO_MISSING_PIOV
    off_t offset = reqs.offset;
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      bytesread += ::pread(_v.fd, iov[n].iov_base, iov[n].iov_len, offset);
      offset += iov[n].iov_len;
    }
#else
    bytesread = ::preadv(_v.fd, iov, reqs.buffers.size(), reqs.offset);
#endif
    if(bytesread < 0)
    {
      return posix_error();
    }
  }
  else
  {
    do
    {
      bytesread = ::readv(_v.fd, iov, reqs.buffers.size());
      if(bytesread <= 0)
      {
        if(bytesread < 0 && EWOULDBLOCK != errno && EAGAIN != errno)
        {
          return posix_error();
        }
        if(!d || !d.steady || d.nsecs != 0)
        {
          LLFIO_POSIX_DEADLINE_TO_SLEEP_LOOP(d);
          int mstimeout = (timeout == nullptr) ? -1 : (timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000LL);
          pollfd p;
          memset(&p, 0, sizeof(p));
          p.fd = _v.fd;
          p.events = POLLIN | POLLERR;
          if(-1 == ::poll(&p, 1, mstimeout))
          {
            return posix_error();
          }
        }
        LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d);
      }
    } while(bytesread <= 0);
  }
  for(size_t i = 0; i < reqs.buffers.size(); i++)
  {
    auto &buffer = reqs.buffers[i];
    if(buffer.size() <= static_cast<size_t>(bytesread))
    {
      bytesread -= buffer.size();
    }
    else
    {
      buffer = {buffer.data(), (size_type) bytesread};
      reqs.buffers = {reqs.buffers.data(), i + 1};
      break;
    }
  }
  return {reqs.buffers};
}

io_handle::io_result<io_handle::const_buffers_type> io_handle::write(io_handle::io_request<io_handle::const_buffers_type> reqs, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  if(d && !_v.is_nonblocking())
  {
    return errc::not_supported;
  }
  if(reqs.buffers.size() > IOV_MAX)
  {
    return errc::argument_list_too_long;
  }
  LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d);
#if 0
  struct iovec *iov = (struct iovec *) alloca(reqs.buffers.size() * sizeof(struct iovec));
  for(size_t n = 0; n < reqs.buffers.size(); n++)
  {
    iov[n].iov_base = const_cast<char *>(reqs.buffers[n].data);
    iov[n].iov_len = reqs.buffers[n].len;
  }
#else
  auto *iov = reinterpret_cast<struct iovec *>(reqs.buffers.data());
#endif
#ifndef NDEBUG
  if(_v.requires_aligned_io())
  {
    assert((reqs.offset & 511) == 0);
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      assert((reinterpret_cast<uintptr_t>(iov[n].iov_base) & 511) == 0);
      assert((iov[n].iov_len & 511) == 0);
    }
  }
#endif
  ssize_t byteswritten = 0;
  if(is_seekable())
  {
#if LLFIO_MISSING_PIOV
    off_t offset = reqs.offset;
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      byteswritten += ::pwrite(_v.fd, iov[n].iov_base, iov[n].iov_len, offset);
      offset += iov[n].iov_len;
    }
#else
    byteswritten = ::pwritev(_v.fd, iov, reqs.buffers.size(), reqs.offset);
#endif
    if(byteswritten < 0)
    {
      return posix_error();
    }
  }
  else
  {
    do
    {
      // Can't guarantee that user code hasn't enabled SIGPIPE
      byteswritten = QUICKCPPLIB_NAMESPACE::signal_guard::signal_guard(
      QUICKCPPLIB_NAMESPACE::signal_guard::signalc_set::broken_pipe, [&] { return ::writev(_v.fd, iov, reqs.buffers.size()); },
      [&](const QUICKCPPLIB_NAMESPACE::signal_guard::raised_signal_info * /*unused*/) {
        errno = EPIPE;
        return -1;
      });
      if(byteswritten <= 0)
      {
        if(byteswritten < 0 && EWOULDBLOCK != errno && EAGAIN != errno)
        {
          return posix_error();
        }
        if(!d || !d.steady || d.nsecs != 0)
        {
          LLFIO_POSIX_DEADLINE_TO_SLEEP_LOOP(d);
          int mstimeout = (timeout == nullptr) ? -1 : (timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000LL);
          pollfd p;
          memset(&p, 0, sizeof(p));
          p.fd = _v.fd;
          p.events = POLLOUT | POLLERR;
          if(-1 == ::poll(&p, 1, mstimeout))
          {
            return posix_error();
          }
        }
        LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d);
      }
    } while(byteswritten <= 0);
  }
  for(size_t i = 0; i < reqs.buffers.size(); i++)
  {
    auto &buffer = reqs.buffers[i];
    if(buffer.size() <= static_cast<size_t>(byteswritten))
    {
      byteswritten -= buffer.size();
    }
    else
    {
      buffer = {buffer.data(), (size_type) byteswritten};
      reqs.buffers = {reqs.buffers.data(), i + 1};
      break;
    }
  }
  return {reqs.buffers};
}

io_handle::io_result<io_handle::const_buffers_type> io_handle::barrier(io_handle::io_request<io_handle::const_buffers_type> reqs, barrier_kind kind, deadline d) noexcept
{
  (void) kind;
  LLFIO_LOG_FUNCTION_CALL(this);
  if(is_pipe() || is_socket())
  {
    return success();  // nothing was flushed
  }
  if(d)
  {
    return errc::not_supported;
  }
#ifdef __linux__
  if(kind == barrier_kind::nowait_data_only || kind == barrier_kind::wait_data_only)
  {
    // Linux has a lovely dedicated syscall giving us exactly what we need here
    extent_type offset = reqs.offset, bytes = 0;
    // empty buffers means bytes = 0 which means sync entire file
    for(const auto &req : reqs.buffers)
    {
      bytes += req.size();
    }
    unsigned flags = SYNC_FILE_RANGE_WRITE;  // start writing all dirty pages in range now
    if(kind == barrier_kind::wait_data_only)
    {
      flags |= SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WAIT_AFTER;  // block until they're on storage
    }
    if(-1 != ::sync_file_range(_v.fd, offset, bytes, flags))
    {
      return {reqs.buffers};
    }
  }
#endif
#if !defined(__FreeBSD__) && !defined(__APPLE__)  // neither of these have fdatasync()
  if(kind == barrier_kind::nowait_data_only || kind == barrier_kind::wait_data_only)
  {
    if(-1 == ::fdatasync(_v.fd))
    {
      return posix_error();
    }
    return {reqs.buffers};
  }
#endif
#ifdef __APPLE__
  if(kind == barrier_kind::nowait_data_only || kind == barrier_kind::nowait_all)
  {
    // OS X fsync doesn't wait for the device to flush its buffers
    if(-1 == ::fsync(_v.fd))
      return posix_error();
    return {std::move(reqs.buffers)};
  }
  // This is the fsync as on every other OS
  if(-1 == ::fcntl(_v.fd, F_FULLFSYNC))
    return posix_error();
#else
  if(-1 == ::fsync(_v.fd))
  {
    return posix_error();
  }
#endif
  return {reqs.buffers};
}

#if 0
io_handle::co_read_awaitable<false> io_handle::co_read(io_request<buffers_type> reqs, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  if(multiplexer() == nullptr)
  {
    OUTCOME_TRY(set_multiplexer());  // traps if handle is not multiplexable
  }
  if(reqs.buffers.size() > IOV_MAX)
  {
    return errc::argument_list_too_long;
  }
  LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d);
#if 0
  struct iovec *iov = (struct iovec *) alloca(reqs.buffers.size() * sizeof(struct iovec));
  for(size_t n = 0; n < reqs.buffers.size(); n++)
  {
    iov[n].iov_base = const_cast<char *>(reqs.buffers[n].data);
    iov[n].iov_len = reqs.buffers[n].len;
  }
#else
  auto *iov = reinterpret_cast<struct iovec *>(reqs.buffers.data());
#endif
#ifndef NDEBUG
  if(_v.requires_aligned_io())
  {
    assert((reqs.offset & 511) == 0);
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      assert((reinterpret_cast<uintptr_t>(iov[n].iov_base) & 511) == 0);
      assert((iov[n].iov_len & 511) == 0);
    }
  }
#endif
  ssize_t bytesread = 0;
  if(is_seekable())
  {
    return errc::operation_not_supported;
  }
  else
  {
    do
    {
      bytesread = ::readv(_v.fd, iov, reqs.buffers.size());
      if(bytesread <= 0)
      {
        if(bytesread < 0 && EWOULDBLOCK != errno && EAGAIN != errno)
        {
          return posix_error();
        }
        // Don't attempt suspension if this is a poll
        if(!d || !d.steady || d.nsecs != 0)
        {
          // Have my i/o context suspend my caller until either some data for read turns up, or my deadline expires
          using promise_type = io_handle::co_read_awaitable<false>::promise_type;
          using result_type = io_result<buffers_type>;
          using callable_type = result_type(promise_type &);
          promise_type p(this, reqs);
          struct _
          {
            result_type operator()(promise_type& p) const noexcept {
              // Temporarily wrap the native handle
              io_handle wrapper(p.nativeh);
              auto ret = wrapper.read(p.reqs, std::chrono::seconds(0));
              wrapper.release();
              return ret;
            }
          };
          static_assert(std::is_nothrow_move_constructible<_>::value, "boo");
          new(&p.extra.erased_op) function_ptr<callable_type, 2 * sizeof(void *)>(make_function_ptr_nothrow<callable_type>(_()));
          p.extra_in_use = 1;
          return multiplexer()->_submit_read(std::move(p), d);
        }
        LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d);
      }
    } while(bytesread <= 0);
  }
  for(size_t i = 0; i < reqs.buffers.size(); i++)
  {
    auto &buffer = reqs.buffers[i];
    if(buffer.size() <= static_cast<size_t>(bytesread))
    {
      bytesread -= buffer.size();
    }
    else
    {
      buffer = {buffer.data(), (size_type) bytesread};
      reqs.buffers = {reqs.buffers.data(), i + 1};
      break;
    }
  }
  return {reqs.buffers};
}

io_handle::co_write_awaitable<false> io_handle::co_write(io_request<const_buffers_type> reqs, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  if(multiplexer() == nullptr)
  {
    OUTCOME_TRY(set_multiplexer());  // traps if handle is not multiplexable
  }
  if(reqs.buffers.size() > IOV_MAX)
  {
    return errc::argument_list_too_long;
  }
  LLFIO_POSIX_DEADLINE_TO_SLEEP_INIT(d);
#if 0
  struct iovec *iov = (struct iovec *) alloca(reqs.buffers.size() * sizeof(struct iovec));
  for(size_t n = 0; n < reqs.buffers.size(); n++)
  {
    iov[n].iov_base = const_cast<char *>(reqs.buffers[n].data);
    iov[n].iov_len = reqs.buffers[n].len;
  }
#else
  auto *iov = reinterpret_cast<struct iovec *>(reqs.buffers.data());
#endif
#ifndef NDEBUG
  if(_v.requires_aligned_io())
  {
    assert((reqs.offset & 511) == 0);
    for(size_t n = 0; n < reqs.buffers.size(); n++)
    {
      assert((reinterpret_cast<uintptr_t>(iov[n].iov_base) & 511) == 0);
      assert((iov[n].iov_len & 511) == 0);
    }
  }
#endif
  ssize_t byteswritten = 0;
  if(is_seekable())
  {
    return errc::operation_not_supported;
  }
  else
  {
    do
    {
      // Can't guarantee that user code hasn't enabled SIGPIPE
      byteswritten = QUICKCPPLIB_NAMESPACE::signal_guard::signal_guard(
      QUICKCPPLIB_NAMESPACE::signal_guard::signalc_set::broken_pipe, [&] { return ::writev(_v.fd, iov, reqs.buffers.size()); },
      [&](const QUICKCPPLIB_NAMESPACE::signal_guard::raised_signal_info * /*unused*/) {
        errno = EPIPE;
        return -1;
      });
      if(byteswritten <= 0)
      {
        if(byteswritten < 0 && EWOULDBLOCK != errno && EAGAIN != errno)
        {
          return posix_error();
        }
        // Don't attempt suspension if this is a poll
        if(!d || !d.steady || d.nsecs != 0)
        {
          // Have my i/o context suspend my caller until either some space for write turns up, or my deadline expires
          using promise_type = io_handle::co_write_awaitable<false>::promise_type;
          using result_type =io_result<const_buffers_type>;
          using callable_type = result_type(promise_type &);
          promise_type p(this, reqs);
          struct _
          {
            result_type operator()(promise_type &p) const noexcept
            {
              // Temporarily wrap the native handle
              io_handle wrapper(p.nativeh);
              auto ret = wrapper.write(p.reqs, std::chrono::seconds(0));
              wrapper.release();
              return ret;
            }
          };
          new(&p.extra.erased_op) function_ptr<callable_type, 2 * sizeof(void *)>(make_function_ptr_nothrow<callable_type>(_()));
          p.extra_in_use = 1;
          return multiplexer()->_submit_write(std::move(p), d);
        }
        LLFIO_POSIX_DEADLINE_TO_TIMEOUT_LOOP(d);
      }
    } while(byteswritten <= 0);
  }
  for(size_t i = 0; i < reqs.buffers.size(); i++)
  {
    auto &buffer = reqs.buffers[i];
    if(buffer.size() <= static_cast<size_t>(byteswritten))
    {
      byteswritten -= buffer.size();
    }
    else
    {
      buffer = {buffer.data(), (size_type) byteswritten};
      reqs.buffers = {reqs.buffers.data(), i + 1};
      break;
    }
  }
  return {reqs.buffers};
}

io_handle::co_barrier_awaitable<false> io_handle::co_barrier(io_request<const_buffers_type> reqs, barrier_kind kind, deadline d) noexcept
{
  (void) reqs;
  (void) kind;
  (void) d;
  LLFIO_LOG_FUNCTION_CALL(this);
  if(is_pipe() || is_socket())
  {
    return success();  // nothing was flushed
  }
  return errc::operation_not_supported;
}
#endif

LLFIO_V2_NAMESPACE_END
