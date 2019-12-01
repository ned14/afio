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
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
  template <class Promise = void> using _coroutine_handle = OUTCOME_V2_NAMESPACE::awaitables::coroutine_handle<Promise>;
  enum class _io_kind
  {
    unknown,
    read,
    write,
    barrier
  };
  struct _await_io_handle_ready_awaitable;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _await_io_handle_ready(_await_io_handle_ready_awaitable *a, _coroutine_handle<> co) noexcept = 0;
  struct _await_io_handle_ready_awaitable
  {
    io_context *ctx;
    handle *_h;
    void *_identifier;  // OVERLAPPED * on Windows, struct aiocb * on BSD, null otherwise
    _io_kind _kind;
    deadline _d;
    optional<result<void>> _ret;

    explicit _await_io_handle_ready_awaitable(io_context *_ctx, handle *h, void *identifier, _io_kind kind, deadline d)
        : ctx(_ctx)
        , _h(h)
        , _identifier(identifier)
        , _kind(kind)
        , _d(d)
    {
      assert(!_d || !_d.steady || 0 != _d.nsecs);
    }

    bool await_ready() { return false; }  // always suspend
    bool await_suspend(_coroutine_handle<> co)
    {
      _ret = ctx->_await_io_handle_ready(this, co);
      return false;  // immediately resume if it returns
    }
    result<void> await_resume() { return _ret.value(); }
  };
#endif

public:
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
