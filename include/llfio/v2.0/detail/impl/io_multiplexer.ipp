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

#include "../../io_multiplexer.hpp"

#include <mutex>

LLFIO_V2_NAMESPACE_BEGIN

namespace this_thread
{
  static LLFIO_THREAD_LOCAL io_multiplexer *_thread_multiplexer;
  LLFIO_HEADERS_ONLY_FUNC_SPEC io_multiplexer *multiplexer() noexcept
  {
    if(_thread_multiplexer == nullptr)
    {
      static auto _ = io_multiplexer::best_available(1).value();
      _thread_multiplexer = _.get();
    }
    return _thread_multiplexer;
  }
  LLFIO_HEADERS_ONLY_FUNC_SPEC void set_multiplexer(io_multiplexer *ctx) noexcept { _thread_multiplexer = ctx; }
}  // namespace this_thread

template <bool threadsafe> class io_multiplexer_impl : public io_multiplexer
{
  struct _fake_lock_guard
  {
    explicit _fake_lock_guard(std::mutex & /*unused*/) {}
    void lock() {}
    void unlock() {}
  };

protected:
  using _lock_guard = std::conditional_t<threadsafe, std::unique_lock<std::mutex>, _fake_lock_guard>;
  std::mutex _lock;
  std::atomic<bool> _nonzero_items_posted{false};
  function_ptr<void *(void *)> _items_posted, *_last_item_posted{nullptr};

  bool _execute_posted_items()
  {
    if(_nonzero_items_posted.load(std::memory_order_acquire))
    {
      function_ptr<void *(void *)> i, empty;
      {
        std::lock_guard<std::mutex> h(_lock);  // need real locking here
        i = std::move(_items_posted);          // Detach item from front
        _items_posted = std::move(*reinterpret_cast<function_ptr<void *(void *)> *>(i(&empty)));
        if(!_items_posted)
        {
          _last_item_posted = nullptr;
          _nonzero_items_posted.store(false, std::memory_order_release);
        }
      }
      // Execute the item
      i(nullptr);
      return true;
    }
    return false;
  }

public:
  // Lock should be held on entry!
  virtual ~io_multiplexer_impl()
  {
    if(_nonzero_items_posted)
    {
      function_ptr<void *(void *)> empty;
      while(_items_posted)
      {
        auto &next = *reinterpret_cast<function_ptr<void *(void *)> *>(_items_posted(&empty));
        _items_posted = std::move(next);
      }
    }
    _lock.unlock();
  }

  virtual void _post(function_ptr<void *(void *)> &&f) noexcept override
  {
    std::lock_guard<std::mutex> h(_lock);  // need real locking here
    if(_last_item_posted == nullptr)
    {
      _items_posted = std::move(f);
      _last_item_posted = &_items_posted;
      _nonzero_items_posted.store(true, std::memory_order_release);
      return;
    }
    // Store the new item into the most recently posted item
    _last_item_posted = reinterpret_cast<function_ptr<void *(void *)> *>((*_last_item_posted)(&f));
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> io_multiplexer::best_available(size_t threads) noexcept
{
#ifdef __linux__
  if(threads > 1)
  {
    return io_multiplexer::linux_epoll(threads);
  }
  auto r = io_multiplexer::linux_io_uring();
  if(r)
  {
    return r;
  }
  return io_multiplexer::linux_epoll(threads);
#elif defined(__FreeBSD__) || defined(__APPLE__)
  return io_multiplexer::bsd_kqueue(threads);
#elif defined(_WIN32)
  return io_multiplexer::win_iocp(threads);
#else
#error Unknown platform
  return errc::not_supported;
#endif
}

LLFIO_V2_NAMESPACE_END

#if defined(_WIN32)
#include "windows/io_multiplexer.ipp"
#else
#include "posix/io_multiplexer.ipp"
#endif
