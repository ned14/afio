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

#include "outcome/coroutine_support.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

class io_handle;

#ifdef OUTCOME_FOUND_COROUTINE_HEADER
template <class Promise = void> using coroutine_handle = OUTCOME_V2_NAMESPACE::awaitables::coroutine_handle<Promise>;
template <class... Args> using coroutine_traits = OUTCOME_V2_NAMESPACE::awaitables::coroutine_traits<Args...>;
using OUTCOME_V2_NAMESPACE::awaitables::suspend_always;
using OUTCOME_V2_NAMESPACE::awaitables::suspend_never;

//! \brief The promise type for an i/o awaitable
template <class Awaitable, bool use_atomic> struct io_awaitable_promise_type
{
  using awaitable_type = Awaitable;
  using container_type = io_result<typename Awaitable::container_type>;
  using result_set_type = std::conditional_t<use_atomic, std::atomic<bool>, OUTCOME_V2_NAMESPACE::awaitables::detail::fake_atomic<bool>>;
  union {
    OUTCOME_V2_NAMESPACE::detail::empty_type _default{};
    container_type result;
  };
  result_set_type result_set{false};
  coroutine_handle<> continuation;
  native_handle_type nativeh;
  io_request<typename Awaitable::container_type> reqs{};
  deadline d{};

  // Constructor used by coroutines
  io_awaitable_promise_type() {}
  // Constructor used by co_read|co_write|co_barrier
  io_awaitable_promise_type(handle *_h, io_request<typename Awaitable::container_type> _reqs, deadline _d)
      : nativeh(_h->native_handle())
      , reqs(_reqs)
      , d(_d)
  {
  }
  io_awaitable_promise_type(const io_awaitable_promise_type &) = delete;
  io_awaitable_promise_type(io_awaitable_promise_type &&o) noexcept
      : result_set(o.result_set.load())
      , continuation(o.continuation)
  {
    if(result_set.load(std::memory_order_acquire))
    {
      new(&result) container_type(static_cast<container_type &&>(o.result));
    }
    o.continuation = {};
  }
  io_awaitable_promise_type &operator=(const io_awaitable_promise_type &) = delete;
  io_awaitable_promise_type &operator=(io_awaitable_promise_type &&) = delete;
  ~io_awaitable_promise_type()
  {
    if(result_set.load(std::memory_order_acquire))
    {
      result.~container_type();
    }
  }
  auto get_return_object() { return Awaitable{*this}; }
  void return_value(container_type &&value)
  {
    assert(!result_set.load(std::memory_order_acquire));
    if(result_set.load(std::memory_order_acquire))
    {
      result.~container_type();
    }
    new(&result) container_type(static_cast<container_type &&>(value));
    result_set.store(true, std::memory_order_release);
  }
  void return_value(const container_type &value)
  {
    assert(!result_set.load(std::memory_order_acquire));
    if(result_set.load(std::memory_order_acquire))
    {
      result.~container_type();
    }
    new(&result) container_type(value);
    result_set.store(true, std::memory_order_release);
  }
  void unhandled_exception()
  {
    assert(!result_set.load(std::memory_order_acquire));
    if(result_set.load(std::memory_order_acquire))
    {
      result.~container_type();
    }
#ifdef __cpp_exceptions
    auto e = std::current_exception();
    auto ec = detail::error_from_exception(static_cast<decltype(e) &&>(e), {});
    // Try to set error code first
    if(!detail::error_is_set(ec) || !detail::try_set_error(ec, &result))
    {
      detail::set_or_rethrow(e, &result);
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
      void await_suspend(coroutine_handle<> /*unused*/) {}
    };
    return awaiter{};
  }
  auto final_suspend()
  {
    struct awaiter
    {
      bool await_ready() noexcept { return false; }
      void await_resume() noexcept {}
      void await_suspend(coroutine_handle<io_awaitable_promise_type> self)
      {
        if(self.promise().continuation)
        {
          return self.promise().continuation.resume();
        }
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
  using container_type = Cont;
  using promise_type = outcome_promise_type<io_awaitable, use_atomic>;
  union {
    OUTCOME_V2_NAMESPACE::detail::empty_type _default{};
    io_result<container_type> _immediate_result;
  };
  coroutine_handle<promise_type> _h;

public:
  io_awaitable(io_awaitable &&o) noexcept
      : _h(static_cast<coroutine_handle<promise_type> &&>(o._h))
  {
    o._h = nullptr;
    if(!_h)
    {
      new(&_immediate_result) container_type(static_cast<io_result<container_type> &&>(o._immediate_result));
    }
  }
  io_awaitable(const io_awaitable &o) = delete;
  io_awaitable &operator=(io_awaitable &&) = delete;  // as per P1056
  io_awaitable &operator=(const io_awaitable &) = delete;
  ~io_awaitable()
  {
    if(_h)
    {
      _h.destroy();
    }
    else
    {
      _immediate_result.~io_result<container_type>();
    }
  }

  // Construct an awaitable set later by its promise
  explicit io_awaitable(promise_type &p)
      : _h(coroutine_handle<promise_type>::from_promise(p))
  {
  }
  // Construct an awaitable which has an immediate result
  io_awaitable(io_result<container_type> &&c)
      : _immediate_result(static_cast<io_result<container_type> &&>(c))
  {
  }
  bool await_ready() noexcept { return !_h || _h.promise().result_set.load(std::memory_order_acquire); }
  io_result<container_type> await_resume()
  {
    if(!_h)
    {
      return static_cast<typename io_result<container_type> &&>(_immediate_result);
    }
    assert(_h.promise().result_set.load(std::memory_order_acquire));
    if(!_h.promise().result_set.load(std::memory_order_acquire))
    {
      std::terminate();
    }
    return static_cast<typename io_result<container_type> &&>(_h.promise().result);
  }
  void await_suspend(coroutine_handle<> cont)
  {
    _h.promise().continuation = cont;
    _h.resume();
  }
};
#endif

/*! \class io_context
\brief An i/o multiplexer context.

This i/o context is used in conjunction with `io_handle` to multiplex
i/o on a single kernel thread. An `io_handle` may use its own i/o
context set using its `.set_multiplexer()`, or else it will use the
current thread's i/o context which is set using `this_thread::set_multiplexer()`.
If never set, `this_thread::multiplexer()` will upon first call create
an i/o context suitable for the current platform using `io_context::best_available(1)`,
so in general you can simply start multiplexing i/o immediately without
having to do any setup, and everything should "just work".

For all i/o context implementations, `.post()` is guaranteed to be
threadsafe. You can use this to post work from other kernel threads
to be executed by the i/o context on its kernel thread as soon as
possible.

## Available implementations

There are multiple i/o context implementations available, each with
varying tradeoffs. Some of the implementations take a `threads`
parameter. If not `1`, the implementation returned can handle more
than one thread using the same instance at a time. In this situation,
i/o completions are delivered to the next available idle thread
calling `.run()`.

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
ready for reads. You can perform up to 64 concurrent i/o's per handle
in this implementation before an error code comparing equal to
`errc::resource_unavailable_try_again` will be returned.

Note that this implementation must perform a dynamic memory allocation per i/o
if an i/o with a non-zero non-infinite deadline blocks. This is because
a global ordered list of deadlines must be kept for `epoll()` to efficiently
detect timeouts.

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

Note that support for asynchronous file i/o is not currently
implemented for no good reason other than lack of time. Only
non-seekable devices are currently supported.

\snippet coroutines.cpp coroutines_example
*/
class LLFIO_DECL io_context
{
  friend class io_handle;

protected:
  constexpr io_context() {}

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _register_io_handle(handle *h) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _deregister_io_handle(handle *h) noexcept = 0;

public:
  using path_type = handle::path_type;
  using extent_type = handle::extent_type;
  using size_type = handle::size_type;
  using mode = handle::mode;
  using creation = handle::creation;
  using caching = handle::caching;
  using flag = handle::flag;

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

  /*! Checks if any items have been posted, or if any i/o scheduled has completed, if so
  either executes the posted item, or resumes any coroutines suspended pending the
  completion of the i/o. Returns true if more work remains and we just handled an i/o or post;
  false if there is no more work; `errc::timed_out` if the deadline passed.

  \mallocs None in the usual case; if any items posted were executed, there is a dynamic
  memory free and a mutex lock/unlock cycle. If the i/o context is threadsafe, there
  are multiple mutex lock/unlock cycles.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<bool> run(deadline d = deadline()) noexcept = 0;
  LLFIO_DEADLINE_TRY_FOR_UNTIL(run)

private:
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _post(detail::function_ptr<void *(void *)> &&f) noexcept = 0;

public:
  /*! Schedule the callable to be invoked by the thread owning this object and executing `run()` at its next
  available opportunity. Unlike any other function in this API layer, this function is thread safe.

  \mallocs At least one dynamic memory allocation to type erase the callable, and a mutex
  lock/unlock cycle.
  */
  template <class U> result<void> post(U &&f) noexcept
  {
    try
    {
      struct _
      {
        U f;
        detail::function_ptr<void(void *)> next;
        void *operator()(void *n)
        {
          if(n != nullptr)
          {
            if(!next)
            {
              next = std::move(*reinterpret_cast<detail::function_ptr<void(void *)> *>(n));
            }
            return &next;
          }
          f();
          return &next;
        }
      };
      _post(detail::make_function_ptr<void *(void *)>(_{std::forward<U>(f), detail::function_ptr<void(void *)>()}));
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

#ifdef OUTCOME_FOUND_COROUTINE_HEADER
  struct co_post_self_to_context_awaitable
  {
    io_context *ctx;

    explicit co_post_self_to_context_awaitable(io_context *_ctx)
        : ctx(_ctx)
    {
    }

    bool await_ready() { return false; }
    void await_suspend(coroutine_handle<> co)
    {
      ctx->post([co = co]() mutable { co.resume(); }).value();
    }
    void await_resume() {}
  };
  /*! \brief Return aAn awaitable suspending execution of this coroutine on the current kernel thread,
  and resuming execution on the kernel thread running this i/o service. This is a
  convenience wrapper for `.post()`.
  */
  co_post_self_to_context_awaitable co_post_self_to_context() { return co_post_self_to_context_awaitable(this); }

protected:
  template <bool use_atomic> using _co_read_awaitable = io_awaitable<buffers_type, use_atomic>;
  template <bool use_atomic> using _co_write_awaitable = io_awaitable<const_buffers_type, use_atomic>;
  template <bool use_atomic> using _co_barrier_awaitable = io_awaitable<const_buffers_type, use_atomic>;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC _co_read_awaitable<false> _run_until_read_ready(_co_read_awaitable<false>::promise_type &&p) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC _co_write_awaitable<false> _run_until_write_ready(_co_write_awaitable<false>::promise_type &&p) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC _co_barrier_awaitable<false> _run_until_barrier_ready(_co_barrier_awaitable<false>::promise_type &&p) noexcept = 0;
#endif
};

//! \brief Thread local settings
namespace this_thread
{
  //! \brief Return the calling thread's i/o context.
  LLFIO_HEADERS_ONLY_FUNC_SPEC io_context *multiplexer() noexcept;
  //! \brief Set the calling thread's i/o context.
  LLFIO_HEADERS_ONLY_FUNC_SPEC void set_multiplexer(io_context *ctx) noexcept;
}  // namespace this_thread

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
