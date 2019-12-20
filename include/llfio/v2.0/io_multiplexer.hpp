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

#include "handle.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

class io_handle;

namespace detail
{
  struct io_operation_connection;
}

template <class Awaitable, bool use_atomic> struct io_awaitable_promise_type;
template <class Cont, bool use_atomic> class OUTCOME_NODISCARD io_awaitable;

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
`.wait()` as soon as possible. Be aware that if nobody calls `.wait()`
or `.check_posted_items()`, posted work is never executed.

For all i/o multiplexer implementations, no dynamic memory allocations
occur during i/o unless you call `.wait()`. If `threads` is less than or
equal to `1`, no locking occurs during i/o either. This makes multiplexed
i/o very low overhead. It is effectively a spinlooped polling implementation
by default.

The price paid for very low overhead i/o is that `.wait()` must be a very
expensive call involving many syscalls and dynamic
memory allocations and multiple mutex lock/unlock cycles. This is because
all previously unseen pending i/o must be registered into the OS kernel's
reactor, the earliest expiring timeout i/o calculated, and the blocking
wait executed. Once an i/o is registered, when the i/o completes it will
introduce overhead to deregister that i/o with the OS kernel, which will introduce extra
overhead on completions of all pending i/o at the time of blocking. For
most use cases, this is a very desirable tradeoff.

Be aware that if you ever call `.check_deadlined_io()` or `.wait()`, i/o
with a non-zero non-infinite deadline is slightly more
expensive than zero or infinite deadlined i/o, as all pending i/o must be
ordered into a coherent list in order to calculate the earliest expiring i/o.

For some use cases where all kernel thread sleeps are unacceptable, you want
an exclusively non-blocking i/o implementation.
This can be implemented using `.poll()` on the connection states, and
manually invoking `.check_posted_items()`. There is also another intermediate
configuration, where one kernel thread periodically calls `.check_posted_items()`
and `.check_deadlined_io()` to dispatch posted work and cancel and complete
i/o which has timed out, and other threads manually `.poll()` on the
connection states for completion on an as-needed basis.

Finally, be aware that `.poll_for()` and `.poll_until()` on the connection
state, if given a timeout later than now, will call `.wait()` on the
associated multiplexer, looping it until the connection state completes
either via i/o completion or time out.


## Available implementations

There are multiple i/o multiplexer implementations available, each with
varying tradeoffs. Some of the implementations take a `threads`
parameter. If `> 1`, the implementation returned can handle more
than one thread using the same instance at a time. In this situation,
receivers are invoked in the next available idle thread
blocked within `.wait()`.

`.wait()` returns when at least one i/o completion or post was processed.
This enables you to sleep the kernel thread until some i/o completes
somewhere, and then to ask `.completed()` on your connected i/o state to
see if it has completed. The `.poll_for()`, `.poll_until()` and `.poll_until_ready()`
calls are convenience wrappers of `.wait()` which do this for you.
For improved efficiency, some of the `.wait()` implementations will process
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
  template <class Cont, bool use_atomic> friend class io_awaitable;

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
  enum class barrier_kind
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

  /*! \brief Checks if any items have been posted, and if so executes one, returning true.
  If no items have been posted, returns false.

  \mallocs May perform a dynamic memory free, and a single mutex lock-unlock cycle.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC bool check_posted_items() noexcept = 0;

  /*! \brief Checks if any currently pending i/o has passed its deadline, and if so
  completes the most expired i/o now, returning true. If no pending i/o has passed
  its deadline, returns false.

  \mallocs May perform a dynamic memory allocation per previously unseen pending i/o
  in order to calculate an ordered list of pending i/o. A single mutex lock-unlock
  cycle may occur.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<bool> check_deadlined_io() noexcept = 0;

  /*! \brief Calls `check_posted_items()` and `check_deadlined_io()`, returning `1`
  if either returned true. If neither, registers any previously unseen pending i/o
  with the operating system for state change notifications and waits for the first
  i/o to signal. For all i/o reported by the operating system to have completed,
  invokes the completion of their Receivers. Returns the number of i/o completed;
  `errc::timed_out` if the deadline passed.

  \mallocs Many dynamic memory allocations and syscalls and mutex lock-unlock cycles
  may be performed. This is a non-deterministic function.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<size_t> wait(deadline d = deadline()) noexcept = 0;
  LLFIO_DEADLINE_TRY_FOR_UNTIL(wait)

protected:
  constexpr io_multiplexer() {}

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _post(function_ptr<void *(void *)> &&f) noexcept = 0;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _register_io_handle(handle *h) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _deregister_io_handle(handle *h) noexcept = 0;

public:
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _register_pending_io(detail::io_operation_connection *op) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _deregister_pending_io(detail::io_operation_connection *op) noexcept = 0;

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
}  // namespace this_thread

namespace detail
{
  struct io_operation_connection
  {
    enum class status_type
    {
      unknown,
      unscheduled,
      scheduled,
      completed
    };
#ifdef _WIN32
    static constexpr size_t max_overlappeds = 64;
#endif

    io_operation_connection *prev{nullptr}, *next{nullptr};
    std::chrono::steady_clock::time_point deadline_duration;
    std::chrono::system_clock::time_point deadline_absolute;
#ifdef _WIN32
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)  // nonstandard extension used
#endif
    struct _OVERLAPPED
    {
      volatile size_t Internal;  // volatile has acquire-release atomic semantics on MSVC
      size_t InternalHigh;
      union {
        struct
        {
          unsigned long Offset;
          unsigned long OffsetHigh;
        };
        void *Pointer;
      };
      void *hEvent;
    } ols[max_overlappeds];
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif
    io_multiplexer *ctx;
    native_handle_type nativeh;
    deadline d;
    bool is_added_to_deadline_list{false};
    bool is_registered_with_io_multiplexer{false};

  protected:
    io_operation_connection(handle &h, deadline d)
        : ctx(h.multiplexer())
        , nativeh(h.native_handle())
        , d(d)
    {
    }
    virtual ~io_operation_connection() {}

  public:
    // Called by io_handle to immediately cause the setting of the output buffers
    // and invocation of the receiver with the result. Or sets a failure.
    // Used to skip the overhead of poll() where an i/o completed immediately,
    // and no pending i/o overhead is required.
    virtual void _complete_io(result<size_t> /*unused*/) noexcept { abort(); }
    // Called by anyone to cancel any started i/o
    virtual void cancel() noexcept { abort(); }
    // Called by anyone to cause the checking for i/o completion or
    // timed out, and if so, to complete the i/o
    virtual status_type poll(deadline /*unused*/ = std::chrono::seconds(0)) noexcept { abort(); }
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
  };
  struct fake_mutex
  {
    void lock() {}
    void unlock() {}
  };
  LLFIO_HEADERS_ONLY_FUNC_SPEC error_info ntkernel_error_from_overlapped(size_t);

  template <class HandleType, bool use_atomic> struct io_sender : public io_operation_connection
  {
    using handle_type = HandleType;
    static constexpr bool is_atomic = use_atomic;
    using buffers_type = typename io_multiplexer::buffers_type;
    using request_type = typename io_multiplexer::io_request<buffers_type>;
    using result_type = typename io_multiplexer::io_result<buffers_type>;
    using error_type = typename result_type::error_type;
    using status_type = typename io_operation_connection::status_type;
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
    io_sender(handle_type &h, request_type req = {}, LLFIO_V2_NAMESPACE::deadline d = LLFIO_V2_NAMESPACE::deadline())
        : io_operation_connection(h, d)
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
        // destruct ret
        storage.ret.~result_type();
        break;
      }
    }

  public:
    //! Return the associated multiplexer
    io_multiplexer *multiplexer() const noexcept { return this->ctx; }
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
      if(this->is_registered_with_io_multiplexer)
      {
        this->ctx->_deregister_pending_io(this);
      }
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
          // It seems the NT kernel is guilty of casting bugs sometimes
          size_t internal = ols[n].Internal & 0xffffffff;
          if(internal != 0)
          {
            storage.req.~request_type();
            new(&storage.ret) result_type(ntkernel_error_from_overlapped(internal));
            status.store(status_type::completed, std::memory_order_release);
            return;
          }
          storage.req.buffers[n] = {storage.req.buffers[n].data(), ols[n].InternalHigh};
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
LLFIO_TEMPLATE(class HandleType, bool use_atomic = false)
LLFIO_TREQUIRES(LLFIO_TPRED(std::is_base_of<io_handle, HandleType>::value))
struct async_read : protected detail::io_sender<HandleType, use_atomic>
{
  using detail::io_sender<HandleType, use_atomic>::io_sender;
  using detail::io_sender<HandleType, use_atomic>::request;
  using detail::io_sender<HandleType, use_atomic>::deadline;

protected:
  void _begin_io() noexcept
  {
    // Begin a read
    HandleType temp(this->nativeh, 0, 0);
    temp._begin_read(this, this->storage.req);
    temp.release();
  }
  void _cancel_io() noexcept
  {
    // Cancel a read
    HandleType temp(this->nativeh, 0, 0);
    temp._cancel_read(this, this->storage.req);
    temp.release();
  }
};
//! \brief A Sender of an atomic async read i/o on a handle
template <class HandleType> using atomic_async_read = async_read<HandleType, true>;

//! \brief A Sender of a non-atomic async write i/o on a handle
LLFIO_TEMPLATE(class HandleType, bool use_atomic = false)
LLFIO_TREQUIRES(LLFIO_TPRED(std::is_base_of<io_handle, HandleType>::value))
struct async_write : protected detail::io_sender<HandleType, use_atomic>
{
  using detail::io_sender<HandleType, use_atomic>::io_sender;
  using detail::io_sender<HandleType, use_atomic>::request;
  using detail::io_sender<HandleType, use_atomic>::deadline;

protected:
  void _begin_io() noexcept
  {
    // Begin a write
    HandleType temp(this->nativeh, 0, 0);
    temp._begin_write(this, this->storage.req);
    temp.release();
  }
  void _cancel_io() noexcept
  {
    // Cancel a write
    HandleType temp(this->nativeh, 0, 0);
    temp._cancel_write(this, this->storage.req);
    temp.release();
  }
};
//! \brief A Sender of an atomic async write i/o on a handle
template <class HandleType> using atomic_async_write = async_write<HandleType, true>;

//! \brief A Sender of a non-atomic async barrier i/o on a handle
LLFIO_TEMPLATE(class HandleType, bool use_atomic = false)
LLFIO_TREQUIRES(LLFIO_TPRED(std::is_base_of<io_handle, HandleType>::value))
class async_barrier : protected detail::io_sender<HandleType, use_atomic>
{
  using _base = detail::io_sender<HandleType, use_atomic>;
  using request_type = typename _base::request_type;
  using status_type = typename _base::status_type;
  using barrier_kind = typename io_multiplexer::barrier_kind;
  barrier_kind _kind;

public:
  using _base::deadline;
  using _base::io_sender;
  using _base::request;

  //! Access the barrier kind to be made when started
  barrier_kind &kind() noexcept
  {
    assert(this->status.load(std::memory_order_acquire) == status_type::unscheduled);
    return _kind;
  }
  //! Access the barrier kind to be made when started
  const barrier_kind &kind() const noexcept
  {
    assert(this->status.load(std::memory_order_acquire) == status_type::unscheduled);
    return _kind;
  }

  explicit async_barrier(handle &h, request_type req = {}, barrier_kind kind = barrier_kind::nowait_data_only, LLFIO_V2_NAMESPACE::deadline d = LLFIO_V2_NAMESPACE::deadline())
      : _base(h, req, d)
      , _kind(kind)
  {
  }

protected:
  void _begin_io() noexcept
  {
    // Begin a barrier
    HandleType temp(this->nativeh, 0, 0);
    temp._begin_barrier(this, this->storage.req, _kind);
    temp.release();
  }
  void _cancel_io() noexcept
  {
    // Cancel a barrier
    HandleType temp(this->nativeh, 0, 0);
    temp._cancel_barrier(this, this->storage.req, _kind);
    temp.release();
  }
};
//! \brief A Sender of an atomic async barrier i/o on a handle
template <class HandleType> using atomic_async_barrier = async_barrier<HandleType, true>;

/*! \brief The i/o operation connection state type.

\warning Once started, you cannot change the address of this object until the
i/o completes. Doing so will terminate the process.
*/
template <class Sender, class Receiver>                                          //
LLFIO_REQUIRES(std::is_base_of<detail::io_operation_connection, Sender>::value)  //
class io_operation_connection final : protected Sender
{
  static_assert(std::is_base_of<detail::io_operation_connection, Sender>::value, "Sender type is not an i/o sender type");

public:
  using sender_type = Sender;
  using receiver_type = Receiver;
  using handle_type = typename sender_type::handle_type;
  static constexpr bool is_atomic = sender_type::is_atomic;
  using request_type = typename sender_type::request_type;
  using result_type = typename sender_type::result_type;
  using error_type = typename sender_type::error_type;

private:
  using _status_type = typename sender_type::status_type;
  using _lock_guard = typename sender_type::lock_guard;
  Receiver _receiver;

  virtual void _complete_io(result<size_t> bytes_transferred) noexcept override final
  {
    if(this->status.load(std::memory_order_acquire) != _status_type::scheduled)
    {
      abort();
    }
    sender_type::_create_result(std::move(bytes_transferred));
    // Set success or failure
    _receiver.set_value(std::move(this->storage.ret));
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
      : Sender(std::move(*this))
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
  ~io_operation_connection() { cancel(); }

  using Sender::completed;
  using Sender::multiplexer;
  using Sender::started;
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
    // If this i/o is timed, begin the timeout from now
    if(this->d)
    {
      if(this->d.steady)
      {
        this->deadline_duration = std::chrono::steady_clock::now() + std::chrono::nanoseconds(this->d.nsecs);
      }
      else
      {
        this->deadline_absolute = this->d.to_time_point();
      }
    }
    this->status.store(_status_type::scheduled, std::memory_order_release);
    // Ask the Sender to begin to i/o
    this->_begin_io();
  }
  //! \brief Cancel the operation, if it is scheduled.
  virtual void cancel() noexcept override final
  {
    if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      _lock_guard g(this->lock);
      if(this->status.load(std::memory_order_acquire) == _status_type::scheduled)
      {
        this->_cancel_io();
        sender_type::_create_result(errc::operation_canceled);
        // Set cancelled
        _receiver.set_done();
      }
    }
  }
  /*! \brief Poll the operation, executing completion if newly completed, returning
  immediately by default.
  
  Note that if the operation has a deadline, the current time will be fetched, which will
  involve a syscall.

  If the deadline is not zero, a loop of calling the associated multiplexer's `.wait()`
  function is performed, not returning until either the time out expires, or the i/o
  completes.
  */
  virtual _status_type poll(deadline d = deadline(std::chrono::seconds(0))) noexcept override final
  {
    _status_type ret = this->status.load(std::memory_order_acquire);
    if(ret == _status_type::scheduled)
    {
      LLFIO_DEADLINE_TO_SLEEP_INIT(d);
      do
      {
        {
          _lock_guard g(this->lock);
          ret = this->status.load(std::memory_order_acquire);  // check after lock grant
          if(ret != _status_type::scheduled)
          {
            break;
          }
#ifdef _WIN32
          // The OVERLAPPED structures can complete asynchronously. If they
          // have completed, complete i/o right now.
          bool asynchronously_completed = true;
          for(size_t n = 0; n < this->storage.req.buffers.size(); n++)
          {
            if(this->ols[n].Internal == (size_t) -1)
            {
              asynchronously_completed = false;
              break;
            }
          }
          if(asynchronously_completed)
          {
            _complete_io(result<size_t>(0));
            return _status_type::completed;
          }
#endif
          // Have I timed out?
          if(this->deadline_absolute != std::chrono::system_clock::time_point())
          {
            if(std::chrono::system_clock::now() >= this->deadline_absolute)
            {
              this->_cancel_io();
              _complete_io(errc::timed_out);
              return _status_type::completed;
            }
          }
          else if(this->deadline_duration != std::chrono::steady_clock::time_point())
          {
            if(std::chrono::steady_clock::now() >= this->deadline_duration)
            {
              this->_cancel_io();
              _complete_io(errc::timed_out);
              return _status_type::completed;
            }
          }
          if(d && d.nsecs == 0)
          {
            break;
          }
        }
        deadline nd;
        LLFIO_DEADLINE_TO_PARTIAL_DEADLINE(nd, d);
        auto r = this->ctx->wait(nd);
        if(!r)
        {
          char buffer[256];
          snprintf(buffer, sizeof(buffer), "io_operation_connection::poll() fails to blocking wait due to %s", r.error().message().c_str());
          LLFIO_LOG_WARN(nullptr, buffer);
        }
      } while(ret == _status_type::scheduled);
    }
    return ret;
  }
  //! \overload Convenience overload for `.poll()`
  template <class Rep, class Period> _status_type poll_for(const std::chrono::duration<Rep, Period> &duration) noexcept { return poll(duration); }
  //! \overload Convenience overload for `.poll()`
  template <class Clock, class Duration> _status_type poll_until(const std::chrono::time_point<Clock, Duration> &timeout) noexcept { return poll(timeout); }
  //! \overload Convenience overload for `.poll()`
  _status_type poll_until_ready() noexcept { return poll({}); }
};

//! \brief Connect an `async_read` Sender with a Receiver
LLFIO_TEMPLATE(class HandleType, bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                                              //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename HandleType::template io_result<typename HandleType::buffers_type>>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_read<HandleType, use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_read<HandleType, use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

//! \brief Connect an `async_write` Sender with a Receiver
LLFIO_TEMPLATE(class HandleType, bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                                                    //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename HandleType::template io_result<typename HandleType::const_buffers_type>>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_write<HandleType, use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_write<HandleType, use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

//! \brief Connect an `async_barrier` Sender with a Receiver
LLFIO_TEMPLATE(class HandleType, bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                                                    //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename HandleType::template io_result<typename HandleType::const_buffers_type>>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_barrier<HandleType, use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_barrier<HandleType, use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

#if 0  // disabled until we reimplement the awaitables around Sender-Receiver
//! \brief The promise type for an i/o awaitable
template <class Awaitable, bool use_atomic> struct io_awaitable_promise_type
{
  using awaitable_type = Awaitable;
  using container_type = typename io_multiplexer::template io_result<typename Awaitable::container_type>;
  using result_set_type = std::conditional_t<use_atomic, std::atomic<bool>, OUTCOME_V2_NAMESPACE::awaitables::detail::template fake_atomic<bool>>;
  // Constructor used by coroutines
  io_awaitable_promise_type() {}
  // Constructor used by co_read|co_write|co_barrier
  io_awaitable_promise_type(handle *_h, typename io_multiplexer::template io_request<typename Awaitable::container_type> _reqs)
      : ctx(_h->multiplexer())
      , nativeh(_h->native_handle())
      , reqs(_reqs)
  {
  }
  io_awaitable_promise_type(const io_awaitable_promise_type &) = delete;
  io_awaitable_promise_type(io_awaitable_promise_type &&o) noexcept
      : result_set(o.result_set.load(std::memory_order_relaxed))
      , extra_in_use(o.extra_in_use)
      , ctx(o.ctx)
      , internal_reference(o.internal_reference)
      , nativeh(o.nativeh)
      , reqs(o.reqs)
  {
    if(result_set.load(std::memory_order_acquire))
    {
      new(&result.value) container_type(static_cast<container_type &&>(o.result.value));
    }
    o.ctx = nullptr;
    if(1 == extra_in_use)
    {
#ifdef _WIN32
      memcpy(extra.ols, o.extra.ols, sizeof(extra.ols));
#else
      new(&extra.erased_op) function_ptr<container_type(io_awaitable_promise_type & p), 2 * sizeof(void *)>(std::move(o.extra.erased_op));
#endif
    }
  }
  io_awaitable_promise_type &operator=(const io_awaitable_promise_type &) = delete;
  io_awaitable_promise_type &operator=(io_awaitable_promise_type &&) = delete;
  ~io_awaitable_promise_type()
  {
    if(result_set.load(std::memory_order_acquire))
    {
      result.value.~container_type();
    }
#ifndef _WIN32
    if(1 == extra_in_use)
    {
      extra.erased_op.~function_ptr<container_type(io_awaitable_promise_type & p), 2 * sizeof(void *)>();
    }
#endif
  }
  auto get_return_object() { return Awaitable{this}; }
  void return_value(container_type &&value)
  {
    assert(!result_set.load(std::memory_order_acquire));
    if(result_set.load(std::memory_order_acquire))
    {
      result.value.~container_type();
    }
    new(&result.value) container_type(static_cast<container_type &&>(value));
    result_set.store(true, std::memory_order_release);
  }
  void return_value(const container_type &value)
  {
    assert(!result_set.load(std::memory_order_acquire));
    if(result_set.load(std::memory_order_acquire))
    {
      result.value.~container_type();
    }
    new(&result.value) container_type(value);
    result_set.store(true, std::memory_order_release);
  }
  void unhandled_exception()
  {
    assert(!result_set.load(std::memory_order_acquire));
    if(result_set.load(std::memory_order_acquire))
    {
      result.value.~container_type();
    }
#ifdef __cpp_exceptions
    auto e = std::current_exception();
    auto ec = OUTCOME_V2_NAMESPACE::awaitables::detail::error_from_exception(static_cast<decltype(e) &&>(e), {});
    // Try to set error code first
    if(!OUTCOME_V2_NAMESPACE::awaitables::detail::error_is_set(ec) || !OUTCOME_V2_NAMESPACE::awaitables::detail::try_set_error(ec, &result.value))
    {
      OUTCOME_V2_NAMESPACE::awaitables::detail::set_or_rethrow(e, &result.value);
    }
#else
    std::terminate();
#endif
    result_set.store(true, std::memory_order_release);
  }
  auto initial_suspend() noexcept
  {
    struct awaiter
    {
      bool await_ready() noexcept { return true; }
      void await_resume() noexcept {}
      void await_suspend(
#if LLFIO_ENABLE_COROUTINES
      coroutine_handle<> /*unused*/
#endif
      )
      {
      }
    };
    return awaiter{};
  }
  auto final_suspend()
  {
    struct awaiter
    {
      bool await_ready() noexcept { return true; }
      void await_resume() noexcept {}
      void await_suspend(
#if LLFIO_ENABLE_COROUTINES
      coroutine_handle<> /*unused*/
#endif
      )
      {
      }
    };
    return awaiter{};
  }
};

/*! \brief An i/o awaitable type, where the operation is attempted immediately,
and if it can be completed immediately without blocking then the awaitable is returned ready.
If it must block, the calling coroutine is suspended and the awaitable is returned not ready.
If one then awaits on the i/o awaitable, `multiplexer()->run()` is looped until the
i/o completes.
*/
template <class Cont, bool use_atomic> class OUTCOME_NODISCARD io_awaitable
{
  template <class Awaitable, bool use_atomic_> friend struct io_awaitable_promise_type;

public:
  using promise_type = io_awaitable_promise_type<io_awaitable, use_atomic>;

private:
  using container_type = Cont;
  union {
    OUTCOME_V2_NAMESPACE::detail::empty_type _default{};
    typename io_multiplexer::template io_result<container_type> _immediate_result;
  };
  promise_type *_p{nullptr};

public:
  io_awaitable(io_awaitable &&o) noexcept
      : _p(o._p)
  {
    o._p = nullptr;
    if(_p == nullptr)
    {
      new(&_immediate_result) typename io_multiplexer::template io_result<container_type>(static_cast<typename io_multiplexer::template io_result<container_type> &&>(o._immediate_result));
    }
  }
  io_awaitable(const io_awaitable &o) = delete;
  io_awaitable &operator=(io_awaitable &&) = delete;  // as per P1056
  io_awaitable &operator=(const io_awaitable &) = delete;
  ~io_awaitable()
  {
    if(_p == nullptr)
    {
      _immediate_result.~io_result<container_type>();
    }
    else
    {
      auto r = _p->ctx->_cancel_io(_p->internal_reference);
      if(!r)
      {
        abort();  // should never happen as promise already satisfied
      }
    }
  }

  // Construct an awaitable set later by its promise
  explicit io_awaitable(promise_type *p)
      : _p(p)
  {
  }
  // Construct an awaitable which has an immediate result
  LLFIO_TEMPLATE(class T)
  LLFIO_TREQUIRES(LLFIO_TPRED(std::is_constructible<typename io_multiplexer::template io_result<container_type>, T>::value))
  io_awaitable(T &&c)
      : _immediate_result(static_cast<T &&>(c))
  {
  }
  bool await_ready() noexcept { return _p == nullptr || _p->result_set.load(std::memory_order_acquire); }
  typename io_multiplexer::template io_result<container_type> await_resume()
  {
    if(_p == nullptr)
    {
      return static_cast<typename io_multiplexer::template io_result<container_type> &&>(_immediate_result);
    }
    assert(_p->result_set.load(std::memory_order_acquire));
    if(!_p->result_set.load(std::memory_order_acquire))
    {
      std::terminate();
    }
    // Release my promise as early as possible
    new(&_immediate_result) typename io_multiplexer::template io_result<container_type>(static_cast<typename io_multiplexer::template io_result<container_type> &&>(_p->result.value));
    auto r = _p->ctx->_cancel_io(_p->internal_reference);
    if(!r)
    {
      abort();  // should never happen as promise already satisfied
    }
    _p = nullptr;
    return static_cast<typename io_multiplexer::template io_result<container_type> &&>(_immediate_result);
  }
  void await_suspend(
#if LLFIO_ENABLE_COROUTINES
  coroutine_handle<> /*unused*/
#endif
  )
  {
    // Pump the i/o context until my promise gets set
    do
    {
      auto r = _p->ctx->run();
      if(!r)
      {
        auto r2 = _p->ctx->_cancel_io(_p->internal_reference);
        if(!r2)
        {
          abort();
        }
        _p = nullptr;
        new(&_immediate_result) typename io_multiplexer::template io_result<container_type>(r.error());
      }
    } while(!await_ready());
  }
};
#endif

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
