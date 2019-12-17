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

#ifndef LLFIO_IO_CONTEXT_H
#define LLFIO_IO_CONTEXT_H

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

/*! \class io_context
\brief An i/o multiplexer context.

This i/o context is used in conjunction with `io_handle` to multiplex
i/o on a single kernel thread. An `io_handle` may use its own i/o
context set using its `.set_multiplexer()`, or else it will use the
current thread's i/o context which is set using `this_thread::set_multiplexer()`.
If never set, `this_thread::multiplexer()` will upon first call create
an i/o context suitable for the current platform using `io_context::best_available(1)`
and retain it in thread local storage, so in general you can simply
start multiplexing i/o immediately without having to do any setup, and
everything should "just work".

For all i/o context implementations, `.post()` is guaranteed to be
threadsafe. You can use this to post work from other kernel threads
to be executed by the i/o context on its kernel thread as soon as
possible.

For most i/o context implementations, no dynamic memory allocations
occur during i/o. If `threads` is less than or equal to `1`, no
locking occurs during i/o either unless items from `.post()` were
processed.

## Available implementations

There are multiple i/o context implementations available, each with
varying tradeoffs. Some of the implementations take a `threads`
parameter. If not `1`, the implementation returned can handle more
than one thread using the same instance at a time. In this situation,
i/o completions are delivered to the next available idle thread
blocked within `.run()`.

`.run()` returns when i/o completions were processed. You should
probably therefore run it within a loop. For improved efficiency,
some of the `.run()` implementations will process many completions
at a time, and will return the number it processed.

### Linux

- `io_context::linux_epoll(size_t threads)` returns a Linux `epoll()`
based i/o context implementation. If `threads` is 1, the implementation
returned cannot be used by multiple threads (apart from `.post()`).
Note that Linux kernel 4.5 or later is required for `threads` > 1 to
successfully instantiate.

Because Linux `epoll()` has no way of efficiently identifying
which specific i/o to wait for (and `epoll()` doesn't work on seekable
devices in any case), the implementation works on a FIFO basis i.e.
if forty coroutines do forty reads on the same i/o handle, the
earliest read is the first one resumed when the i/o handle signals
ready for reads. You can perform up to `maximum_pending_io()` concurrent
i/o's per handle in this implementation before an error code comparing
equal to `errc::resource_unavailable_try_again` will be returned.

Note that this implementation must perform a dynamic memory allocation per i/o
if an i/o with a non-zero non-infinite deadline blocks. This is because
a global ordered list of deadlines must be kept for `epoll()` to efficiently
detect timeouts. This implementation is only non-allocating if you use
either zero or infinite deadlines.

- `io_context::linux_io_uring()` returns a Linux io_uring based i/o
context implementation. As Linux io_uring is fundamentally a single
threaded kernel interface, multiple threads is not supported. Only
available on Linux kernel 5.1 or later.
`io_context::best_available()` chooses this if your kernel is new
enough to support io_uring and `threads` is 1, otherwise the `epoll()`
implementation is chosen.

Linux io_uring is a very well designed kernel syscall interface with
very high efficiency and no built in limits to scalability. This
implementation uses a 16Kb submission buffer and a 4Kb completion
buffer which means up to 256 i/o's can be in flight per i/o context
instance at a time. If you need more than this for a single kernel
thread, simply create multiple i/o contexts, and distribute your i/o
between them.

Note that support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

### Mac OS and FreeBSD

- `io_context::bsd_kqueue(size_t threads)` returns a BSD kqueues based
i/o context implementation. If `threads` is 1, the implementation
returned cannot be used by multiple threads (apart from `.post()`).

Note that FreeBSD's support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

### Windows

- `io_context::win_iocp(size_t threads)` returns a Windows IOCP based
i/o context implementation. If `threads` is 1, the implementation
returned cannot be used by multiple threads (apart from `.post()`).

Be aware that because Windows does not have scatter-gather i/o support
for some kinds of handle, on those kinds of handle a whole i/o is
issued per buffer. This can cause `maximum_pending_io()` to be reached
earlier than on other platforms.

Note that this implementation must perform a dynamic memory allocation per i/o
if an i/o with a non-zero non-infinite deadline blocks. This is because
a global ordered list of deadlines must be kept for IOCP to efficiently
detect timeouts. This implementation is only non-allocating if you use
either zero or infinite deadlines.

Note that support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

\snippet coroutines.cpp coroutines_example
*/
class LLFIO_DECL io_context
{
  friend class io_handle;
  template <class Cont, bool use_atomic> friend class io_awaitable;

protected:
  size_t _maximum_pending_io{64};
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
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> best_available(size_t threads) noexcept;
#ifdef __linux__
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> linux_epoll(size_t threads) noexcept;
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> linux_io_uring() noexcept;
#elif defined(__FreeBSD__) || defined(__APPLE__)
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> bsd_kqueue(size_t threads) noexcept;
#elif defined(_WIN32)
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_context>> win_iocp(size_t threads) noexcept;
#else
#error Unknown platform
#endif

  io_context(io_context &&) = delete;
  io_context(const io_context &) = delete;
  io_context &operator=(io_context &&) = delete;
  io_context &operator=(const io_context &) = delete;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~io_context() {}

  //! \brief Returns the maximum number of inflight i/o there can be.
  size_t maximum_pending_io() const noexcept { return _maximum_pending_io; }

  /*! \brief Sets the maximum number of inflight i/o there can be. This is the number of
  i/o either pending completion, or completed but whose result has not yet been retrieved.
  Setting it to zero sets the default for this i/o context implementation, which for all
  i/o contexts implemented by LLFIO is 64.
  */
  void set_maximum_pending_io(size_t no) noexcept { _maximum_pending_io = (0 == no) ? 64 : no; }

  //! The native handle used by this i/o context
  native_handle_type native_handle() const noexcept { return _v; }

  /*! \brief Checks if any items have been posted, or if any i/o scheduled has completed, if so
  either executes the posted item, or resumes any coroutines suspended pending the
  completion of the i/o. Returns the number of items or i/o completed; `errc::timed_out`
  if the deadline passed.

  \mallocs None in the usual case; if any items posted were executed, there may be a dynamic
  memory free and a mutex lock/unlock cycle. If the i/o context is threadsafe, there
  are multiple mutex lock/unlock cycles.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<size_t> run(deadline d = deadline()) noexcept = 0;
  LLFIO_DEADLINE_TRY_FOR_UNTIL(run)

  /*! \brief Cancels the previously begun i/o indicated, invoking the Receiver's done
  implementation.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void cancel_io(detail::io_operation_connection *op) noexcept = 0;

protected:
  constexpr io_context() {}

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _post(function_ptr<void *(void *)> &&f) noexcept = 0;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _register_io_handle(handle *h) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _deregister_io_handle(handle *h) noexcept = 0;
#if 0
  template <bool use_atomic> using _co_read_awaitable = io_awaitable<buffers_type, use_atomic>;
  template <bool use_atomic> using _co_write_awaitable = io_awaitable<const_buffers_type, use_atomic>;
  template <bool use_atomic> using _co_barrier_awaitable = io_awaitable<const_buffers_type, use_atomic>;

  template <bool use_atomic> using _co_read_awaitable_promise_type = io_awaitable_promise_type<_co_read_awaitable<use_atomic>, use_atomic>;
  template <bool use_atomic> using _co_write_awaitable_promise_type = io_awaitable_promise_type<_co_write_awaitable<use_atomic>, use_atomic>;
  template <bool use_atomic> using _co_barrier_awaitable_promise_type = io_awaitable_promise_type<_co_barrier_awaitable<use_atomic>, use_atomic>;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC _co_read_awaitable<false> _submit_read(_co_read_awaitable_promise_type<false> &&p, deadline d) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC _co_write_awaitable<false> _submit_write(_co_write_awaitable_promise_type<false> &&p, deadline d) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC _co_barrier_awaitable<false> _submit_barrier(_co_barrier_awaitable_promise_type<false> &&p, deadline d) noexcept = 0;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _cancel_io(void *p) noexcept = 0;
#endif

public:
  // Called by io_handle::_begin_X().
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _begin_io(detail::io_operation_connection *op) noexcept = 0;

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
    io_context *ctx;

    explicit co_post_self_to_context_awaitable(io_context *_ctx)
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
  //! \brief Return the calling thread's i/o context.
  LLFIO_HEADERS_ONLY_FUNC_SPEC io_context *multiplexer() noexcept;
  //! \brief Set the calling thread's i/o context.
  LLFIO_HEADERS_ONLY_FUNC_SPEC void set_multiplexer(io_context *ctx) noexcept;
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
      size_t Internal;
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
    io_context *ctx;
    native_handle_type nativeh;
    deadline d;

    io_operation_connection(handle &h, deadline d)
        : ctx(h.multiplexer())
        , nativeh(h.native_handle())
        , d(d)
    {
    }
    virtual ~io_operation_connection() {}

    // Returns the current status of the operation
    virtual status_type _status() const noexcept = 0;
    // Called by the i/o context to cause the setting of the output buffers
    // and invocation of the receiver with the result. Or sets a failure.
    virtual void _complete_io(result<size_t> /*unused*/) noexcept { abort(); }
    // Called by the i/o context to cause the cancellation of the receiver
    virtual void _cancel_io() noexcept { abort(); }
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

  template <class HandleType, bool use_atomic> struct io_sender : public io_operation_connection
  {
    using handle_type = HandleType;
    static constexpr bool is_atomic = use_atomic;
    using buffers_type = typename io_context::buffers_type;
    using request_type = typename io_context::io_request<buffers_type>;
    using result_type = typename io_context::io_result<buffers_type>;
    using error_type = typename result_type::error_type;
    using status_type = typename io_operation_connection::status_type;
    using result_set_type = std::conditional_t<is_atomic, std::atomic<status_type>, fake_atomic<status_type>>;

    result_set_type status{status_type::unscheduled};
    union storage_t {
      request_type req;
      result_type ret;
      storage_t(request_type _req)
          : req(std::move(_req))
      {
      }
      ~storage_t() {}
    } storage;

    io_sender(handle_type &h, request_type req = {}, LLFIO_V2_NAMESPACE::deadline d = LLFIO_V2_NAMESPACE::deadline())
        : io_operation_connection(h, d)
        , storage(req)
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
        // Cancel the i/o
        ctx->cancel_io(this);
        if(status.load(std::memory_order_acquire) == status_type::completed)
        {
          // destruct ret
          storage.ret.~result_type();
        }
        else
        {
          // destruct req
          storage.req.~request_type();
        }
        break;
      case status_type::completed:
        // destruct ret
        storage.ret.~result_type();
        break;
      }
    }

    //! True if started
    bool started() const noexcept { return status.load(std::memory_order_acquire) != status_type::unscheduled; }
    //! True if completed
    bool completed() const noexcept { return status.load(std::memory_order_acquire) == status_type::completed; }
    //! Access the request to be made when started
    request_type &request() noexcept
    {
      assert(status.load(std::memory_order_acquire) == status_type::unscheduled);
      return storage.req;
    }
    //! Access the request to be made when started
    const request_type &request() const noexcept
    {
      assert(status.load(std::memory_order_acquire) == status_type::unscheduled);
      return storage.req;
    }
    //! Access the deadline to be used when started
    LLFIO_V2_NAMESPACE::deadline &deadline() noexcept
    {
      assert(status.load(std::memory_order_acquire) == status_type::unscheduled);
      return this->d;
    }
    //! Access the deadline to be used when started
    const LLFIO_V2_NAMESPACE::deadline &deadline() const noexcept
    {
      assert(status.load(std::memory_order_acquire) == status_type::unscheduled);
      return this->d;
    }

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
      size_t bytes_transferred = toset.value();
      buffers_type ret(storage.req.buffers);
      // Set each individual buffer filled
      for(size_t i = 0; i < ret.size(); i++)
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
      // Set the result
      storage.req.~request_type();
      new(&storage.ret) result_type(std::move(ret));
      status.store(status_type::completed, std::memory_order_release);
    }
    virtual status_type _status() const noexcept override final { return status.load(std::memory_order_acquire); }
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
    HandleType::_begin_read(this, this->storage.req);
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
    HandleType::_begin_write(this, this->storage.req);
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
  using barrier_kind = typename io_context::barrier_kind;
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
    HandleType::_begin_barrier(this, this->storage.req, _kind);
  }
};
//! \brief A Sender of an atomic async barrier i/o on a handle
template <class HandleType> using atomic_async_barrier = async_barrier<HandleType, true>;

//! \brief The i/o operation connection state type
template <class Sender, class Receiver> LLFIO_REQUIRES(std::is_base_of<detail::io_operation_connection, Sender>::value) class io_operation_connection final : protected Sender
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
  Receiver _receiver;

  virtual void _complete_io(result<size_t> bytes_transferred) noexcept override final
  {
    assert(this->status.load(std::memory_order_acquire) == _status_type::scheduled);
    if(this->status.load(std::memory_order_acquire) != _status_type::scheduled)
    {
      abort();
    }
    sender_type::_create_result(std::move(bytes_transferred));
    // Set success or failure
    _receiver.set_value(std::move(this->storage.ret));
    this->status.store(_status_type::completed, std::memory_order_release);
  }

  virtual void _cancel_io() noexcept override final
  {
    assert(this->status.load(std::memory_order_acquire) == _status_type::scheduled);
    if(this->status.load(std::memory_order_acquire) != _status_type::scheduled)
    {
      abort();
    }
    // Set cancelled
    _receiver.set_done();
    this->status.store(_status_type::completed, std::memory_order_release);
  }

public:
  //! \brief Use the `connect()` function in preference to using this directly
  template <class _Sender, class _Receiver>
  io_operation_connection(_Sender &&sender, _Receiver &&receiver)
      : Sender(std::forward<_Sender>(sender))
      , _receiver(std::forward<_Receiver>(receiver))
  {
  }
  io_operation_connection(const io_operation_connection &) = delete;
  io_operation_connection(io_operation_connection &&o) noexcept
      : Sender(std::move(*this))
      , _receiver(std::move(o._receiver))
  {
    assert(o.status.load(std::memory_order_acquire) != _status_type::scheduled);
    if(o.status.load(std::memory_order_acquire) == _status_type::scheduled)
    {
      abort();
    }
    this->status.store(o.status.load(std::memory_order_acquire), std::memory_order_release);
    o.status.store(_status_type::unknown, std::memory_order_release);
  }
  io_operation_connection &operator=(const io_operation_connection &) = delete;
  io_operation_connection &operator=(io_operation_connection &&o) noexcept
  {
    this->~io_operation_connection();
    new(this) io_operation_connection(std::move(o));
    return *this;
  }

  using Sender::completed;
  using Sender::started;
  //! \brief Access the sender
  sender_type &sender() noexcept { return *this; }
  //! \brief Access the sender
  const sender_type &sender() const noexcept { return *this; }
  //! \brief Access the receiver
  receiver_type &receiver() noexcept { return _receiver; }
  //! \brief Access the receiver
  const receiver_type &receiver() const noexcept { return _receiver; }

  /*! \brief Start the operation. Note that it may complete immediately, possibly
  with failure. Any relative deadline begins at this point.
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
  //! \brief Cancel the operation, if it is scheduled
  void cancel() noexcept { this->ctx->cancel_io(this); }
};

//! \brief Connect an async_read Sender with a Receiver
LLFIO_TEMPLATE(class HandleType, bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                            //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename async_read<HandleType, use_atomic>::result_type>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_read<HandleType, use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_read<HandleType, use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

//! \brief Connect an async_write Sender with a Receiver
LLFIO_TEMPLATE(class HandleType, bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                             //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename async_write<HandleType, use_atomic>::result_type>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_write<HandleType, use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_write<HandleType, use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

//! \brief Connect an async_barrier Sender with a Receiver
LLFIO_TEMPLATE(class HandleType, bool use_atomic, class Receiver)
LLFIO_TREQUIRES(                                                                                                               //
LLFIO_TEXPR(std::declval<Receiver>().set_value(std::declval<typename async_barrier<HandleType, use_atomic>::result_type>())),  //
LLFIO_TEXPR(std::declval<Receiver>().set_done()))
auto connect(async_barrier<HandleType, use_atomic> &&sender, Receiver &&receiver)
{
  return io_operation_connection<async_barrier<HandleType, use_atomic>, Receiver>(std::move(sender), std::forward<Receiver>(receiver));
}

#if 0
//! \brief The promise type for an i/o awaitable
template <class Awaitable, bool use_atomic> struct io_awaitable_promise_type
{
  using awaitable_type = Awaitable;
  using container_type = typename io_context::template io_result<typename Awaitable::container_type>;
  using result_set_type = std::conditional_t<use_atomic, std::atomic<bool>, OUTCOME_V2_NAMESPACE::awaitables::detail::template fake_atomic<bool>>;
  // Constructor used by coroutines
  io_awaitable_promise_type() {}
  // Constructor used by co_read|co_write|co_barrier
  io_awaitable_promise_type(handle *_h, typename io_context::template io_request<typename Awaitable::container_type> _reqs)
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
    typename io_context::template io_result<container_type> _immediate_result;
  };
  promise_type *_p{nullptr};

public:
  io_awaitable(io_awaitable &&o) noexcept
      : _p(o._p)
  {
    o._p = nullptr;
    if(_p == nullptr)
    {
      new(&_immediate_result) typename io_context::template io_result<container_type>(static_cast<typename io_context::template io_result<container_type> &&>(o._immediate_result));
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
  LLFIO_TREQUIRES(LLFIO_TPRED(std::is_constructible<typename io_context::template io_result<container_type>, T>::value))
  io_awaitable(T &&c)
      : _immediate_result(static_cast<T &&>(c))
  {
  }
  bool await_ready() noexcept { return _p == nullptr || _p->result_set.load(std::memory_order_acquire); }
  typename io_context::template io_result<container_type> await_resume()
  {
    if(_p == nullptr)
    {
      return static_cast<typename io_context::template io_result<container_type> &&>(_immediate_result);
    }
    assert(_p->result_set.load(std::memory_order_acquire));
    if(!_p->result_set.load(std::memory_order_acquire))
    {
      std::terminate();
    }
    // Release my promise as early as possible
    new(&_immediate_result) typename io_context::template io_result<container_type>(static_cast<typename io_context::template io_result<container_type> &&>(_p->result.value));
    auto r = _p->ctx->_cancel_io(_p->internal_reference);
    if(!r)
    {
      abort();  // should never happen as promise already satisfied
    }
    _p = nullptr;
    return static_cast<typename io_context::template io_result<container_type> &&>(_immediate_result);
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
        new(&_immediate_result) typename io_context::template io_result<container_type>(r.error());
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
#include "detail/impl/io_context.ipp"
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
