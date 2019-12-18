/* A handle to something
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
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

#ifndef LLFIO_IO_HANDLE_H
#define LLFIO_IO_HANDLE_H

#include "io_context.hpp"

//! \file io_handle.hpp Provides i/o handle

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

/*! \class io_handle
\brief A handle to something capable of scatter-gather i/o.
*/
class LLFIO_DECL io_handle : public handle
{
public:
  using path_type = handle::path_type;
  using extent_type = handle::extent_type;
  using size_type = handle::size_type;
  using mode = handle::mode;
  using creation = handle::creation;
  using caching = handle::caching;
  using flag = handle::flag;
  using buffer_type = io_context::buffer_type;
  using const_buffer_type = io_context::const_buffer_type;
  using buffers_type = io_context::buffers_type;
  using const_buffers_type = io_context::const_buffers_type;
  template <class T> using io_request = io_context::io_request<T>;
  template <class T> using io_result = io_context::io_result<T>;
  using barrier_kind = io_context::barrier_kind;

public:
  //! Default constructor
  constexpr io_handle() {}  // NOLINT
  ~io_handle() = default;
  //! Construct a handle from a supplied native handle
  constexpr explicit io_handle(native_handle_type h, caching caching = caching::none, flag flags = flag::none, io_context *ctx = nullptr)
      : handle(h, caching, flags, ctx)
  {
  }
  //! Explicit conversion from handle permitted
  explicit constexpr io_handle(handle &&o) noexcept
      : handle(std::move(o))
  {
  }
  //! Move construction permitted
  io_handle(io_handle &&) = default;
  //! No copy construction (use `clone()`)
  io_handle(const io_handle &) = delete;
  //! Move assignment permitted
  io_handle &operator=(io_handle &&) = default;
  //! No copy assignment
  io_handle &operator=(const io_handle &) = delete;

  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept override
  {
    if(this->_ctx != nullptr)
    {
      OUTCOME_TRY(set_multiplexer(nullptr));
    }
    return handle::close();
  }

  /*! \brief The *maximum* number of buffers which a single read or write syscall can process at a
  time for this specific open handle. On POSIX, this is known as `IOV_MAX`.

  Note that the actual number of buffers accepted for a read or a write may be significantly
  lower than this system-defined limit, depending on available resources. The `read()` or `write()`
  call will return the buffers accepted.

  Note also that some OSs will error out if you supply more than this limit to `read()` or `write()`,
  but other OSs do not. Some OSs guarantee that each i/o syscall has effects atomically visible or not
  to other i/o, other OSs do not.

  OS X does not implement scatter-gather file i/o syscalls.
  Thus this function will always return `1` in that situation.

  Microsoft Windows *may* implement scatter-gather file i/o under very limited circumstances.
  Most of the time this function will return `1`.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC size_t max_buffers() const noexcept;

  /*! \brief Sets the i/o context this handle will use to multiplex i/o.

  This function will always fail if `.is_multiplexable()` is false for this handle.

  Note that this call deregisters this handle from any existing i/o context, and registers
  it with the new i/o context. You must therefore not call it if any i/o is currently
  outstanding on this handle. You should also be aware that multiple dynamic memory
  allocations and deallocations may occur, as well as multiple syscalls (i.e. this is
  an expensive call, try to do it from cold code).

  \mallocs Multiple dynamic memory allocations and deallocations.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> set_multiplexer(io_context *c = this_thread::multiplexer()) noexcept
  {
    if(!is_multiplexable())
    {
      return errc::operation_not_supported;
    }
    if(c == this->_ctx)
    {
      return success();
    }
    if(this->_ctx != nullptr)
    {
      OUTCOME_TRY(this->_ctx->_deregister_io_handle(this));
      this->_ctx = nullptr;
    }
    if(c != nullptr)
    {
      OUTCOME_TRY(c->_register_io_handle(this));
    }
    this->_ctx = c;
    return success();
  }

  /*! \brief Read data from the open handle.

  \warning Depending on the implementation backend, **very** different buffers may be returned than you
  supplied. You should **always** use the buffers returned and assume that they point to different
  memory and that each buffer's size will have changed.

  \return The buffers read, which may not be the buffers input. The size of each scatter-gather
  buffer returned is updated with the number of bytes of that buffer transferred, and the pointer
  to the data may be \em completely different to what was submitted (e.g. it may point into a
  memory map).
  \param reqs A scatter-gather and offset request.
  \param d An optional deadline by which the i/o must complete, else it is cancelled.
  Note function may return significantly after this deadline if the i/o takes long to cancel.
  \errors Any of the values POSIX read() can return, `errc::timed_out`, `errc::operation_canceled`. `errc::not_supported` may be
  returned if deadline i/o is not possible with this particular handle configuration (e.g.
  reading from regular files on POSIX or reading from a non-overlapped HANDLE on Windows).
  \mallocs The default synchronous implementation in file_handle performs no memory allocation.
  The asynchronous implementation in async_file_handle performs one calloc and one free.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<buffers_type> read(io_request<buffers_type> reqs, deadline d = deadline()) noexcept;
  //! Convenience initialiser list based overload for `read()`
  LLFIO_MAKE_FREE_FUNCTION
  io_result<size_type> read(extent_type offset, std::initializer_list<buffer_type> lst, deadline d = deadline()) noexcept
  {
    buffer_type *_reqs = reinterpret_cast<buffer_type *>(alloca(sizeof(buffer_type) * lst.size()));
    memcpy(_reqs, lst.begin(), sizeof(buffer_type) * lst.size());
    io_request<buffers_type> reqs(buffers_type(_reqs, lst.size()), offset);
    auto ret = read(reqs, d);
    if(ret)
    {
      return ret.bytes_transferred();
    }
    return std::move(ret).error();
  }

  LLFIO_DEADLINE_TRY_FOR_UNTIL(read)

  /*! \brief Write data to the open handle.

  \warning Depending on the implementation backend, not all of the buffers input may be written.
  For example, with a zeroed deadline, some backends may only consume as many buffers as the system has available write slots
  for, thus for those backends this call is "non-blocking" in the sense that it will return immediately even if it
  could not schedule a single buffer write. Another example is that some implementations will not
  auto-extend the length of a file when a write exceeds the maximum extent, you will need to issue
  a `truncate(newsize)` first.

  \return The buffers written, which may not be the buffers input. The size of each scatter-gather
  buffer returned is updated with the number of bytes of that buffer transferred.
  \param reqs A scatter-gather and offset request.
  \param d An optional deadline by which the i/o must complete, else it is cancelled.
  Note function may return significantly after this deadline if the i/o takes long to cancel.
  \errors Any of the values POSIX write() can return, `errc::timed_out`, `errc::operation_canceled`. `errc::not_supported` may be
  returned if deadline i/o is not possible with this particular handle configuration (e.g.
  writing to regular files on POSIX or writing to a non-overlapped HANDLE on Windows).
  \mallocs The default synchronous implementation in file_handle performs no memory allocation.
  The asynchronous implementation in async_file_handle performs one calloc and one free.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> write(io_request<const_buffers_type> reqs, deadline d = deadline()) noexcept;
  //! Convenience initialiser list based overload for `write()`
  LLFIO_MAKE_FREE_FUNCTION
  io_result<size_type> write(extent_type offset, std::initializer_list<const_buffer_type> lst, deadline d = deadline()) noexcept
  {
    const_buffer_type *_reqs = reinterpret_cast<const_buffer_type *>(alloca(sizeof(const_buffer_type) * lst.size()));
    memcpy(_reqs, lst.begin(), sizeof(const_buffer_type) * lst.size());
    io_request<const_buffers_type> reqs(const_buffers_type(_reqs, lst.size()), offset);
    auto ret = write(reqs, d);
    if(ret)
    {
      return ret.bytes_transferred();
    }
    return std::move(ret).error();
  }

  LLFIO_DEADLINE_TRY_FOR_UNTIL(write)

  /*! \brief Issue a write reordering barrier such that writes preceding the barrier will reach
  storage before writes after this barrier.

  \warning **Assume that this call is a no-op**. It is not reliably implemented in many common
  use cases, for example if your code is running inside a LXC container, or if the user has mounted
  the filing system with non-default options. Instead open the handle with `caching::reads` which
  means that all writes form a strict sequential order not completing until acknowledged by the
  storage device. Filing system can and do use different algorithms to give much better performance
  with `caching::reads`, some (e.g. ZFS) spectacularly better.

  \warning Let me repeat again: consider this call to be a **hint** to poke the kernel with a stick
  to go start to do some work sooner rather than later. **It may be ignored entirely**.

  \warning For portability, you can only assume that barriers write order for a single handle
  instance. You cannot assume that barriers write order across multiple handles to the same inode,
  or across processes.

  \return The buffers barriered, which may not be the buffers input. The size of each scatter-gather
  buffer is updated with the number of bytes of that buffer barriered.
  \param reqs A scatter-gather and offset request for what range to barrier. May be ignored on
  some platforms which always write barrier the entire file. Supplying a default initialised reqs
  write barriers the entire file.
  \param kind Which kind of write reordering barrier to perform.
  \param d An optional deadline by which the i/o must complete, else it is cancelled.
  Note function may return significantly after this deadline if the i/o takes long to cancel.
  \errors Any of the values POSIX fdatasync() or Windows NtFlushBuffersFileEx() can return.
  \mallocs None.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> barrier(io_request<const_buffers_type> reqs = io_request<const_buffers_type>(), barrier_kind kind = barrier_kind::nowait_data_only, deadline d = deadline()) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(barrier)

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _begin_read(detail::io_operation_connection *state, io_request<buffers_type> reqs) noexcept;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _begin_write(detail::io_operation_connection *state, io_request<const_buffers_type> reqs) noexcept;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _begin_barrier(detail::io_operation_connection *state, io_request<buffers_type> reqs, barrier_kind kind) noexcept;
#if 0
  //! \brief The type for a read i/o awaitable
  template <bool use_atomic> using co_read_awaitable = typename io_context::template _co_read_awaitable<use_atomic>;
  //! \brief The type for a write i/o awaitable
  template <bool use_atomic> using co_write_awaitable =typename io_context::template _co_write_awaitable<use_atomic>;
  //! \brief The type for a barrier i/o awaitable
  template <bool use_atomic> using co_barrier_awaitable = typename io_context::template _co_barrier_awaitable<use_atomic>;

  /*! \brief Read data from the open handle immediately if it would not block, if so
  the returned awaitable will be immediately ready. Otherwise begin the i/o, and the
  awaitable shall become ready when the i/o has completed.

  This function will always fail if `.is_multiplexable()` is false for this handle.

  The parameters are as for `.read()`. `.set_multiplexer()` can be used to force the i/o
  context used to schedule any blocking i/o. If this handle's multiplexer is null,
  `.set_multiplexer()` is called on your behalf to register this handle with
  the current thread's i/o context, which is a non-deterministic operation. To avoid
  that, call `.set_multiplexer()` manually from cold code.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC co_read_awaitable<false> co_read(io_request<buffers_type> reqs, deadline d = deadline()) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(co_read)

  /*! \brief Write data to the open handle immediately if it would not block, if so
  the returned awaitable will be immediately ready. Otherwise begin the i/o, and the
  awaitable shall become ready when the i/o has completed.

  This function will always fail if `.is_multiplexable()` is false for this handle.

  The parameters are as for `.write()`. `.set_multiplexer()` can be used to force the i/o
  context used to schedule any blocking i/o. If this handle's multiplexer is null,
  `.set_multiplexer()` is called on your behalf to register this handle with
  the current thread's i/o context, which is a non-deterministic operation. To avoid
  that, call `.set_multiplexer()` manually from cold code.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC co_write_awaitable<false> co_write(io_request<const_buffers_type> reqs, deadline d = deadline()) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(co_write)

  /*! \brief Begin the issuing of a write reordering barrier such that writes
  preceding the barrier will reach storage before writes after this barrier completes.
  This operation almost never completes immediately.

  This function will always fail if `.is_multiplexable()` is false for this handle.

  The parameters are as for `.barrier()`. `.set_multiplexer()` can be used to force the i/o
  context used to schedule any blocking i/o. If this handle's multiplexer is null,
  `.set_multiplexer()` is called on your behalf to register this handle with
  the current thread's i/o context, which is a non-deterministic operation. To avoid
  that, call `.set_multiplexer()` manually from cold code.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC co_barrier_awaitable<false> co_barrier(io_request<const_buffers_type> reqs = io_request<const_buffers_type>(), barrier_kind kind = barrier_kind::nowait_data_only, deadline d = deadline()) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(co_barrier)
#endif
};


// BEGIN make_free_functions.py
/*! \brief Read data from the open handle.

\warning Depending on the implementation backend, **very** different buffers may be returned than you
supplied. You should **always** use the buffers returned and assume that they point to different
memory and that each buffer's size will have changed.

\return The buffers read, which may not be the buffers input. The size of each scatter-gather
buffer is updated with the number of bytes of that buffer transferred, and the pointer to
the data may be \em completely different to what was submitted (e.g. it may point into a
memory map).
\param self The object whose member function to call.
\param reqs A scatter-gather and offset request.
\param d An optional deadline by which the i/o must complete, else it is cancelled.
Note function may return significantly after this deadline if the i/o takes long to cancel.
\errors Any of the values POSIX read() can return, `errc::timed_out`, `errc::operation_canceled`. `errc::not_supported` may be
returned if deadline i/o is not possible with this particular handle configuration (e.g.
reading from regular files on POSIX or reading from a non-overlapped HANDLE on Windows).
\mallocs The default synchronous implementation in file_handle performs no memory allocation.
The asynchronous implementation in async_file_handle performs one calloc and one free.
*/
inline io_handle::io_result<io_handle::buffers_type> read(io_handle &self, io_handle::io_request<io_handle::buffers_type> reqs, deadline d = deadline()) noexcept
{
  return self.read(std::forward<decltype(reqs)>(reqs), std::forward<decltype(d)>(d));
}
/*! \brief Write data to the open handle.

\warning Depending on the implementation backend, not all of the buffers input may be written and
the some buffers at the end of the returned buffers may return with zero bytes written.
For example, with a zeroed deadline, some backends may only consume as many buffers as the system has available write slots
for, thus for those backends this call is "non-blocking" in the sense that it will return immediately even if it
could not schedule a single buffer write. Another example is that some implementations will not
auto-extend the length of a file when a write exceeds the maximum extent, you will need to issue
a `truncate(newsize)` first.

\return The buffers written, which may not be the buffers input. The size of each scatter-gather
buffer is updated with the number of bytes of that buffer transferred.
\param self The object whose member function to call.
\param reqs A scatter-gather and offset request.
\param d An optional deadline by which the i/o must complete, else it is cancelled.
Note function may return significantly after this deadline if the i/o takes long to cancel.
\errors Any of the values POSIX write() can return, `errc::timed_out`, `errc::operation_canceled`. `errc::not_supported` may be
returned if deadline i/o is not possible with this particular handle configuration (e.g.
writing to regular files on POSIX or writing to a non-overlapped HANDLE on Windows).
\mallocs The default synchronous implementation in file_handle performs no memory allocation.
The asynchronous implementation in async_file_handle performs one calloc and one free.
*/
inline io_handle::io_result<io_handle::const_buffers_type> write(io_handle &self, io_handle::io_request<io_handle::const_buffers_type> reqs, deadline d = deadline()) noexcept
{
  return self.write(std::forward<decltype(reqs)>(reqs), std::forward<decltype(d)>(d));
}
//! \overload
inline io_handle::io_result<io_handle::size_type> write(io_handle &self, io_handle::extent_type offset, std::initializer_list<io_handle::const_buffer_type> lst, deadline d = deadline()) noexcept
{
  return self.write(std::forward<decltype(offset)>(offset), std::forward<decltype(lst)>(lst), std::forward<decltype(d)>(d));
}
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#ifdef _WIN32
#include "detail/impl/windows/io_handle.ipp"
#else
#include "detail/impl/posix/io_handle.ipp"
#endif
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
