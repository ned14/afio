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

#ifndef LLFIO_IO_MULTIPLEXER_H
#define LLFIO_IO_MULTIPLEXER_H

//#define LLFIO_DEBUG_PRINT

#include "handle.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

class io_handle;

/*! \class io_multiplexer
\brief An i/o multiplexer context.

This i/o multiplexer is used in conjunction with `io_handle` to multiplex
i/o on a single kernel thread. An `io_handle` may use its own i/o
multiplexer set using its `.set_multiplexer()`. `.set_multiplexer()`'s
defaulted parameter is to use the current thread's i/o multiplexer which
is set using `this_thread::set_multiplexer()`.
If never set, `this_thread::multiplexer()` will upon first call create
an i/o multiplexer suitable for the current platform using `io_multiplexer::best_available(1)`
and retain it in thread local storage, so in general you can simply
start multiplexing i/o immediately without having to do any setup, and
everything should "just work". Just be aware that the very first use
will be unusually expensive.

For all i/o multiplexer implementations, `.post()` is guaranteed to be
threadsafe. You can use this to post work from other kernel threads
to be executed by the i/o multiplexer by any current or next call to
`.run()` as soon as possible. Be aware that if nobody calls `.run()`
or `.check_posted_items()`, posted work is never executed.

For all i/o multiplexer implementations, no dynamic memory allocations
occur during i/o unless you call `.run()` or `.check_deadlined_io()`. If `threads` is less than or
equal to `1`, no locking occurs during i/o either. This makes multiplexed
i/o very low overhead. It is effectively a spinlooped polling implementation
by default.

Be aware that if you ever call `.check_deadlined_io()` or `.run()`, i/o
with a non-zero non-infinite deadline is slightly more
expensive than zero or infinite deadlined i/o, as all pending i/o must be
ordered into a coherent list in order to calculate the earliest expiring i/o.
This list is cached between runs, which means that completion of deadlined
i/o will cause a dynamic memory free per completed deadlined i/o. No dynamic
memory allocation occurs for deadlined i/o if `.check_deadlined_io()` or `.run()`
is never called, however additional syscalls to check the current time will
occur.

For some use cases where all kernel thread sleeps are unacceptable, you want
an exclusively non-blocking i/o implementation.
This can be implemented using `.poll()` on the connection states, and
manually invoking `.check_posted_items()`. There is also another intermediate
configuration, where one kernel thread periodically calls `.check_posted_items()`
and `.check_deadlined_io()` to dispatch posted work and cancel and complete
i/o which has timed out, and other threads manually `.poll()` on the
connection states for completion on an as-needed basis.

Finally, be aware that `.poll_for()` and `.poll_until()` on the connection
state, if given a timeout later than now, will call `.run()` on the
associated multiplexer, looping it until the connection state completes
either via i/o completion or time out.


## Available implementations

There are multiple i/o multiplexer implementations available, each with
varying tradeoffs. Some of the implementations take a `threads`
parameter. If `> 1`, the implementation returned can handle more
than one thread using the same instance at a time. In this situation,
receivers are invoked in the next available idle thread
blocked within `.run()`.

`.run()` returns when at least one i/o completion or post was processed.
This enables you to sleep the kernel thread until some i/o completes
somewhere, and then to ask `.completed()` on your connected i/o state to
see if it has completed. The `.poll_for()`, `.poll_until()` and `.poll_until_ready()`
calls are convenience wrappers of `.run()` which do this for you.
For improved efficiency, some of the `.run()` implementations will process
many completions at a time, and will return the number processed.

### Linux

- `io_multiplexer::linux_epoll(size_t threads)` returns a Linux `epoll()`
based i/o context implementation. If `threads` is 1, the implementation
returned cannot be used by multiple threads (apart from `.post()`).
Note that Linux kernel 4.5 or later is required for `threads > 1` to
successfully instantiate.

TODO FIXME on detail

- `io_multiplexer::linux_io_uring()` returns a Linux io_uring based i/o
context implementation. As Linux io_uring is fundamentally a single
threaded kernel interface, multiple threads are not supported. Only
available on Linux kernel 5.1 or later.
`io_multiplexer::best_available()` chooses this if your kernel is new
enough to support io_uring and `threads` is 1, otherwise the `epoll()`
implementation is chosen.

Linux io_uring is a very well designed kernel syscall interface with
very high efficiency and no built in limits to scalability. This
implementation uses a 16Kb submission buffer and a 4Kb completion
buffer which means up to 256 i/o's can be in flight per i/o context
instance at a time. If you need more than this for a single kernel
thread, simply create multiple i/o multiplexers, and distribute your i/o
between them.

Note that support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

### Mac OS and FreeBSD

- `io_multiplexer::bsd_kqueue(size_t threads)` returns a BSD kqueues based
i/o multiplexer implementation. If `threads` is 1, the implementation
returned cannot be used by multiple threads (apart from `.post()`).

Note that FreeBSD's support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

### Windows

- `io_multiplexer::win_iocp(size_t threads)` returns a Windows IOCP based
i/o multiplexer implementation. If `threads` is 1, the implementation
returned cannot be used by multiple threads (apart from `.post()`).

Be aware that because Windows does not have scatter-gather i/o support
for some kinds of handle, on those kinds of handle a whole i/o is
issued per buffer. This can cause `maximum_pending_io()` to be reached
earlier than on other platforms.

This implementation issues an ideally minimum single syscall per i/o, and
completion checks poll the structure asynchronously updated by the kernel
to detect completion. This is as efficient as is currently possible to
do i/o on Microsoft Windows.

Note that support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

\snippet coroutines.cpp coroutines_example
*/
class LLFIO_DECL io_multiplexer
{
  friend class io_handle;

protected:
  native_handle_type _v;

public:
  using path_type = handle::path_type;
  using extent_type = handle::extent_type;
  using size_type = handle::size_type;
  using mode = handle::mode;
  using creation = handle::creation;
  using caching = handle::caching;
  using flag = handle::flag;

  //! The kinds of write reordering barrier which can be performed.
  enum class barrier_kind : uint8_t
  {
    nowait_data_only,  //!< Barrier data only, non-blocking. This is highly optimised on NV-DIMM storage, but consider using `nvram_barrier()` for even better performance.
    wait_data_only,    //!< Barrier data only, block until it is done. This is highly optimised on NV-DIMM storage, but consider using `nvram_barrier()` for even better performance.
    nowait_all,        //!< Barrier data and the metadata to retrieve it, non-blocking.
    wait_all           //!< Barrier data and the metadata to retrieve it, block until it is done.
  };

  //! The scatter buffer type used by this handle. Guaranteed to be `TrivialType` and `StandardLayoutType`.
  //! Try to make address and length 64 byte, or ideally, `page_size()` aligned where possible.
  struct buffer_type
  {
    //! Type of the pointer to memory.
    using pointer = byte *;
    //! Type of the pointer to memory.
    using const_pointer = const byte *;
    //! Type of the iterator to memory.
    using iterator = byte *;
    //! Type of the iterator to memory.
    using const_iterator = const byte *;
    //! Type of the length of memory.
    using size_type = size_t;

    //! Default constructor
    buffer_type() = default;
    //! Constructor
    constexpr buffer_type(pointer data, size_type len) noexcept
        : _data(data)
        , _len(len)
    {
    }
    buffer_type(const buffer_type &) = default;
    buffer_type(buffer_type &&) = default;
    buffer_type &operator=(const buffer_type &) = default;
    buffer_type &operator=(buffer_type &&) = default;
    ~buffer_type() = default;

    // Emulation of this being a span<byte> in the TS

    //! Returns the address of the bytes for this buffer
    constexpr pointer data() noexcept { return _data; }
    //! Returns the address of the bytes for this buffer
    constexpr const_pointer data() const noexcept { return _data; }
    //! Returns the number of bytes in this buffer
    constexpr size_type size() const noexcept { return _len; }

    //! Returns an iterator to the beginning of the buffer
    constexpr iterator begin() noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator begin() const noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator cbegin() const noexcept { return _data; }
    //! Returns an iterator to after the end of the buffer
    constexpr iterator end() noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator end() const noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator cend() const noexcept { return _data + _len; }

  private:
    friend constexpr inline void _check_iovec_match();
    pointer _data;
    size_type _len;
  };
  //! The gather buffer type used by this handle. Guaranteed to be `TrivialType` and `StandardLayoutType`.
  //! Try to make address and length 64 byte, or ideally, `page_size()` aligned where possible.
  struct const_buffer_type
  {
    //! Type of the pointer to memory.
    using pointer = const byte *;
    //! Type of the pointer to memory.
    using const_pointer = const byte *;
    //! Type of the iterator to memory.
    using iterator = const byte *;
    //! Type of the iterator to memory.
    using const_iterator = const byte *;
    //! Type of the length of memory.
    using size_type = size_t;

    //! Default constructor
    const_buffer_type() = default;
    //! Constructor
    constexpr const_buffer_type(pointer data, size_type len) noexcept
        : _data(data)
        , _len(len)
    {
    }
    //! Converting constructor from non-const buffer type
    constexpr const_buffer_type(buffer_type b) noexcept
        : _data(b.data())
        , _len(b.size())
    {
    }
    const_buffer_type(const const_buffer_type &) = default;
    const_buffer_type(const_buffer_type &&) = default;
    const_buffer_type &operator=(const const_buffer_type &) = default;
    const_buffer_type &operator=(const_buffer_type &&) = default;
    ~const_buffer_type() = default;

    // Emulation of this being a span<byte> in the TS

    //! Returns the address of the bytes for this buffer
    constexpr pointer data() noexcept { return _data; }
    //! Returns the address of the bytes for this buffer
    constexpr const_pointer data() const noexcept { return _data; }
    //! Returns the number of bytes in this buffer
    constexpr size_type size() const noexcept { return _len; }

    //! Returns an iterator to the beginning of the buffer
    constexpr iterator begin() noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator begin() const noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator cbegin() const noexcept { return _data; }
    //! Returns an iterator to after the end of the buffer
    constexpr iterator end() noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator end() const noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator cend() const noexcept { return _data + _len; }

  private:
    pointer _data;
    size_type _len;
  };
#ifndef NDEBUG
  static_assert(std::is_trivial<buffer_type>::value, "buffer_type is not a trivial type!");
  static_assert(std::is_trivial<const_buffer_type>::value, "const_buffer_type is not a trivial type!");
  static_assert(std::is_standard_layout<buffer_type>::value, "buffer_type is not a standard layout type!");
  static_assert(std::is_standard_layout<const_buffer_type>::value, "const_buffer_type is not a standard layout type!");
#endif
  //! The scatter buffers type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  using buffers_type = span<buffer_type>;
  //! The gather buffers type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  using const_buffers_type = span<const_buffer_type>;
#ifndef NDEBUG
  // Is trivial in all ways, except default constructibility
  static_assert(std::is_trivially_copyable<buffers_type>::value, "buffers_type is not trivially copyable!");
  // static_assert(std::is_trivially_assignable<buffers_type, buffers_type>::value, "buffers_type is not trivially assignable!");
  // static_assert(std::is_trivially_destructible<buffers_type>::value, "buffers_type is not trivially destructible!");
  // static_assert(std::is_trivially_copy_constructible<buffers_type>::value, "buffers_type is not trivially copy constructible!");
  // static_assert(std::is_trivially_move_constructible<buffers_type>::value, "buffers_type is not trivially move constructible!");
  // static_assert(std::is_trivially_copy_assignable<buffers_type>::value, "buffers_type is not trivially copy assignable!");
  // static_assert(std::is_trivially_move_assignable<buffers_type>::value, "buffers_type is not trivially move assignable!");
  static_assert(std::is_standard_layout<buffers_type>::value, "buffers_type is not a standard layout type!");
#endif
  //! The i/o request type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  template <class T> struct io_request
  {
    T buffers{};
    extent_type offset{0};
    constexpr io_request() {}  // NOLINT (defaulting this breaks clang and GCC, so don't do it!)
    constexpr io_request(T _buffers, extent_type _offset)
        : buffers(std::move(_buffers))
        , offset(_offset)
    {
    }
  };
#ifndef NDEBUG
  // Is trivial in all ways, except default constructibility
  static_assert(std::is_trivially_copyable<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially copyable!");
  // static_assert(std::is_trivially_assignable<io_request<buffers_type>, io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially assignable!");
  // static_assert(std::is_trivially_destructible<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially destructible!");
  // static_assert(std::is_trivially_copy_constructible<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially copy constructible!");
  // static_assert(std::is_trivially_move_constructible<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially move constructible!");
  // static_assert(std::is_trivially_copy_assignable<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially copy assignable!");
  // static_assert(std::is_trivially_move_assignable<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially move assignable!");
  static_assert(std::is_standard_layout<io_request<buffers_type>>::value, "io_request<buffers_type> is not a standard layout type!");
#endif
  //! The i/o result type used by this handle. Guaranteed to be `TrivialType` apart from construction.
  template <class T> struct io_result : public LLFIO_V2_NAMESPACE::result<T>
  {
    using Base = LLFIO_V2_NAMESPACE::result<T>;
    size_type _bytes_transferred{static_cast<size_type>(-1)};

#if defined(_MSC_VER) && !defined(__clang__)  // workaround MSVC parsing bug
    constexpr io_result()
        : Base()
    {
    }
    template <class... Args>
    constexpr io_result(Args &&... args)
        : Base(std::forward<Args>(args)...)
    {
    }
#else
    using Base::Base;
    io_result() = default;
#endif
    ~io_result() = default;
    io_result &operator=(io_result &&) = default;  // NOLINT
#if LLFIO_EXPERIMENTAL_STATUS_CODE
    io_result(const io_result &) = delete;
    io_result &operator=(const io_result &) = delete;
#else
    io_result(const io_result &) = default;
    io_result &operator=(const io_result &) = default;
#endif
    io_result(io_result &&) = default;  // NOLINT
    //! Returns bytes transferred
    size_type bytes_transferred() noexcept
    {
      if(_bytes_transferred == static_cast<size_type>(-1))
      {
        _bytes_transferred = 0;
        for(auto &i : this->value())
        {
          _bytes_transferred += i.size();
        }
      }
      return _bytes_transferred;
    }
  };
#if !defined(NDEBUG) && !LLFIO_EXPERIMENTAL_STATUS_CODE
  // Is trivial in all ways, except default constructibility
  static_assert(std::is_trivially_copyable<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially copyable!");
// static_assert(std::is_trivially_assignable<io_result<buffers_type>, io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially assignable!");
// static_assert(std::is_trivially_destructible<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially destructible!");
// static_assert(std::is_trivially_copy_constructible<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially copy constructible!");
// static_assert(std::is_trivially_move_constructible<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially move constructible!");
// static_assert(std::is_trivially_copy_assignable<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially copy assignable!");
// static_assert(std::is_trivially_move_assignable<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially move assignable!");
//! \todo Why is io_result<buffers_type> not a standard layout type?
// static_assert(std::is_standard_layout<result<buffers_type>>::value, "result<buffers_type> is not a standard layout type!");
// static_assert(std::is_standard_layout<io_result<buffers_type>>::value, "io_result<buffers_type> is not a standard layout type!");
#endif

  /*! \brief Choose the best available i/o context implementation for this platform.
   */
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> best_available(size_t threads) noexcept;
#ifdef __linux__
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> linux_epoll(size_t threads) noexcept;
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> linux_io_uring() noexcept;
#elif defined(__FreeBSD__) || defined(__APPLE__)
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> bsd_kqueue(size_t threads) noexcept;
#elif defined(_WIN32)
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> win_alertable() noexcept;
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> win_iocp(size_t threads) noexcept;
#else
#error Unknown platform
#endif

  io_multiplexer(io_multiplexer &&) = delete;
  io_multiplexer(const io_multiplexer &) = delete;
  io_multiplexer &operator=(io_multiplexer &&) = delete;
  io_multiplexer &operator=(const io_multiplexer &) = delete;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~io_multiplexer() {}

  //! The native handle used by this i/o context
  native_handle_type native_handle() const noexcept { return _v; }

  /*! \brief Invokes any posted items, until all items posted at the time of call
  have been executed, or either the count or deadline limit is reached. Returns
  the number of posted items invoked. Does not block if there are no posted items.

  Note that unlike most other deadline taking APIs in LLFIO, this one never
  returns `errc::timed_out`, it simply exits sooner than it would have done
  otherwise.

  \mallocs May perform a dynamic memory free, and a single mutex lock-unlock cycle
  per posted item invoked. Clock retrieving syscalls are performed if the deadline
  is non zero or non infinite.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<int> invoke_posted_items(int max_items = -1, deadline d = deadline()) noexcept = 0;

  /*! \brief Updates the internal ordered list of pending i/o according to interval
  to deadline expiry, and for all pending i/o later than their deadline, cancels
  the i/o and invokes completion with `errc::timed_out`, returning the number of
  pending i/o completed. If no pending i/o has passed its deadline, returns a
  negative number. If there is no pending i/o with deadlines, returns zero.

  Only the expired i/o calculated at the beginning is completed -- therefore this
  routine will not enter an infinite loop if completions take a long time.

  For efficiency, this call may complete fewer than the total pending i/o
  which has timed out at the time of the call.

  Note that unlike most other deadline taking APIs in LLFIO, this one never
  returns `errc::timed_out`, it simply exits sooner than it would have done
  otherwise.

  \mallocs May perform a dynamic memory allocation per previously unseen pending i/o
  in order to calculate an ordered list of pending i/o. A single mutex lock-unlock
  cycle may occur per completed i/o. Clock retrieving syscalls are performed to
  fetch the current time.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<int> timeout_io(int max_items = -1, deadline d = deadline()) noexcept = 0;

  /*! \brief Efficiently check all pending i/o for i/o completion, which may
  or may not include time out, and invoke completion appropriately, returning
  the number of pending i/o completed. If no pending i/o has been completed,
  returns a negative number. If there is no pending i/o at all, returns zero.

  For efficiency, this call may complete fewer than the total pending i/o
  which has completed at the time of the call.

  Note that unlike most other deadline taking APIs in LLFIO, this one never
  returns `errc::timed_out`, it simply exits sooner than it would have done
  otherwise.

  The deadline is ignored on the Windows IOCP and Alertable multiplexers,
  as once we reap completions from the OS, we must invoke completions on all
  of them before returning.

  \mallocs May call a syscall per previously unseen pending i/o. Will call
  a syscall to ask the kernel for which pending i/o have completed. A single
  mutex lock-unlock cycle may occur per completed i/o. Clock retrieving
  syscalls are performed if the deadline is non zero or non infinite.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<int> complete_io(int max_items = -1, deadline d = deadline()) noexcept = 0;

  /*! \brief Block up to the deadline specified waiting for posted items, i/o
  to complete, or i/o to time out. Returns the number of items of work done.

  This is partially a combination of `.invoke_posted_items()`, `.timeout_io()`
  and `.complete_io()`, however unlike those calls where the deadline
  specifies when to early exit, this call will sleep the calling thread
  until new events occur.

  Immediately returns zero if there is no work which could be done (no
  pending items, no pending i/o), as some implementations cannot sleep
  a thread when there is no pending i/o. You can always detect when i/o
  became pending from the i/o state's `.poll()` return value.

  This is a fairly expensive call. However if you are willing to sleep
  the thread, expense is probably not so important. High performance code
  probably ought to spin loop all three of the functions above for some
  period of time until entering power efficient sleep using this function.
  This function will wake the thread whenever something new happens.

  \mallocs Many dynamic memory allocations and syscalls and mutex lock-unlock cycles
  may be performed. This is a highly non-deterministic function.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<int> run(int max_items = -1, deadline d = deadline()) noexcept = 0;
  LLFIO_DEADLINE_TRY_FOR_UNTIL(run)

protected:
  constexpr io_multiplexer() {}

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _post(function_ptr<void *(void *)> &&f) noexcept = 0;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<int> _register_io_handle(handle *h) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _deregister_io_handle(handle *h) noexcept = 0;

public:
  // Called when i/o has been scheduled
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _scheduled_io(detail::io_operation_connection *op) noexcept = 0;
  // Called when scheduled i/o has been completed by the OS, but the Receiver has not been invoked yet
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _os_has_completed_io(detail::io_operation_connection *op) noexcept = 0;

  /*! Schedule the callable to be invoked by the thread owning this object and executing `run()` at its next
  available opportunity. Unlike any other function in this API layer, this function is thread safe.

  \mallocs Up to one dynamic memory allocation to type erase the callable (there is a
  non-allocating small object optimisation), and a mutex lock/unlock cycle.
  */
  template <class U> result<void> post(U &&f) noexcept
  {
    try
    {
      struct _
      {
        U f;
        function_ptr<void(void *)> next;
        // If called with nullptr, execute and return next
        // If called with object and is unset, set object
        // If called with object and is set, return previously set object
        void *operator()(void *n)
        {
          if(n != nullptr)
          {
            if(!next)
            {
              next = std::move(*reinterpret_cast<function_ptr<void(void *)> *>(n));
            }
            return &next;
          }
          f();
          return &next;
        }
      };
      _post(make_function_ptr<void *(void *)>(_{std::forward<U>(f), function_ptr<void(void *)>()}));
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

  //! \brief An awaitable returned by `co_post_self_to_context()`.
  struct co_post_self_to_context_awaitable
  {
    io_multiplexer *ctx;

    explicit co_post_self_to_context_awaitable(io_multiplexer *_ctx)
        : ctx(_ctx)
    {
    }

    bool await_ready() { return false; }
#if LLFIO_ENABLE_COROUTINES
    void await_suspend(coroutine_handle<> co)
    {
      ctx->post([co = co]() mutable { co.resume(); }).value();
    }
#endif
    void await_resume() {}
  };
  /*! \brief Return an awaitable suspending execution of this coroutine on the current kernel thread,
  and resuming execution on the kernel thread running this i/o service. This is a
  convenience wrapper for `.post()`.
  */
  co_post_self_to_context_awaitable co_post_self_to_context() { return co_post_self_to_context_awaitable(this); }
};

//! \brief Thread local settings
namespace this_thread
{
  //! \brief Return the calling thread's current i/o multiplexer.
  LLFIO_HEADERS_ONLY_FUNC_SPEC io_multiplexer *multiplexer() noexcept;
  //! \brief Set the calling thread's current i/o multiplexer.
  LLFIO_HEADERS_ONLY_FUNC_SPEC void set_multiplexer(io_multiplexer *ctx) noexcept;

  //! \brief Used to prevent recursion of i/o completions
  struct LLFIO_DECL delay_invoking_io_completion
  {
    static void add(detail::io_operation_connection *op);
    static void remove(detail::io_operation_connection *op);

    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC delay_invoking_io_completion(int &count);
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC ~delay_invoking_io_completion();

  private:
    int &_count;
    delay_invoking_io_completion *_prev{nullptr};
  };
}  // namespace this_thread

//! \brief The status type for an i/o connection state
enum class io_state_status : uint8_t
{
  unknown,      //!< This i/o connection state is uninitialised or moved from
  unscheduled,  //!< This i/o connection state has been connected but not started
  scheduled,    //!< This i/o connection state has been started but has not completed
  completed     //!< This i/o connection state has completed
};

namespace detail
{
  struct io_operation_connection
  {
    using status_type = io_state_status;
#ifdef _WIN32
    static constexpr size_t max_overlappeds = 64;
#endif

    // Used in doubly linked lists for scheduled and OS completed
    io_operation_connection *prev{nullptr}, *next{nullptr};
    // Used in singlely linked list for non-recursive completion invocations
    io_operation_connection *delay_invoking_prev{nullptr}, *delay_invoking_next{nullptr};
    // Set at start for duration timed out i/o
    std::chrono::steady_clock::time_point deadline_duration;
    handle *h{nullptr};

    // args to op and internal metadata
    deadline d;
    io_multiplexer::barrier_kind barrierkind{io_multiplexer::barrier_kind::wait_all};
    bool is_scheduled{false};                   // whether state is scheduled
    signed char is_added_to_deadline_list{-1};  // =1 if state needs to be removed from ordered deadline lists
    bool is_os_completed{false};                // whether the OS kernel is done with this state but Receiver::set_value() has not yet been called
    bool is_done_set{false};                    // whether Receiver::set_done() been called
    bool is_being_destructed{false};            // whether i/o is being cancelled due to state destruction, in which case don't call Receiver::set_value()
    bool is_immediately_done_after_completion{false}; // whether to call Receiver::set_done() immediately after Receiver::set_value()

#ifdef _WIN32
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)  // nonstandard extension used
#endif
    struct _EXTENDED_IO_STATUS_BLOCK
    {
      union {
        volatile long Status;  // volatile has acquire-release atomic semantics on MSVC
        void *Pointer;
      };
      size_t Information;
    } ols[max_overlappeds];
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

  protected:
    io_operation_connection(handle &_h, deadline _d, io_multiplexer::barrier_kind kind)
        : h(&_h)
        , d(_d)
        , barrierkind(kind)
    {
    }
    virtual ~io_operation_connection() { h = nullptr; }

  public:
    enum class _poll_kind : uint8_t
    {
      check,
      complete,
      timeout
    };
    // Called by io_handle to immediately cause the setting of the output buffers
    // and invocation of the receiver with the result. Or sets a failure.
    // Used to skip the overhead of poll() where an i/o completed immediately,
    // and no pending i/o overhead is required. DO NOT CALL IF EVER WAS PENDING!
    virtual void _complete_io(result<size_t> /*unused*/) noexcept { abort(); }
    // Called by anyone to cancel any started i/o
    virtual void cancel() noexcept { abort(); }
    // Called by anyone to cause the checking for i/o completion or
    // timed out, and if so, to complete the i/o
    virtual status_type _poll(_poll_kind /*unused*/) noexcept { abort(); }
  };

  template <class T> class fake_atomic
  {
    T _v;

  public:
    constexpr fake_atomic(T v)
        : _v(v)
    {
    }
    T load(std::memory_order /*unused*/) const { return _v; }
    void store(T v, std::memory_order /*unused*/) { _v = v; }
    void fetch_add(T v, std::memory_order /*unused*/) { _v += v; }
    void fetch_sub(T v, std::memory_order /*unused*/) { _v -= v; }
  };
  struct fake_mutex
  {
    void lock() {}
    void unlock() {}
  };
  LLFIO_HEADERS_ONLY_FUNC_SPEC error_info ntkernel_error_from_overlapped(size_t);

  template <class BuffersType, bool use_atomic> struct io_sender : public io_operation_connection
  {
    static constexpr bool is_atomic = use_atomic;
    using buffers_type = BuffersType;
    using request_type = typename io_multiplexer::io_request<buffers_type>;
    using result_type = typename io_multiplexer::io_result<buffers_type>;
    using error_type = typename result_type::error_type;
    using status_type = typename io_operation_connection::status_type;
    using _barrier_kind = typename io_multiplexer::barrier_kind;
    using result_set_type = std::conditional_t<is_atomic, std::atomic<status_type>, fake_atomic<status_type>>;
    using lock_type = std::conditional_t<is_atomic, spinlocks::spinlock<uintptr_t>, fake_mutex>;
    using lock_guard = spinlocks::lock_guard<lock_type>;

  protected:
    lock_type lock;
    result_set_type status{status_type::unscheduled};
    union storage_t {
      request_type req;
      result_type ret;
      storage_t(request_type _req)
          : req(std::move(_req))
      {
      }
      storage_t(result_type _ret)
          : ret(std::move(_ret))
      {
      }
      ~storage_t() {}
      storage_t(storage_t &&o, status_type which) noexcept
          : storage_t((which == status_type::completed) ? storage_t(std::move(o.ret)) : storage_t(std::move(o.req)))
      {
      }
      storage_t(const storage_t &o, status_type which)
          : storage_t((which == status_type::completed) ? storage_t(o.ret) : storage_t(o.req))
      {
      }
    } storage;

  public:
    io_sender(handle &h, request_type req = {}, LLFIO_V2_NAMESPACE::deadline d = LLFIO_V2_NAMESPACE::deadline(), _barrier_kind kind = _barrier_kind::wait_all)
        : io_operation_connection(h, d, kind)
        , storage(req)
    {
    }

  protected:
    io_sender(io_sender &&o) noexcept
        : io_operation_connection(std::move(o))
        , status(o.status.load(std::memory_order_acquire))
        , storage(std::move(o.storage), status.load(std::memory_order_acquire))
    {
      switch(o.status.load(std::memory_order_acquire))
      {
      case status_type::unknown:
        break;
      case status_type::unscheduled:
        // destruct req
        o.storage.req.~request_type();
        break;
      case status_type::scheduled:
        // Should never occur
        abort();
      case status_type::completed:
        // destruct ret
        o.storage.ret.~result_type();
        break;
      }
      o.status.store(status_type::unknown, std::memory_order_release);
    }
    io_sender(const io_sender &o)
        : io_operation_connection(o)
        , status(o.status.load(std::memory_order_acquire))
        , storage(o.storage, status.load(std::memory_order_acquire))
    {
    }
    ~io_sender()
    {
      switch(status.load(std::memory_order_acquire))
      {
      case status_type::unknown:
        break;
      case status_type::unscheduled:
        // destruct req
        storage.req.~request_type();
        break;
      case status_type::scheduled:
        // Should never occur
        abort();
      case status_type::completed:
        // Detect attempt to destroy a completed state whose
        // .set_done() hasn't been called yet to indicate state
        // has been released
        assert(is_done_set);
        if(!is_done_set)
        {
          abort();
        }
        // destruct ret
        storage.ret.~result_type();
        break;
      }
    }

  public:
    //! Return the associated handle
    LLFIO_V2_NAMESPACE::handle *handle() const noexcept { return this->h; }
    //! True if started
    bool started() const noexcept { return status.load(std::memory_order_acquire) != status_type::unscheduled; }
    //! True if completed
    bool completed() const noexcept { return status.load(std::memory_order_acquire) == status_type::completed; }
    //! Access the request to be made when started
    request_type &request() noexcept
    {
      assert(status.load(std::memory_order_acquire) != status_type::scheduled);
      return storage.req;
    }
    //! Access the request to be made when started
    const request_type &request() const noexcept
    {
      assert(status.load(std::memory_order_acquire) != status_type::scheduled);
      return storage.req;
    }
    //! Access the deadline to be used when started
    LLFIO_V2_NAMESPACE::deadline &deadline() noexcept
    {
      assert(status.load(std::memory_order_acquire) != status_type::scheduled);
      return this->d;
    }
    //! Access the deadline to be used when started
    const LLFIO_V2_NAMESPACE::deadline &deadline() const noexcept
    {
      assert(status.load(std::memory_order_acquire) != status_type::scheduled);
      return this->d;
    }

  protected:
    // Lock must be held on entry!
    void _create_result(result<size_t> toset) noexcept
    {
      if(toset.has_error())
      {
        // Set the result
        storage.req.~request_type();
        new(&storage.ret) result_type(std::move(toset.error()));
        status.store(status_type::completed, std::memory_order_release);
        return;
      }
      buffers_type ret(storage.req.buffers);
      size_t bytes_transferred = toset.value();
      if(bytes_transferred != (size_t) -1)
      {
#ifdef _WIN32
        for(size_t n = 0; n < storage.req.buffers.size(); n++)
        {
          auto internal = ols[n].Status;
          if(internal != 0)
          {
            if(internal == 0x103 /*STATUS_PENDING*/)
            {
              abort();
            }
            storage.req.~request_type();
            new(&storage.ret) result_type(ntkernel_error_from_overlapped(internal));
            status.store(status_type::completed, std::memory_order_release);
            return;
          }
          storage.req.buffers[n] = {storage.req.buffers[n].data(), ols[n].Information};
          if(storage.req.buffers[n].size() != 0)
          {
            ret = {ret.data(), n + 1};
          }
        }
#else
        // Set each individual buffer filled
        for(size_t i = 0; i < storage.req.buffers.size(); i++)
        {
          auto &buffer = ret[i];
          if(buffer.size() <= bytes_transferred)
          {
            bytes_transferred -= buffer.size();
          }
          else
          {
            buffer = {buffer.data(), (size_t) bytes_transferred};
            ret = {ret.data(), i + 1};
            break;
          }
        }
#endif
      }
      // Set the result
      storage.req.~request_type();
      new(&storage.ret) result_type(std::move(ret));
      status.store(status_type::completed, std::memory_order_release);
    }
  };
}  // namespace detail

//! \brief A Sender of a non-atomic async read i/o on a handle
template <bool use_atomic> struct async_read_sender : protected detail::io_sender<typename io_multiplexer::buffers_type, use_atomic>
{
  using _base = detail::io_sender<typename io_multiplexer::buffers_type, use_atomic>;
  using result_type = typename io_multiplexer::template io_result<typename io_multiplexer::buffers_type>;
  using _base::deadline;
  using detail::io_sender<typename io_multiplexer::buffers_type, use_atomic>::io_sender;
  using _base::request;

protected:
  void _begin_io() noexcept
  {
    using async_op = typename handle::_async_op;
    // Begin a read
#ifdef _WIN32
    if(this->h->_is_multiplexer_apc())
    {
      this->h->_apc_begin(async_op::read, this, &this->storage.req);
    }
    else if(this->h->_is_multiplexer_iocp())
    {
      this->h->_iocp_begin(async_op::read, this, &this->storage.req);
    }
    else
    {
      abort();
    }
#endif
  }
  void _cancel_io() noexcept
  {
    // Cancel a read
#ifdef _WIN32
    if(this->h->_is_multiplexer_apc())
    {
      this->h->_apc_cancel(this, &this->storage.req);
    }
    else if(this->h->_is_multiplexer_iocp())
    {
      this->h->_iocp_cancel(this, &this->storage.req);
    }
    else
    {
      abort();
    }
#endif
  }
};
//! \brief A Sender of an async read i/o on a handle
using async_read = async_read_sender<false>;
//! \brief A Sender of an atomic async read i/o on a handle
using atomic_async_read = async_read_sender<true>;

//! \brief A Sender of a non-atomic async write i/o on a handle
template <bool use_atomic> struct async_write_sender : protected detail::io_sender<typename io_multiplexer::const_buffers_type, use_atomic>
{
  using _base = detail::io_sender<typename io_multiplexer::const_buffers_type, use_atomic>;
  using result_type = typename io_multiplexer::template io_result<typename io_multiplexer::const_buffers_type>;
  using _base::deadline;
  using detail::io_sender<typename io_multiplexer::const_buffers_type, use_atomic>::io_sender;
  using _base::request;

protected:
  void _begin_io() noexcept
  {
    // Begin a write
#ifdef _WIN32
    if(this->h->_is_multiplexer_apc())
    {
      this->h->_apc_begin(async_op::write, this, &this->storage.req);
    }
    else if(this->h->_is_multiplexer_iocp())
    {
      this->h->_iocp_begin(async_op::write, this, &this->storage.req);
    }
    else
    {
      abort();
    }
#endif
  }
  void _cancel_io() noexcept
  {
    // Cancel a write
#ifdef _WIN32
    if(this->h->_is_multiplexer_apc())
    {
      this->h->_apc_cancel(this, &this->storage.req);
    }
    else if(this->h->_is_multiplexer_iocp())
    {
      this->h->_iocp_cancel(this, &this->storage.req);
    }
    else
    {
      abort();
    }
#endif
  }
};
//! \brief A Sender of an async write i/o on a handle
using async_write = async_write_sender<false>;
//! \brief A Sender of an atomic async write i/o on a handle
using atomic_async_write = async_write_sender<true>;

//! \brief A Sender of a non-atomic async barrier i/o on a handle
template <bool use_atomic> class async_barrier_sender : protected detail::io_sender<typename io_multiplexer::const_buffers_type, use_atomic>
{
  using _base = detail::io_sender<typename io_multiplexer::const_buffers_type, use_atomic>;
  using barrier_kind = typename io_multiplexer::barrier_kind;

public:
  using status_type = typename _base::status_type;
  using request_type = typename io_multiplexer::template io_request<typename io_multiplexer::const_buffers_type>;
  using result_type = typename io_multiplexer::template io_result<typename io_multiplexer::const_buffers_type>;
  using _base::deadline;
  using detail::io_sender<typename io_multiplexer::const_buffers_type, use_atomic>::io_sender;
  using _base::request;

  //! Access the barrier kind to be made when started
  barrier_kind &kind() noexcept
  {
    assert(this->status.load(std::memory_order_acquire) == status_type::unscheduled);
    return this->_barrierkind;
  }
  //! Access the barrier kind to be made when started
  const barrier_kind &kind() const noexcept
  {
    assert(this->status.load(std::memory_order_acquire) == status_type::unscheduled);
    return this->_barrierkind;
  }

  template <class HandleType>
  explicit async_barrier_sender(HandleType &h, request_type req = {}, barrier_kind kind = barrier_kind::nowait_data_only, LLFIO_V2_NAMESPACE::deadline d = LLFIO_V2_NAMESPACE::deadline())
      : _base(h, req, d, kind)
  {
  }

protected:
  void _begin_io() noexcept
  {
    // Begin a barrier
#ifdef _WIN32
    if(this->h->_is_multiplexer_apc())
    {
      this->h->_apc_begin(async_op::barrier, this, &this->storage.req);
    }
    else if(this->h->_is_multiplexer_iocp())
    {
      this->h->_iocp_begin(async_op::barrier, this, &this->storage.req);
    }
    else
    {
      abort();
    }
#endif
  }
  void _cancel_io() noexcept
  {
    // Cancel a barrier
#ifdef _WIN32
    if(this->h->_is_multiplexer_apc())
    {
      this->h->_apc_cancel(this, &this->storage.req);
    }
    else if(this->h->_is_multiplexer_iocp())
    {
      this->h->_iocp_cancel(this, &this->storage.req);
    }
    else
    {
      abort();
    }
#endif
  }
};
//! \brief A Sender of an async barrier i/o on a handle
using async_barrier = async_barrier_sender<false>;
//! \brief A Sender of an atomic async barrier i/o on a handle
using atomic_async_barrier = async_barrier_sender<true>;

/*! \brief The i/o operation connection state type.

\warning Once started, you cannot change the address of this object until the
i/o completes. Doing so will terminate the process.
*/
template <class Sender, class Receiver>                                          //
LLFIO_REQUIRES(std::is_base_of<detail::io_operation_connection, Sender>::value)  //
class io_operation_connection : protected Sender
{
  static_assert(std::is_base_of<detail::io_operation_connection, Sender>::value, "Sender type is not an i/o sender type");

public:
  using sender_type = Sender;
  using receiver_type = Receiver;
  static constexpr bool is_atomic = sender_type::is_atomic;
  using request_type = typename sender_type::request_type;
  using result_type = typename sender_type::result_type;
  using error_type = typename sender_type::error_type;

protected:
  using _status_type = typename sender_type::status_type;
  using _lock_guard = typename sender_type::lock_guard;
  using _poll_kind = typename sender_type::_poll_kind;
  Receiver _receiver;

  virtual void _complete_io(result<size_t> bytes_transferred) noexcept override final
  {
    if(this->status.load(std::memory_order_acquire) != _status_type::scheduled)
    {
      abort();
    }
    if(!this->is_being_destructed)
    {
      // Set success or failure
      sender_type::_create_result(std::move(bytes_transferred));
#ifdef LLFIO_DEBUG_PRINT
      std::cerr << "_complete_io " << this << std::endl;
#endif
      _receiver.set_value(std::move(this->storage.ret));
      if(this->is_immediately_done_after_completion)
      {
        this->is_done_set = true;
        _receiver.set_done();
      }
    }
    else
    {
      // suppress the cancelled error log message
      sender_type::_create_result((size_t) -1);
    }
  }

public:
  //! \brief Use the `connect()` function in preference to using this directly
  template <class _Sender, class _Receiver>
  io_operation_connection(_Sender &&sender, _Receiver &&receiver)
      : Sender(std::forward<_Sender>(sender))
      , _receiver(std::forward<_Receiver>(receiver))
  {
  }
  io_operation_connection(const io_operation_connection &) = default;
  io_operation_connection(io_operation_connection &&o) noexcept
      : Sender(std::move(o))
      , _receiver(std::move(o._receiver))
  {
    assert(this->status.load(std::memory_order_acquire) != _status_type::scheduled);
    if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      abort();
    }
  }
  io_operation_connection &operator=(const io_operation_connection &) = default;
  io_operation_connection &operator=(io_operation_connection &&o) noexcept
  {
    this->~io_operation_connection();
    new(this) io_operation_connection(std::move(o));
    return *this;
  }
  //! If the i/o is started and not completed, will call `.cancel()`, which may block.
  ~io_operation_connection()
  {
    if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      _lock_guard g(this->lock);
      if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
      {
        // std::cerr << "~io_operation_connection " << this << std::endl;
        /* We must do a custom kind of cancel here. We cannot set value, for
        a coroutine receiver that would resume the coroutine, and this destructor
        may be being called because the coroutine frame is being destroyed.
        Reentering the coroutine in this circumstance is bad.
        */
        this->is_being_destructed = true;  // prevent set value from being called
        this->_cancel_io();                // tell the OS to cancel the i/o

        // We must block until OS completes the cancellation request
        while(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
        {
          this->lock.unlock();
          (void) h->multiplexer()->complete_io();
          this->lock.lock();
        }
        // complete_io() should have called set_done() for us
        assert(this->is_done_set);
      }
    }
  }

  using Sender::completed;
  using Sender::handle;
  using Sender::started;
  //! \brief The multiplexer of the handle upon which this state will be scheduled
  io_multiplexer *multiplexer() const noexcept { return this->handle()->multiplexer(); }
  //! \brief Access the sender. Use of this between when the i/o is started and not completed will terminate the process (use the const overload instead).
  sender_type &sender() noexcept
  {
    assert(this->status.load(std::memory_order_acquire) != _status_type::scheduled);
    if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      abort();
    }
    return *this;
  }
  //! \brief Access the sender
  const sender_type &sender() const noexcept { return *this; }
  //! \brief Access the receiver. Use of this between when the i/o is started and not completed will terminate the process (use the const overload instead).
  receiver_type &receiver() noexcept
  {
    assert(this->status.load(std::memory_order_acquire) != _status_type::scheduled);
    if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      abort();
    }
    return _receiver;
  }
  //! \brief Access the receiver
  const receiver_type &receiver() const noexcept { return _receiver; }

  /*! \brief Start the operation. Note that it may complete immediately, possibly
  with failure. Any relative deadline begins at this point. This operation is not
  threadsafe, only call it once from a single thread.
  */
  void start() noexcept
  {
    assert(this->status.load(std::memory_order_acquire) == _status_type::unscheduled);
    if(this->status.load(std::memory_order_acquire) != _status_type::unscheduled)
    {
      abort();
    }
    if(this->is_being_destructed)
    {
      cancel();
      return;
    }
    // If this i/o is timed, begin the timeout from now
    if(this->d)
    {
      if(this->d.steady)
      {
        this->deadline_duration = std::chrono::steady_clock::now() + std::chrono::nanoseconds(this->d.nsecs);
      }
    }
    this->status.store(_status_type::scheduled, std::memory_order_release);
    // Ask the Sender to begin to i/o
    this->_begin_io();
  }
  //! \brief Cancel the operation, if it is scheduled. Note that the state may
  //! be destructed upon return if that is what the receiver's `.set_done()` does.
  virtual void cancel() noexcept override final
  {
    if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      _lock_guard g(this->lock);
      if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
      {
        this->_cancel_io();
        sender_type::_create_result(errc::operation_canceled);
      }
    }
    // Set done. NOTE that set_done() may legally destruct the state!
    this->is_done_set = true;
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "set_done " << this << std::endl;
#endif
    _receiver.set_done();
  }
  //! Reset the operation back to a connected state
  void reset() noexcept
  {
    if(this->status.load(std::memory_order_acquire) != _status_type::completed || !this->is_done_set)
    {
      abort();
    }
    this->is_done_set = false;
    this->is_immediately_done_after_completion = false;
    this->status.store(_status_type::unscheduled, std::memory_order_release);
  }
  /*! \brief Poll the operation, executing completion if newly completed, without blocking.
   */
  _status_type poll() noexcept { return _poll(_poll_kind::check); }

protected:
  virtual _status_type _poll(_poll_kind caller) noexcept override final
  {
    _status_type ret = this->status.load(std::memory_order_acquire);
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "_poll " << this << " (status=" << (int) ret << ")" << std::endl;
#endif
    if(ret == _status_type::scheduled)
    {
      _lock_guard g(this->lock);
      ret = this->status.load(std::memory_order_acquire);  // check after lock grant
      if(ret == _status_type::scheduled)
      {
        if(caller == _poll_kind::check)
        {
#ifdef _WIN32
          // The OVERLAPPED structures can complete asynchronously. If they
          // have completed, complete i/o right now.
          bool asynchronously_completed = true;
          for(size_t n = 0; n < this->storage.req.buffers.size(); n++)
          {
            if(this->ols[n].Status == 0x103 /*STATUS_PENDING*/)
            {
              asynchronously_completed = false;
              break;
            }
          }
          if(asynchronously_completed)
          {
            caller = _poll_kind::complete;
          }
#endif
          // Have I timed out?
          if(this->d && caller != _poll_kind::complete)
          {
            if(this->deadline_duration != std::chrono::steady_clock::time_point())
            {
              if(std::chrono::steady_clock::now() >= this->deadline_duration)
              {
                caller = _poll_kind::timeout;
              }
            }
            else
            {
              if(std::chrono::system_clock::now() >= this->d.to_time_point())
              {
                caller = _poll_kind::timeout;
              }
            }
          }
        }
        if(caller == _poll_kind::complete)
        {
          _complete_io(result<size_t>(0));
          return _status_type::completed;
        }
        if(caller == _poll_kind::timeout)
        {
          this->_cancel_io();
          _complete_io(errc::timed_out);
          return _status_type::completed;
        }
      }
    }
    return ret;
  }
};

//! \brief Connect an `async_read` Sender with a Receiver
LLFIO_TEMPLATE(bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                                                      //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename io_multiplexer::template io_result<typename io_multiplexer::buffers_type>>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_read_sender<use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_read_sender<use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

//! \brief Connect an `async_write` Sender with a Receiver
LLFIO_TEMPLATE(bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                                                            //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename io_multiplexer::template io_result<typename io_multiplexer::const_buffers_type>>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_write_sender<use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_write_sender<use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

//! \brief Connect an `async_barrier` Sender with a Receiver
LLFIO_TEMPLATE(bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                                                            //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename io_multiplexer::template io_result<typename io_multiplexer::const_buffers_type>>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_barrier_sender<use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_barrier_sender<use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

namespace detail
{
  struct null_io_awaitable_visitor
  {
    template <class T> void await_suspend(T * /*unused*/) {}
    template <class T> void await_resume(T * /*unused*/) {}
  };

  template <class ResultType> struct io_awaitable_receiver_type
  {
    ResultType *result{nullptr};
#if LLFIO_ENABLE_COROUTINES
    coroutine_handle<> coro;
#endif
    void set_value(ResultType &&res)
    {
      result = &res;
#if LLFIO_ENABLE_COROUTINES
      if(coro)
      {
        coro.resume();
      }
#endif
    }
    void set_done() {}
  };
}  // namespace detail

/*! \brief A convenience wrapper of `async_read`, `async_write` and `async_barrier`
into a coroutine awaitable.

`io_handle` provides these convenience wrapper functions returning this awaitable type:

- `.co_read()` and `.co_read_atomic()`.
- `.co_write()` and `.co_write_atomic()`.
- `.co_barrier()` and `.co_barrier_atomic()`.

Upon first `.await_ready()`, the awaitable begins the i/o. If the i/o completes immediately,
the awaitable is immediately ready and no coroutine suspension occurs.

If the i/o does not complete immediately, the coroutine is suspended. Subsequent calls
to `.await_ready()` call `.poll()` on the i/o connection state, which checks if the i/o
has completed without blocking. If the i/o completes, the coroutine is resumed.

For many users, to pump all pending i/o with completions in a set of coroutines scheduled
on the current kernel thread would look something like this:

\begincode
auto multiplexer = llfio::this_thread::multiplexer();
while(multiplexer->poll().value() > 0);
\endcode

This will resume any suspended coroutines as the i/o they are blocked against
completes. When the current thread's multiplexer has no i/o completions left
to process, `poll()` will return less than zero and the loop will exit.

`poll()` never sleeps the thread, so if in fact you want to pump all pending i/o until
there is no more pending i/o at all, you would do instead:

\begincode
auto multiplexer = llfio::this_thread::multiplexer();
while(multiplexer->run().value() > 0);
\endcode

If there are no i/o completions to process, but there is pending i/o, `run()` will
sleep the kernel thread until an i/o completes. When there is no pending i/o for
this multiplexer, `run()` returns with `0`.

Both `poll()` and `run()` are by their nature expensive calls. To provide a
mechanism by which a pure volatile checking spin loop can be conveniently
constructed, i/o awaitables can configured with a vistor whose
`.await_suspend(io_awaitable *)` is called when the coroutine is suspended,
and whose `.await_resume(io_awaitable *)` is called when the coroutine is resumed.
This lets you accumulate sublists of i/o awaitables into a container which can
be spin loop iterated, calling `.await_ready()` on each i/o awaitable.
*/
template <class SenderType, class Visitor = detail::null_io_awaitable_visitor> class OUTCOME_NODISCARD io_awaitable : protected io_operation_connection<SenderType, detail::io_awaitable_receiver_type<typename SenderType::result_type>>
{
  using _base = io_operation_connection<SenderType, detail::io_awaitable_receiver_type<typename SenderType::result_type>>;
  using _sender_type = SenderType;
  using _receiver_type = detail::io_awaitable_receiver_type<typename SenderType::result_type>;
  using _result_type = typename _sender_type::result_type;
  using _lock_guard = typename _base::lock_guard;
  Visitor _visitor;

public:
  explicit io_awaitable(_sender_type &&sender, Visitor &&visitor = {})
      : _base(connect(std::move(sender), _receiver_type()))
      , _visitor(std::move(visitor))
  {
  }
  io_awaitable(io_awaitable &&) = default;
  io_awaitable(const io_awaitable &o) = default;
  io_awaitable &operator=(io_awaitable &&) = delete;  // as per P1056
  io_awaitable &operator=(const io_awaitable &) = default;
  ~io_awaitable() = default;

  bool await_ready() noexcept
  {
    // If i/o not started yet, start it
    if(!this->started())
    {
      this->start();
    }
    // If i/o already completed, we are ready
    if(this->completed())
    {
      return true;
    }
    // Poll i/o for completion, if it completes we are ready
    if(this->poll() == _base::status_type::completed)
    {
      return true;
    }
    return false;
  }
  _result_type await_resume()
  {
    assert(this->_receiver.result != nullptr);
    _visitor.await_resume(this);
    return std::move(this->storage.ret);
  }
#if LLFIO_ENABLE_COROUTINES
  void await_suspend(coroutine_handle<> coro)
  {
    _lock_guard g(this->lock);
    if(this->completed())
    {
      coro.resume();
      return;
    }
    this->_receiver.coro = coro;
    _visitor.await_suspend(this);
  }
#endif
};

// BEGIN make_free_functions.py
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#include "detail/impl/io_multiplexer.ipp"
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
