/* Dynamic thread pool group
(C) 2020-2021 Niall Douglas <http://www.nedproductions.biz/> (9 commits)
File Created: Dec 2020


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

#include "../../dynamic_thread_pool_group.hpp"

#include "../../file_handle.hpp"
#include "../../statfs.hpp"

#include "quickcpplib/spinlock.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <iostream>

#ifndef LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
#if LLFIO_FORCE_USE_LIBDISPATCH
#include <dispatch/dispatch.h>
#define LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD 1
#else
#ifdef _WIN32
#include "windows/import.hpp"
#include <threadpoolapiset.h>
#else
#if __has_include(<dispatch/dispatch.h>)
#include <dispatch/dispatch.h>
#define LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD 1
#endif
#endif
#endif
#endif
#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
#if !defined(__linux__)
#error dynamic_thread_pool_group requires Grand Central Dispatch (libdispatch) on non-Linux POSIX.
#endif
#include <dirent.h> /* Defines DT_* constants */
#include <sys/syscall.h>

#include <condition_variable>
#include <thread>
#endif

#define LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING 0

/* NOTE that the Linux results are from a VM on the same machine as the Windows results,
so they are not directly comparable.

Linux 4Kb and 64Kb

Benchmarking asio ...
   For 1 work items got 38182.6 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 68664 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 87036.4 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 78702.2 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 51911.2 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 553964 SHA256 hashes/sec with 31 maximum concurrency.
   For 64 work items got 713844 SHA256 hashes/sec with 36 maximum concurrency.
   For 128 work items got 700172 SHA256 hashes/sec with 37 maximum concurrency.
   For 256 work items got 716099 SHA256 hashes/sec with 37 maximum concurrency.
   For 512 work items got 703323 SHA256 hashes/sec with 37 maximum concurrency.
   For 1024 work items got 722827 SHA256 hashes/sec with 38 maximum concurrency.

Benchmarking asio ...
   For 1 work items got 3917.88 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 7798.29 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 14395.2 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 23633.4 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 31771.1 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 57978 SHA256 hashes/sec with 32 maximum concurrency.
   For 64 work items got 66200.6 SHA256 hashes/sec with 64 maximum concurrency.
   For 128 work items got 65706.5 SHA256 hashes/sec with 64 maximum concurrency.
   For 256 work items got 65717.5 SHA256 hashes/sec with 64 maximum concurrency.
   For 512 work items got 65652.4 SHA256 hashes/sec with 64 maximum concurrency.
   For 1024 work items got 65580.3 SHA256 hashes/sec with 64 maximum concurrency.


Windows 4Kb and 64kB

Benchmarking asio ...
   For 1 work items got 51216.7 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 97691 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 184381 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 305270 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 520728 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 482729 SHA256 hashes/sec with 32 maximum concurrency.
   For 64 work items got 1.02629e+06 SHA256 hashes/sec with 64 maximum concurrency.
   For 128 work items got 1.01816e+06 SHA256 hashes/sec with 64 maximum concurrency.
   For 256 work items got 1.01672e+06 SHA256 hashes/sec with 64 maximum concurrency.
   For 512 work items got 1.01727e+06 SHA256 hashes/sec with 64 maximum concurrency.
   For 1024 work items got 1.01477e+06 SHA256 hashes/sec with 64 maximum concurrency.

Benchmarking asio ...
   For 1 work items got 4069.92 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 8099.1 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 16021.7 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 30275.2 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 40972.5 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 70919.2 SHA256 hashes/sec with 32 maximum concurrency.
   For 64 work items got 71917 SHA256 hashes/sec with 64 maximum concurrency.
   For 128 work items got 71111.8 SHA256 hashes/sec with 64 maximum concurrency.
   For 256 work items got 70963.5 SHA256 hashes/sec with 64 maximum concurrency.
   For 512 work items got 70956.3 SHA256 hashes/sec with 64 maximum concurrency.
   For 1024 work items got 70989.9 SHA256 hashes/sec with 64 maximum concurrency.
*/


/* Linux 4Kb and 64Kb libdispatch

Benchmarking llfio (Grand Central Dispatch) ...
   For 1 work items got 33942.7 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 91275.8 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 191446 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 325776 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 405282 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 408015 SHA256 hashes/sec with 31 maximum concurrency.
   For 64 work items got 412343 SHA256 hashes/sec with 32 maximum concurrency.
   For 128 work items got 450024 SHA256 hashes/sec with 41 maximum concurrency.
   For 256 work items got 477885 SHA256 hashes/sec with 46 maximum concurrency.
   For 512 work items got 531752 SHA256 hashes/sec with 48 maximum concurrency.
   For 1024 work items got 608181 SHA256 hashes/sec with 44 maximum concurrency.

Benchmarking llfio (Grand Central Dispatch) ...
   For 1 work items got 3977.21 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 7980.09 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 15075.6 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 24427.3 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 41858.7 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 64896.4 SHA256 hashes/sec with 32 maximum concurrency.
   For 64 work items got 65683.6 SHA256 hashes/sec with 34 maximum concurrency.
   For 128 work items got 65476.1 SHA256 hashes/sec with 35 maximum concurrency.
   For 256 work items got 65210.6 SHA256 hashes/sec with 36 maximum concurrency.
   For 512 work items got 65241.1 SHA256 hashes/sec with 36 maximum concurrency.
   For 1024 work items got 65205.3 SHA256 hashes/sec with 37 maximum concurrency.
*/

/* Linux 4Kb and 64Kb native

Benchmarking llfio (Linux native) ...
   For 1 work items got 65160.3 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 126586 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 246616 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 478938 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 529919 SHA256 hashes/sec with 15 maximum concurrency.
   For 32 work items got 902885 SHA256 hashes/sec with 32 maximum concurrency.
   For 64 work items got 919633 SHA256 hashes/sec with 34 maximum concurrency.
   For 128 work items got 919695 SHA256 hashes/sec with 35 maximum concurrency.
   For 256 work items got 923159 SHA256 hashes/sec with 36 maximum concurrency.
   For 512 work items got 922961 SHA256 hashes/sec with 37 maximum concurrency.
   For 1024 work items got 926624 SHA256 hashes/sec with 38 maximum concurrency.

Benchmarking llfio (Linux native) ...
   For 1 work items got 4193.79 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 8422.44 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 12521.7 SHA256 hashes/sec with 3 maximum concurrency.
   For 8 work items got 20028.4 SHA256 hashes/sec with 6 maximum concurrency.
   For 16 work items got 30657.4 SHA256 hashes/sec with 10 maximum concurrency.
   For 32 work items got 53217.4 SHA256 hashes/sec with 20 maximum concurrency.
   For 64 work items got 65452.3 SHA256 hashes/sec with 32 maximum concurrency.
   For 128 work items got 65396.3 SHA256 hashes/sec with 32 maximum concurrency.
   For 256 work items got 65363.7 SHA256 hashes/sec with 32 maximum concurrency.
   For 512 work items got 65198.2 SHA256 hashes/sec with 32 maximum concurrency.
   For 1024 work items got 65003.9 SHA256 hashes/sec with 34 maximum concurrency.
*/


/* Windows 4Kb and 64Kb Win32 thread pool

Benchmarking llfio (Win32 thread pool (Vista+)) ...
   For 1 work items got 57995.3 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 120267 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 238139 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 413488 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 575423 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 720938 SHA256 hashes/sec with 31 maximum concurrency.
   For 64 work items got 703460 SHA256 hashes/sec with 30 maximum concurrency.
   For 128 work items got 678257 SHA256 hashes/sec with 29 maximum concurrency.
   For 256 work items got 678898 SHA256 hashes/sec with 29 maximum concurrency.
   For 512 work items got 671729 SHA256 hashes/sec with 28 maximum concurrency.
   For 1024 work items got 674433 SHA256 hashes/sec with 30 maximum concurrency.

Benchmarking llfio (Win32 thread pool (Vista+)) ...
   For 1 work items got 4132.18 SHA256 hashes/sec with 1 maximum concurrency.
   For 2 work items got 8197.21 SHA256 hashes/sec with 2 maximum concurrency.
   For 4 work items got 16281.3 SHA256 hashes/sec with 4 maximum concurrency.
   For 8 work items got 27447.5 SHA256 hashes/sec with 8 maximum concurrency.
   For 16 work items got 42621.3 SHA256 hashes/sec with 16 maximum concurrency.
   For 32 work items got 69857.7 SHA256 hashes/sec with 32 maximum concurrency.
   For 64 work items got 68797.9 SHA256 hashes/sec with 33 maximum concurrency.
   For 128 work items got 68980.4 SHA256 hashes/sec with 33 maximum concurrency.
   For 256 work items got 70370.8 SHA256 hashes/sec with 33 maximum concurrency.
   For 512 work items got 70365.8 SHA256 hashes/sec with 33 maximum concurrency.
   For 1024 work items got 70794.6 SHA256 hashes/sec with 33 maximum concurrency.
*/


LLFIO_V2_NAMESPACE_BEGIN

namespace detail
{
  struct dynamic_thread_pool_group_impl_guard : std::unique_lock<std::mutex>
  {
    using std::unique_lock<std::mutex>::unique_lock;
  };
#if 0
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
    T fetch_add(T v, std::memory_order /*unused*/)
    {
      _v += v;
      return _v - v;
    }
    T fetch_sub(T v, std::memory_order /*unused*/)
    {
      _v -= v;
      return _v + v;
    }
  };
#endif
  struct global_dynamic_thread_pool_impl_workqueue_item
  {
    const size_t nesting_level;
    std::shared_ptr<global_dynamic_thread_pool_impl_workqueue_item> next;
    std::unordered_set<dynamic_thread_pool_group_impl *> items;  // Do NOT use without holding workqueue_lock

    explicit global_dynamic_thread_pool_impl_workqueue_item(size_t _nesting_level, std::shared_ptr<global_dynamic_thread_pool_impl_workqueue_item> &&preceding)
        : nesting_level(_nesting_level)
        , next(preceding)
    {
    }

#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
    static constexpr unsigned TOTAL_NEXTACTIVES = 1;
    struct next_active_base_t
    {
      std::atomic<unsigned> count{0};
      QUICKCPPLIB_NAMESPACE::configurable_spinlock::spinlock<unsigned> lock;
      dynamic_thread_pool_group::work_item *front{nullptr}, *back{nullptr};

      next_active_base_t() = default;
      next_active_base_t(const next_active_base_t &o)
          : count(o.count.load(std::memory_order_relaxed))
          , front(o.front)
          , back(o.back)
      {
      }
    };
    struct alignas(64) next_active_work_t : next_active_base_t
    {
      char _padding[64 - sizeof(next_active_base_t)];  // 40 bytes?
    } next_actives[TOTAL_NEXTACTIVES];
    static_assert(sizeof(next_active_work_t) == 64, "next_active_work_t is not a cacheline");
    next_active_base_t next_timer_relative, next_timer_absolute;

    dynamic_thread_pool_group::work_item *next_active(unsigned &count, size_t threadidx)
    {
      if(TOTAL_NEXTACTIVES > 1)
      {
        threadidx &= (TOTAL_NEXTACTIVES - 1);
        const size_t original_threadidx = threadidx;
        bool all_empty = true;
        for(;;)
        {
          next_active_base_t &x = next_actives[threadidx];
          if(x.count.load(std::memory_order_relaxed) > 0)
          {
            all_empty = false;
            if(x.lock.try_lock())
            {
              auto *ret = x.front;
              if(ret != nullptr)
              {
                x.front = ret->_next_scheduled;
                count = x.count.fetch_sub(1, std::memory_order_relaxed);
                if(x.front == nullptr)
                {
                  assert(x.back == ret);
                  x.back = nullptr;
                }
                ret->_next_scheduled = nullptr;
                x.lock.unlock();
                return ret;
              }
              x.lock.unlock();
            }
          }
          if(++threadidx >= TOTAL_NEXTACTIVES)
          {
            threadidx = 0;
          }
          if(threadidx == original_threadidx)
          {
            if(all_empty)
            {
              return nullptr;
            }
            all_empty = true;
          }
        }
      }
      else
      {
        next_active_base_t &x = next_actives[0];
        if(x.count.load(std::memory_order_relaxed) > 0)
        {
          x.lock.lock();
          auto *ret = x.front;
          if(ret != nullptr)
          {
            x.front = ret->_next_scheduled;
            count = x.count.fetch_sub(1, std::memory_order_relaxed);
            if(x.front == nullptr)
            {
              assert(x.back == ret);
              x.back = nullptr;
            }
            ret->_next_scheduled = nullptr;
            x.lock.unlock();
            return ret;
          }
          x.lock.unlock();
        }
      }
      return nullptr;
    }

  private:
    next_active_base_t &_choose_next_active()
    {
      unsigned idx = (unsigned) -1, max_count = (unsigned) -1;
      for(unsigned n = 0; n < TOTAL_NEXTACTIVES; n++)
      {
        auto c = next_actives[n].count.load(std::memory_order_relaxed);
        if(c < max_count)
        {
          idx = n;
          max_count = c;
        }
      }
      for(;;)
      {
        if(next_actives[idx].lock.try_lock())
        {
          return next_actives[idx];
        }
        if(++idx >= TOTAL_NEXTACTIVES)
        {
          idx = 0;
        }
      }
    }

  public:
    void append_active(dynamic_thread_pool_group::work_item *p)
    {
      next_active_base_t &x = _choose_next_active();
      x.count.fetch_add(1, std::memory_order_relaxed);
      if(x.back == nullptr)
      {
        assert(x.front == nullptr);
        x.front = x.back = p;
        x.lock.unlock();
        return;
      }
      p->_next_scheduled = nullptr;
      x.back->_next_scheduled = p;
      x.back = p;
      x.lock.unlock();
    }
    void prepend_active(dynamic_thread_pool_group::work_item *p)
    {
      next_active_base_t &x = _choose_next_active();
      x.count.fetch_add(1, std::memory_order_relaxed);
      if(x.front == nullptr)
      {
        assert(x.back == nullptr);
        x.front = x.back = p;
        x.lock.unlock();
        return;
      }
      p->_next_scheduled = x.front;
      x.front = p;
      x.lock.unlock();
    }

    // x must be LOCKED on entry
    template <int which> dynamic_thread_pool_group::work_item *next_timer()
    {
      if(which == 0)
      {
        return nullptr;
      }
      next_active_base_t &x = (which == 1) ? next_timer_relative : next_timer_absolute;
      // x.lock.lock();
      auto *ret = x.front;
      if(ret == nullptr)
      {
        assert(x.back == nullptr);
        x.lock.unlock();
        return nullptr;
      }
      x.front = ret->_next_scheduled;
      if(x.front == nullptr)
      {
        assert(x.back == ret);
        x.back = nullptr;
      }
      ret->_next_scheduled = nullptr;
      x.count.fetch_sub(1, std::memory_order_relaxed);
      x.lock.unlock();
      return ret;
    }
    void append_timer(dynamic_thread_pool_group::work_item *i)
    {
      if(i->_timepoint1 != std::chrono::steady_clock::time_point())
      {
        next_timer_relative.lock.lock();
        next_timer_relative.count.fetch_add(1, std::memory_order_relaxed);
        if(next_timer_relative.front == nullptr)
        {
          next_timer_relative.front = next_timer_relative.back = i;
        }
        else
        {
          bool done = false;
          for(dynamic_thread_pool_group::work_item *p = nullptr, *n = next_timer_relative.front; n != nullptr; p = n, n = n->_next_scheduled)
          {
            if(n->_timepoint1 <= i->_timepoint1)
            {
              if(p == nullptr)
              {
                i->_next_scheduled = n;
                next_timer_relative.front = i;
              }
              else
              {
                i->_next_scheduled = n;
                p->_next_scheduled = i;
              }
              done = true;
              break;
            }
          }
          if(!done)
          {
            next_timer_relative.back->_next_scheduled = i;
            i->_next_scheduled = nullptr;
            next_timer_relative.back = i;
          }
        }
        next_timer_relative.lock.unlock();
      }
      else
      {
        next_timer_absolute.lock.lock();
        next_timer_absolute.count.fetch_add(1, std::memory_order_relaxed);
        if(next_timer_absolute.front == nullptr)
        {
          next_timer_absolute.front = next_timer_absolute.back = i;
        }
        else
        {
          bool done = false;
          for(dynamic_thread_pool_group::work_item *p = nullptr, *n = next_timer_absolute.front; n != nullptr; p = n, n = n->_next_scheduled)
          {
            if(n->_timepoint2 <= i->_timepoint2)
            {
              if(p == nullptr)
              {
                i->_next_scheduled = n;
                next_timer_absolute.front = i;
              }
              else
              {
                i->_next_scheduled = n;
                p->_next_scheduled = i;
              }
              done = true;
              break;
            }
          }
          if(!done)
          {
            next_timer_absolute.back->_next_scheduled = i;
            i->_next_scheduled = nullptr;
            next_timer_absolute.back = i;
          }
        }
        next_timer_absolute.lock.unlock();
      }
    }
#endif
  };
  struct global_dynamic_thread_pool_impl
  {
    using _spinlock_type = QUICKCPPLIB_NAMESPACE::configurable_spinlock::spinlock<unsigned>;

    _spinlock_type workqueue_lock;
    struct workqueue_guard : std::unique_lock<_spinlock_type>
    {
      using std::unique_lock<_spinlock_type>::unique_lock;
    };
    std::shared_ptr<global_dynamic_thread_pool_impl_workqueue_item> workqueue;
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
    using threadh_type = void *;
    using grouph_type = dispatch_group_t;
    static void _gcd_dispatch_callback(void *arg)
    {
      auto *workitem = (dynamic_thread_pool_group::work_item *) arg;
      global_dynamic_thread_pool()._workerthread(workitem, nullptr);
    }
    static void _gcd_timer_callback(void *arg)
    {
      auto *workitem = (dynamic_thread_pool_group::work_item *) arg;
      global_dynamic_thread_pool()._timerthread(workitem, nullptr);
    }
#elif defined(_WIN32)
    using threadh_type = PTP_CALLBACK_INSTANCE;
    using grouph_type = PTP_CALLBACK_ENVIRON;
    static void CALLBACK _win32_worker_thread_callback(threadh_type threadh, PVOID Parameter, PTP_WORK /*unused*/)
    {
      auto *workitem = (dynamic_thread_pool_group::work_item *) Parameter;
      global_dynamic_thread_pool()._workerthread(workitem, threadh);
    }
    static void CALLBACK _win32_timer_thread_callback(threadh_type threadh, PVOID Parameter, PTP_TIMER /*unused*/)
    {
      auto *workitem = (dynamic_thread_pool_group::work_item *) Parameter;
      global_dynamic_thread_pool()._timerthread(workitem, threadh);
    }
#else
    global_dynamic_thread_pool_impl_workqueue_item first_execute{(size_t) -1, {}};
    using threadh_type = void *;
    using grouph_type = void *;
    std::mutex threadpool_lock;
    struct threadpool_guard : std::unique_lock<std::mutex>
    {
      using std::unique_lock<std::mutex>::unique_lock;
    };
    struct thread_t
    {
      thread_t *_prev{nullptr}, *_next{nullptr};
      std::thread thread;
      std::condition_variable cond;
      std::chrono::steady_clock::time_point last_did_work;
      std::atomic<int> state{0};  // <0 = dead, 0 = sleeping/please die, 1 = busy
    };
    struct threads_t
    {
      size_t count{0};
      thread_t *front{nullptr}, *back{nullptr};
    } threadpool_active, threadpool_sleeping;
    std::atomic<size_t> total_submitted_workitems{0}, threadpool_threads{0};
    std::atomic<uint32_t> ms_sleep_for_more_work{20000};

    std::mutex threadmetrics_lock;
    struct threadmetrics_guard : std::unique_lock<std::mutex>
    {
      using std::unique_lock<std::mutex>::unique_lock;
    };
    struct threadmetrics_threadid
    {
      char text[12];  // enough for a UINT32_MAX in decimal
      constexpr threadmetrics_threadid()
          : text{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
      {
      }
      threadmetrics_threadid(string_view sv)
      {
        memset(text, '0', sizeof(text));
        assert(sv.size() <= sizeof(text));
        if(sv.size() > sizeof(text))
        {
          abort();
        }
        memcpy(text + sizeof(text) - sv.size(), sv.data(), sv.size());
      }
      int compare(const threadmetrics_threadid &o) const noexcept { return memcmp(text, o.text, sizeof(text)); }
      bool operator<(const threadmetrics_threadid &o) const noexcept { return compare(o) < 0; }
      bool operator==(const threadmetrics_threadid &o) const noexcept { return compare(o) == 0; }
    };
    struct threadmetrics_item
    {
      threadmetrics_item *_prev{nullptr}, *_next{nullptr};
      std::chrono::steady_clock::time_point last_updated, blocked_since;  // latter set if thread seen no time
      threadmetrics_threadid threadid;
      uint32_t diskfaults{(uint32_t) -1}, utime{(uint32_t) -1}, stime{(uint32_t) -1};  // culmulative ticks spent in user and system for this thread

      explicit threadmetrics_item(threadmetrics_threadid v)
          : threadid(v)
      {
      }
      string_view threadid_name() const noexcept { return string_view(threadid.text, sizeof(threadid.text)); }
    };
    struct threadmetrics_t
    {
      size_t count{0};
      threadmetrics_item *front{nullptr}, *back{nullptr};
      uint32_t blocked{0}, running{0};
    } threadmetrics_queue;                                   // items at front are least recently updated
    std::vector<threadmetrics_item *> threadmetrics_sorted;  // sorted by threadid
    std::chrono::steady_clock::time_point threadmetrics_last_updated;
    std::atomic<unsigned> populate_threadmetrics_reentrancy{0};
#ifdef __linux__
    std::mutex proc_self_task_fd_lock;
    int proc_self_task_fd{-1};
#endif
#endif

    std::mutex io_aware_work_item_handles_lock;
    struct io_aware_work_item_handles_guard : std::unique_lock<std::mutex>
    {
      using std::unique_lock<std::mutex>::unique_lock;
    };
    struct io_aware_work_item_statfs
    {
      size_t refcount{0};
      deadline default_deadline;
      float average_busy{0}, average_queuedepth{0};
      std::chrono::steady_clock::time_point last_updated;
      statfs_t statfs;
    };
    std::unordered_map<fs_handle::unique_id_type, io_aware_work_item_statfs, fs_handle::unique_id_type_hasher> io_aware_work_item_handles;

    global_dynamic_thread_pool_impl()
    {
#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
      populate_threadmetrics(std::chrono::steady_clock::now());
#endif
    }

    template <class T, class U> static void _append_to_list(T &what, U *v)
    {
      if(what.front == nullptr)
      {
        assert(what.back == nullptr);
        v->_next = v->_prev = nullptr;
        what.front = what.back = v;
      }
      else
      {
        v->_next = nullptr;
        v->_prev = what.back;
        what.back->_next = v;
        what.back = v;
      }
      what.count++;
    }
    template <class T, class U> static void _prepend_to_list(T &what, U *v)
    {
      if(what.front == nullptr)
      {
        assert(what.back == nullptr);
        v->_next = v->_prev = nullptr;
        what.front = what.back = v;
      }
      else
      {
        v->_prev = nullptr;
        v->_next = what.front;
        what.front->_prev = v;
        what.front = v;
      }
      what.count++;
    }
    template <class T, class U> static void _remove_from_list(T &what, U *v)
    {
      if(v->_prev == nullptr && v->_next == nullptr)
      {
        assert(what.front == v);
        assert(what.back == v);
        what.front = what.back = nullptr;
      }
      else if(v->_prev == nullptr)
      {
        assert(what.front == v);
        v->_next->_prev = nullptr;
        what.front = v->_next;
        v->_next = v->_prev = nullptr;
      }
      else if(v->_next == nullptr)
      {
        assert(what.back == v);
        v->_prev->_next = nullptr;
        what.back = v->_prev;
        v->_next = v->_prev = nullptr;
      }
      else
      {
        v->_next->_prev = v->_prev;
        v->_prev->_next = v->_next;
        v->_next = v->_prev = nullptr;
      }
      what.count--;
    }

#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
    inline void _execute_work(thread_t *self);

    void _add_thread(threadpool_guard & /*unused*/)
    {
      thread_t *p = nullptr;
      try
      {
        p = new thread_t;
        _append_to_list(threadpool_active, p);
        p->thread = std::thread([this, p] { _execute_work(p); });
      }
      catch(...)
      {
        if(p != nullptr)
        {
          _remove_from_list(threadpool_active, p);
        }
        // drop failure
      }
    }

    bool _remove_thread(threadpool_guard &g, threads_t &which)
    {
      if(which.count == 0)
      {
        return false;
      }
      // Threads which went to sleep the longest ago are at the front
      auto *t = which.front;
      if(t->state.load(std::memory_order_acquire) < 0)
      {
        // He's already exiting
        return false;
      }
      assert(t->state.load(std::memory_order_acquire) == 0);
      t->state.fetch_sub(1, std::memory_order_release);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
      std::cout << "*** DTP " << t << " is told to quit" << std::endl;
#endif
      do
      {
        g.unlock();
        t->cond.notify_one();
        g.lock();
      } while(t->state.load(std::memory_order_acquire) >= -1);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
      std::cout << "*** DTP " << t << " has quit, deleting" << std::endl;
#endif
      _remove_from_list(threadpool_active, t);
      t->thread.join();
      delete t;
      return true;
    }

    ~global_dynamic_thread_pool_impl()
    {
      {
        threadpool_guard g(threadpool_lock);
        while(threadpool_active.count > 0 || threadpool_sleeping.count > 0)
        {
          while(threadpool_sleeping.count > 0)
          {
            auto removed = _remove_thread(g, threadpool_sleeping);
            assert(removed);
            (void) removed;
          }
          if(threadpool_active.count > 0)
          {
            auto removed = _remove_thread(g, threadpool_active);
            assert(removed);
            (void) removed;
          }
        }
      }
      threadmetrics_guard g(threadmetrics_lock);
      for(auto *p : threadmetrics_sorted)
      {
        delete p;
      }
      threadmetrics_sorted.clear();
      threadmetrics_queue = {};
#ifdef __linux__
      if(proc_self_task_fd > 0)
      {
        ::close(proc_self_task_fd);
        proc_self_task_fd = -1;
      }
#endif
    }

#ifdef __linux__
    // You are guaranteed only one of these EVER executes at a time. Locking is probably overkill, but equally also probably harmless
    bool update_threadmetrics(threadmetrics_guard &&g, std::chrono::steady_clock::time_point now, threadmetrics_item *new_items)
    {
      auto update_item = [&](threadmetrics_item *item) {
        char path[64] = "/proc/self/task/", *pend = path + 16, *tend = item->threadid.text;
        while(*tend == '0' && (tend - item->threadid.text) < (ssize_t) sizeof(item->threadid.text))
        {
          ++tend;
        }
        while((tend - item->threadid.text) < (ssize_t) sizeof(item->threadid.text))
        {
          *pend++ = *tend++;
        }
        memcpy(pend, "/stat", 6);
        int fd = ::open(path, O_RDONLY);
        if(-1 == fd)
        {
          // Thread may have exited since we last populated
          if(item->blocked_since == std::chrono::steady_clock::time_point())
          {
            threadmetrics_queue.running--;
            threadmetrics_queue.blocked++;
          }
          item->blocked_since = now;
          item->last_updated = now;
          return;
        }
        char buffer[1024];
        auto bytesread = ::read(fd, buffer, sizeof(buffer));
        ::close(fd);
        buffer[std::max((size_t) bytesread, sizeof(buffer) - 1)] = 0;
        char state = 0;
        unsigned long majflt = 0, utime = 0, stime = 0;
        sscanf(buffer, "%*d %*s %c %*d %*d %*d %*d %*d %*u %*u %*u %lu %*u %lu %lu", &state, &majflt, &utime, &stime);
        if(item->utime != (uint32_t) -1 || item->stime != (uint32_t) -1)
        {
          if(item->utime == (uint32_t) utime && item->stime == (uint32_t) stime && state != 'R')
          {
            // This thread made no progress since last time
            if(item->blocked_since == std::chrono::steady_clock::time_point())
            {
              threadmetrics_queue.running--;
              threadmetrics_queue.blocked++;
              item->blocked_since = now;
            }
          }
          else
          {
            if(item->blocked_since != std::chrono::steady_clock::time_point())
            {
              threadmetrics_queue.running++;
              threadmetrics_queue.blocked--;
              item->blocked_since = std::chrono::steady_clock::time_point();
            }
          }
        }
        // std::cout << "Threadmetrics " << path << " " << state << " " << majflt << " " << utime << " " << stime << ". Previously " << item->diskfaults << " "
        //          << item->utime << " " << item->stime << std::endl;
        item->diskfaults = (uint32_t) majflt;
        item->utime = (uint32_t) utime;
        item->stime = (uint32_t) stime;
        item->last_updated = now;
      };
      if(new_items != nullptr)
      {
        for(; new_items != nullptr; new_items = new_items->_next)
        {
          update_item(new_items);
        }
        return false;
      }
      if(threadmetrics_queue.count == 0)
      {
        return false;
      }
      size_t updated = 0;
      while(now - threadmetrics_queue.front->last_updated >= std::chrono::milliseconds(200) && updated++ < 4)
      {
        auto *p = threadmetrics_queue.front;
        update_item(p);
        _remove_from_list(threadmetrics_queue, p);
        _append_to_list(threadmetrics_queue, p);
      }
      // if(updated > 0)
      {
        static const auto min_hardware_concurrency = std::thread::hardware_concurrency();
        static const auto max_hardware_concurrency = min_hardware_concurrency + 3;
        auto threadmetrics_running = (ssize_t) threadmetrics_queue.running;
        auto threadmetrics_blocked = (ssize_t) threadmetrics_queue.blocked;
        g.unlock();  // drop threadmetrics_lock

        threadpool_guard gg(threadpool_lock);
        // Adjust for the number of threads sleeping for more work
        threadmetrics_running += threadpool_sleeping.count;
        threadmetrics_blocked -= threadpool_sleeping.count;
        if(threadmetrics_blocked < 0)
        {
          threadmetrics_blocked = 0;
        }
        const auto desired_concurrency = std::min((ssize_t) min_hardware_concurrency, (ssize_t) total_submitted_workitems.load(std::memory_order_relaxed));
        auto toadd = std::max((ssize_t) 0, std::min(desired_concurrency - threadmetrics_running, desired_concurrency - (ssize_t) threadpool_active.count));
        auto toremove = std::max((ssize_t) 0, (ssize_t) threadmetrics_running - (ssize_t) max_hardware_concurrency);
        if(toadd > 0 || toremove > 0)
        {
          // std::cout << "total active = " << threadpool_active.count << " total idle = " << threadpool_sleeping.count
          //          << " threadmetrics_running = " << threadmetrics_running << " threadmetrics_blocked = " << threadmetrics_blocked << " toadd = " << toadd
          //          << " toremove = " << toremove << std::endl;
          if(toadd > 0)
          {
            _add_thread(gg);
          }
          if(toremove > 0 && threadpool_active.count > 1)
          {
            // Kill myself, but not if I'm the final thread who might need to run timers
            return true;
          }
        }
      }
      return false;
    }
    // Returns true if calling thread is to exit
    bool populate_threadmetrics(std::chrono::steady_clock::time_point now)
    {
      if(populate_threadmetrics_reentrancy.exchange(1, std::memory_order_relaxed) == 1)
      {
        return false;
      }
      auto unpopulate_threadmetrics_reentrancy = make_scope_exit([this]() noexcept { populate_threadmetrics_reentrancy.store(0, std::memory_order_relaxed); });

      static thread_local std::vector<char> kernelbuffer(1024);
      static thread_local std::vector<threadmetrics_threadid> threadidsbuffer(1024 / sizeof(dirent));
      using getdents64_t = int (*)(int, char *, unsigned int);
      static auto getdents = static_cast<getdents64_t>([](int fd, char *buf, unsigned count) -> int { return syscall(SYS_getdents64, fd, buf, count); });
      using dirent = dirent64;
      size_t bytes = 0;
      {
        threadmetrics_guard g(threadmetrics_lock);
        if(now - threadmetrics_last_updated < std::chrono::milliseconds(250) &&
           threadmetrics_queue.running + threadmetrics_queue.blocked >= threadpool_threads.load(std::memory_order_relaxed))
        {
          if(now - threadmetrics_last_updated < std::chrono::milliseconds(100))
          {
            return false;
          }
          return update_threadmetrics(std::move(g), now, nullptr);
        }
        threadmetrics_last_updated = now;
        if(proc_self_task_fd < 0)
        {
          proc_self_task_fd = ::open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
          if(proc_self_task_fd < 0)
          {
            posix_error().throw_exception();
          }
        }
      }
      {
        std::lock_guard<std::mutex> g(proc_self_task_fd_lock);
        /* It turns out that /proc/self/task is quite racy in the Linux kernel, so keep
        looping this until it stops telling obvious lies.
        */
        for(auto done = false; !done;)
        {
          if(-1 == ::lseek64(proc_self_task_fd, 0, SEEK_SET))
          {
            posix_error().throw_exception();
          }
          auto _bytes = getdents(proc_self_task_fd, kernelbuffer.data(), kernelbuffer.size());
          // std::cout << "getdents(" << (kernelbuffer.size()-bytes) << ") returns " << _bytes << std::endl;
          if(_bytes < 0 && errno != EINVAL)
          {
            posix_error().throw_exception();
          }
          if(_bytes >= 0 && kernelbuffer.size() - (size_t) _bytes >= sizeof(dirent) + 16)
          {
            bytes = (size_t) _bytes;
            threadidsbuffer.clear();
            for(auto *dent = (dirent *) kernelbuffer.data();; dent = reinterpret_cast<dirent *>(reinterpret_cast<uintptr_t>(dent) + dent->d_reclen))
            {
              if(dent->d_ino != 0u && dent->d_type == DT_DIR && dent->d_name[0] != '.')
              {
                size_t length = strchr(dent->d_name, 0) - dent->d_name;
                threadidsbuffer.push_back(string_view(dent->d_name, length));
              }
              if((bytes -= dent->d_reclen) <= 0)
              {
                break;
              }
            }
            auto mythreadcount = threadpool_threads.load(std::memory_order_relaxed);
            if(threadidsbuffer.size() >= mythreadcount)
            {
              // std::cout << "Parsed from /proc " << threadidsbuffer.size() << " entries, should be at least " << mythreadcount << std::endl;
              std::sort(threadidsbuffer.begin(), threadidsbuffer.end());
              done = true;
              break;
            }
#ifndef NDEBUG
            std::cout << "NOTE: /proc returned " << threadidsbuffer.size() << " items when we know for a fact at least " << mythreadcount
                      << " threads exist, retrying!" << std::endl;
#endif
            continue;
          }
          kernelbuffer.resize(kernelbuffer.size() << 1);
        }
      }
      threadmetrics_item *firstnewitem = nullptr;
      threadmetrics_guard g(threadmetrics_lock);
#if 0
      {
        std::stringstream s;
        s << "Parsed from /proc " << threadidsbuffer.size() << " entries (should be at least " << threadpool_threads.load(std::memory_order_relaxed) << "):";
        for(auto &i : threadidsbuffer)
        {
          s << " " << string_view(i.text, 12);
        }
        std::cout << s.str() << std::endl;
      }
#endif
#if 0
      {
        auto d_it = threadmetrics_sorted.begin();
        auto s_it = threadidsbuffer.begin();
        for(; d_it != threadmetrics_sorted.end() && s_it != threadidsbuffer.end(); ++d_it, ++s_it)
        {
          std::cout << (*d_it)->threadid_name() << "   " << string_view(s_it->text, 12) << "\n";
        }
        for(; d_it != threadmetrics_sorted.end(); ++d_it)
        {
          std::cout << (*d_it)->threadid_name() << "   XXXXXXXXXXXX\n";
        }
        for(; s_it != threadidsbuffer.end(); ++s_it)
        {
          std::cout << "XXXXXXXXXXXX   " << string_view(s_it->text, 12) << "\n";
        }
        std::cout << std::flush;
      }
#endif
      auto d_it = threadmetrics_sorted.begin();
      auto s_it = threadidsbuffer.begin();
      auto remove_item = [&] {
        // std::cout << "Removing thread metrics for " << (*d_it)->threadid_name() << std::endl;
        if((*d_it)->blocked_since != std::chrono::steady_clock::time_point())
        {
          threadmetrics_queue.blocked--;
        }
        else
        {
          threadmetrics_queue.running--;
        }
        _remove_from_list(threadmetrics_queue, *d_it);
        delete *d_it;
        d_it = threadmetrics_sorted.erase(d_it);
      };
      auto add_item = [&] {
        auto p = std::make_unique<threadmetrics_item>(*s_it);
        d_it = threadmetrics_sorted.insert(d_it, p.get());
        _append_to_list(threadmetrics_queue, p.get());
        // std::cout << "Adding thread metrics for " << p->threadid_name() << std::endl;
        if(firstnewitem == nullptr)
        {
          firstnewitem = p.get();
        }
        p.release();
        threadmetrics_queue.running++;
      };
      // std::cout << "Compare" << std::endl;
      for(; d_it != threadmetrics_sorted.end() && s_it != threadidsbuffer.end();)
      {
        auto c = (*d_it)->threadid.compare(*s_it);
        // std::cout << "Comparing " << (*d_it)->threadid_name() << " with " << string_view(s_it->text, 12) << " = " << c << std::endl;
        if(0 == c)
        {
          ++d_it;
          ++s_it;
          continue;
        }
        if(c < 0)
        {
          // d_it has gone away
          remove_item();
        }
        if(c > 0)
        {
          // s_it is a new entry
          add_item();
        }
      }
      // std::cout << "Tail dest" << std::endl;
      while(d_it != threadmetrics_sorted.end())
      {
        remove_item();
      }
      // std::cout << "Tail source" << std::endl;
      while(s_it != threadidsbuffer.end())
      {
        add_item();
        ++d_it;
        ++s_it;
      }
      assert(threadmetrics_sorted.size() == threadidsbuffer.size());
#if 0
      if(!std::is_sorted(threadmetrics_sorted.begin(), threadmetrics_sorted.end(),
                         [](threadmetrics_item *a, threadmetrics_item *b) { return a->threadid < b->threadid; }))
      {
        std::cout << "Threadmetrics:";
        for(auto *p : threadmetrics_sorted)
        {
          std::cout << "\n   " << p->threadid_name();
        }
        std::cout << std::endl;
        abort();
      }
#endif
      assert(threadmetrics_queue.running + threadmetrics_queue.blocked == threadidsbuffer.size());
      return update_threadmetrics(std::move(g), now, firstnewitem);
    }
#endif
#endif

    result<void> _prepare_work_item_delay(dynamic_thread_pool_group::work_item *workitem, grouph_type grouph, deadline d)
    {
      (void) grouph;
      if(!d)
      {
        return errc::invalid_argument;
      }
      if(workitem->_nextwork.load(std::memory_order_acquire) == 0 || d.nsecs > 0)
      {
        if(d.nsecs > 0)
        {
          if(d.steady)
          {
            workitem->_timepoint1 = std::chrono::steady_clock::now() + std::chrono::nanoseconds(d.nsecs);
            workitem->_timepoint2 = {};
          }
          else
          {
            workitem->_timepoint1 = {};
            workitem->_timepoint2 = d.to_time_point();
          }
        }
        else
        {
          workitem->_timepoint1 = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(1));
          workitem->_timepoint2 = {};
        }
        assert(workitem->_has_timer_set());
#if defined(_WIN32)
        if(nullptr == workitem->_internaltimerh)
        {
          workitem->_internaltimerh = CreateThreadpoolTimer(_win32_timer_thread_callback, workitem, grouph);
          if(nullptr == workitem->_internaltimerh)
          {
            return win32_error();
          }
        }
#endif
      }
      else
      {
        if(workitem->_timepoint1 != std::chrono::steady_clock::time_point())
        {
          workitem->_timepoint1 = {};
        }
        if(workitem->_timepoint2 != std::chrono::system_clock::time_point())
        {
          workitem->_timepoint2 = {};
        }
        assert(!workitem->_has_timer_set());
      }
      return success();
    }

    inline void _submit_work_item(bool submit_into_highest_priority, dynamic_thread_pool_group::work_item *workitem, bool defer_pool_wake);

    inline result<void> submit(dynamic_thread_pool_group_impl_guard &g, dynamic_thread_pool_group_impl *group,
                               span<dynamic_thread_pool_group::work_item *> work) noexcept;

    inline void _work_item_done(dynamic_thread_pool_group_impl_guard &g, dynamic_thread_pool_group::work_item *i) noexcept;

    inline result<void> stop(dynamic_thread_pool_group_impl_guard &g, dynamic_thread_pool_group_impl *group, result<void> err) noexcept;
    inline result<void> wait(dynamic_thread_pool_group_impl_guard &g, bool reap, dynamic_thread_pool_group_impl *group, deadline d) noexcept;

    inline void _timerthread(dynamic_thread_pool_group::work_item *workitem, threadh_type selfthreadh);
    inline void _workerthread(dynamic_thread_pool_group::work_item *workitem, threadh_type selfthreadh);
  };
  struct global_dynamic_thread_pool_impl_thread_local_state_t
  {
    dynamic_thread_pool_group::work_item *workitem{nullptr};
    global_dynamic_thread_pool_impl::threadh_type current_callback_instance{nullptr};
    size_t nesting_level{0};
  };
  LLFIO_HEADERS_ONLY_FUNC_SPEC global_dynamic_thread_pool_impl_thread_local_state_t &global_dynamic_thread_pool_thread_local_state() noexcept
  {
    static thread_local global_dynamic_thread_pool_impl_thread_local_state_t tls;
    return tls;
  }

  LLFIO_HEADERS_ONLY_FUNC_SPEC global_dynamic_thread_pool_impl &global_dynamic_thread_pool() noexcept
  {
    static global_dynamic_thread_pool_impl impl;
    return impl;
  }
}  // namespace detail


LLFIO_HEADERS_ONLY_MEMFUNC_SPEC const char *dynamic_thread_pool_group::implementation_description() noexcept
{
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
  return "Grand Central Dispatch";
#elif defined(_WIN32)
  return "Win32 thread pool (Vista+)";
#elif defined(__linux__)
  return "Linux native";
#else
#error Unknown platform
#endif
}

class dynamic_thread_pool_group_impl final : public dynamic_thread_pool_group
{
  friend struct detail::global_dynamic_thread_pool_impl;

  mutable std::mutex _lock;
  size_t _nesting_level{0};
  struct workitems_t
  {
    size_t count{0};
    dynamic_thread_pool_group::work_item *front{nullptr}, *back{nullptr};
  } _work_items_active, _work_items_done, _work_items_delayed;
  std::atomic<bool> _stopping{false}, _stopped{true}, _completing{false};
  std::atomic<int> _waits{0};
  result<void> _abnormal_completion_cause{success()};  // The cause of any abnormal group completion

#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
  dispatch_group_t _grouph;
#elif defined(_WIN32)
  TP_CALLBACK_ENVIRON _callbackenviron;
  PTP_CALLBACK_ENVIRON _grouph{&_callbackenviron};
#else
  void *_grouph{nullptr};
  size_t _newly_added_active_work_items{0};
  size_t _active_work_items_remaining{0};
#endif

public:
  result<void> init()
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    try
    {
      auto &impl = detail::global_dynamic_thread_pool();
      _nesting_level = detail::global_dynamic_thread_pool_thread_local_state().nesting_level;
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
      _grouph = dispatch_group_create();
      if(_grouph == nullptr)
      {
        return errc::not_enough_memory;
      }
#elif defined(_WIN32)
      InitializeThreadpoolEnvironment(_grouph);
#endif
      detail::global_dynamic_thread_pool_impl::workqueue_guard g(impl.workqueue_lock);
      // Append this group to the global work queue at its nesting level
      if(!impl.workqueue || impl.workqueue->nesting_level <= _nesting_level)
      {
        impl.workqueue = std::make_shared<detail::global_dynamic_thread_pool_impl_workqueue_item>(_nesting_level, std::move(impl.workqueue));
      }
      impl.workqueue->items.insert(this);
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

  virtual ~dynamic_thread_pool_group_impl()
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    (void) wait();
    auto &impl = detail::global_dynamic_thread_pool();
    // detail::dynamic_thread_pool_group_impl_guard g1(_lock);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
    if(nullptr != _grouph)
    {
      dispatch_release(_grouph);
      _grouph = nullptr;
    }
#elif defined(_WIN32)
    if(nullptr != _grouph)
    {
      DestroyThreadpoolEnvironment(_grouph);
      _grouph = nullptr;
    }
#endif
    detail::global_dynamic_thread_pool_impl::workqueue_guard g2(impl.workqueue_lock);
    assert(impl.workqueue->nesting_level >= _nesting_level);
    for(auto *p = impl.workqueue.get(); p != nullptr; p = p->next.get())
    {
      if(p->nesting_level == _nesting_level)
      {
        p->items.erase(this);
        break;
      }
    }
    while(impl.workqueue && impl.workqueue->items.empty())
    {
      impl.workqueue = std::move(impl.workqueue->next);
    }
  }

  virtual result<void> submit(span<work_item *> work) noexcept override
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(_stopping.load(std::memory_order_relaxed))
    {
      return errc::operation_canceled;
    }
    if(_completing.load(std::memory_order_relaxed))
    {
      for(auto *i : work)
      {
        i->_parent.store(this, std::memory_order_release);
        detail::global_dynamic_thread_pool_impl::_append_to_list(_work_items_delayed, i);
      }
      return success();
    }
    _stopped.store(false, std::memory_order_release);
    auto &impl = detail::global_dynamic_thread_pool();
    detail::dynamic_thread_pool_group_impl_guard g(_lock);  // lock group
    if(_work_items_active.count == 0 && _work_items_done.count == 0)
    {
      _abnormal_completion_cause = success();
    }
    OUTCOME_TRY(impl.submit(g, this, work));
    if(_work_items_active.count == 0)
    {
      _stopped.store(true, std::memory_order_release);
    }
    return success();
  }

  virtual result<void> stop() noexcept override
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(_stopped.load(std::memory_order_relaxed))
    {
      return success();
    }
    auto &impl = detail::global_dynamic_thread_pool();
    detail::dynamic_thread_pool_group_impl_guard g(_lock);  // lock group
    return impl.stop(g, this, errc::operation_canceled);
  }

  virtual bool stopping() const noexcept override { return _stopping.load(std::memory_order_relaxed); }

  virtual bool stopped() const noexcept override { return _stopped.load(std::memory_order_relaxed); }

  virtual result<void> wait(deadline d = {}) const noexcept override
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(_stopped.load(std::memory_order_relaxed))
    {
      return success();
    }
    auto &impl = detail::global_dynamic_thread_pool();
    detail::dynamic_thread_pool_group_impl_guard g(_lock);  // lock group
    return impl.wait(g, true, const_cast<dynamic_thread_pool_group_impl *>(this), d);
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC size_t dynamic_thread_pool_group::current_nesting_level() noexcept
{
  return detail::global_dynamic_thread_pool_thread_local_state().nesting_level;
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC dynamic_thread_pool_group::work_item *dynamic_thread_pool_group::current_work_item() noexcept
{
  return detail::global_dynamic_thread_pool_thread_local_state().workitem;
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC uint32_t dynamic_thread_pool_group::ms_sleep_for_more_work() noexcept
{
#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
  return detail::global_dynamic_thread_pool().ms_sleep_for_more_work.load(std::memory_order_relaxed);
#else
  return 0;
#endif
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC uint32_t dynamic_thread_pool_group::ms_sleep_for_more_work(uint32_t v) noexcept
{
#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
  if(0 == v)
  {
    v = 1;
  }
  detail::global_dynamic_thread_pool().ms_sleep_for_more_work.store(v, std::memory_order_relaxed);
  return v;
#else
  (void) v;
  return 0;
#endif
}

LLFIO_HEADERS_ONLY_FUNC_SPEC result<dynamic_thread_pool_group_ptr> make_dynamic_thread_pool_group() noexcept
{
  try
  {
    auto ret = std::make_unique<dynamic_thread_pool_group_impl>();
    OUTCOME_TRY(ret->init());
    return dynamic_thread_pool_group_ptr(std::move(ret));
  }
  catch(...)
  {
    return error_from_exception();
  }
}

namespace detail
{
#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
  inline void global_dynamic_thread_pool_impl::_execute_work(thread_t *self)
  {
    pthread_setname_np(pthread_self(), "LLFIO DYN TPG");
    self->last_did_work = std::chrono::steady_clock::now();
    self->state.fetch_add(1, std::memory_order_release);  // busy
    const unsigned mythreadidx = threadpool_threads.fetch_add(1, std::memory_order_release);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
    std::cout << "*** DTP " << self << " begins." << std::endl;
#endif
    while(self->state.load(std::memory_order_relaxed) > 0)
    {
      dynamic_thread_pool_group::work_item *workitem = nullptr;
      bool workitem_is_timer = false;
      std::chrono::steady_clock::time_point now_steady, earliest_duration;
      std::chrono::system_clock::time_point now_system, earliest_absolute;
      // Start from highest priority work group, executing any timers due before selecting a work item
      {
        auto examine_wq = [&](global_dynamic_thread_pool_impl_workqueue_item &wq) -> dynamic_thread_pool_group::work_item * {
          if(wq.next_timer_relative.count.load(std::memory_order_relaxed) > 0)
          {
            if(now_steady == std::chrono::steady_clock::time_point())
            {
              now_steady = std::chrono::steady_clock::now();
            }
            wq.next_timer_relative.lock.lock();
            if(wq.next_timer_relative.front != nullptr)
            {
              if(wq.next_timer_relative.front->_timepoint1 <= now_steady)
              {
                workitem = wq.next_timer<1>();  // unlocks wq.next_timer_relative.lock
                workitem_is_timer = true;
                return workitem;
              }
              if(earliest_duration == std::chrono::steady_clock::time_point() || wq.next_timer_relative.front->_timepoint1 < earliest_duration)
              {
                earliest_duration = wq.next_timer_relative.front->_timepoint1;
              }
            }
            wq.next_timer_relative.lock.unlock();
          }
          if(wq.next_timer_absolute.count.load(std::memory_order_relaxed) > 0)
          {
            if(now_system == std::chrono::system_clock::time_point())
            {
              now_system = std::chrono::system_clock::now();
            }
            wq.next_timer_absolute.lock.lock();
            if(wq.next_timer_absolute.front != nullptr)
            {
              if(wq.next_timer_absolute.front->_timepoint2 <= now_system)
              {
                workitem = wq.next_timer<2>();  // unlocks wq.next_timer_absolute.lock
                workitem_is_timer = true;
                return workitem;
              }
              if(earliest_absolute == std::chrono::system_clock::time_point() || wq.next_timer_absolute.front->_timepoint2 < earliest_absolute)
              {
                earliest_absolute = wq.next_timer_absolute.front->_timepoint2;
              }
            }
            wq.next_timer_absolute.lock.unlock();
          }
          unsigned count = 0;
          return wq.next_active(count, mythreadidx);
        };
        workitem = examine_wq(first_execute);
        if(workitem == nullptr)
        {
          workqueue_lock.lock();
          auto lock_wq = workqueue;  // take shared_ptr to highest priority collection of work groups
          workqueue_lock.unlock();
          while(lock_wq)
          {
            workitem = examine_wq(*lock_wq);
            if(workitem != nullptr)
            {
              // workqueue_lock.lock();
              // std::cout << "workitem = " << workitem << " nesting_level = " << wq.nesting_level << " count = " << count << std::endl;
              // workqueue_lock.unlock();
              break;
            }
            workqueue_lock.lock();
            lock_wq = lock_wq->next;
            workqueue_lock.unlock();
          }
        }
      }
      if(now_steady == std::chrono::steady_clock::time_point())
      {
        now_steady = std::chrono::steady_clock::now();
      }
      // If there are no timers, and no work to do, time to either die or sleep
      if(workitem == nullptr)
      {
        const std::chrono::steady_clock::duration max_sleep(std::chrono::milliseconds(ms_sleep_for_more_work.load(std::memory_order_relaxed)));
        if(now_steady - self->last_did_work >= max_sleep)
        {
          threadpool_guard g(threadpool_lock);
          // If there are any timers running, leave at least one worker thread
          if(threadpool_active.count > 1 ||
             (earliest_duration == std::chrono::steady_clock::time_point() && earliest_absolute == std::chrono::system_clock::time_point()))
          {
            _remove_from_list(threadpool_active, self);
            threadpool_threads.fetch_sub(1, std::memory_order_release);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
            std::cout << "*** DTP " << self << " exits due to no new work for ms_sleep_for_more_work" << std::endl;
#endif
            self->thread.detach();
            delete self;
            return;
          }
        }
        std::chrono::steady_clock::duration duration(max_sleep);
        if(earliest_duration != std::chrono::steady_clock::time_point())
        {
          if(now_steady - earliest_duration < duration)
          {
            duration = now_steady - earliest_duration;
          }
        }
        else if(earliest_absolute != std::chrono::system_clock::time_point())
        {
          if(now_system == std::chrono::system_clock::time_point())
          {
            now_system = std::chrono::system_clock::now();
          }
          auto diff = now_system - earliest_absolute;
          if(diff > duration)
          {
            earliest_absolute = {};
          }
        }
        threadpool_guard g(threadpool_lock);
        _remove_from_list(threadpool_active, self);
        _append_to_list(threadpool_sleeping, self);
        self->state.fetch_sub(1, std::memory_order_release);
        if(earliest_absolute != std::chrono::system_clock::time_point())
        {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
          std::cout << "*** DTP " << self << " goes to sleep absolute" << std::endl;
#endif
          self->cond.wait_until(g, earliest_absolute);
        }
        else
        {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
          std::cout << "*** DTP " << self << " goes to sleep for " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << std::endl;
#endif
          self->cond.wait_for(g, duration);
        }
        self->state.fetch_add(1, std::memory_order_release);
        _remove_from_list(threadpool_sleeping, self);
        _append_to_list(threadpool_active, self);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
        std::cout << "*** DTP " << self << " wakes, state = " << self->state << std::endl;
#endif
        g.unlock();
        try
        {
          populate_threadmetrics(now_steady);
        }
        catch(...)
        {
        }
        continue;
      }
      self->last_did_work = now_steady;
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
      std::cout << "*** DTP " << self << " executes work item " << workitem << std::endl;
#endif
      total_submitted_workitems.fetch_sub(1, std::memory_order_relaxed);
      if(workitem_is_timer)
      {
        _timerthread(workitem, nullptr);
      }
      else
      {
        _workerthread(workitem, nullptr);
      }
      // workitem->_internalworkh should be null, however workitem may also no longer exist
      try
      {
        if(populate_threadmetrics(now_steady))
        {
          threadpool_guard g(threadpool_lock);
          _remove_from_list(threadpool_active, self);
          threadpool_threads.fetch_sub(1, std::memory_order_release);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
          std::cout << "*** DTP " << self << " exits due to threadmetrics saying we exceed max concurrency" << std::endl;
#endif
          self->thread.detach();
          delete self;
          return;
        }
      }
      catch(...)
      {
      }
    }
    self->state.fetch_sub(2, std::memory_order_release);  // dead
    threadpool_threads.fetch_sub(1, std::memory_order_release);
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
    std::cout << "*** DTP " << self << " exits due to state request, state = " << self->state << std::endl;
#endif
  }
#endif

  inline void global_dynamic_thread_pool_impl::_submit_work_item(bool submit_into_highest_priority, dynamic_thread_pool_group::work_item *workitem,
                                                                 bool defer_pool_wake)
  {
    (void) submit_into_highest_priority;
    (void) defer_pool_wake;
    const auto nextwork = workitem->_nextwork.load(std::memory_order_acquire);
    if(nextwork != -1)
    {
      auto *parent = workitem->_parent.load(std::memory_order_relaxed);
      // If no work item for now, or there is a delay, schedule a timer
      if(nextwork == 0 || workitem->_has_timer_set())
      {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
        dispatch_time_t when;
        if(workitem->_has_timer_set_relative())
        {
          // Special constant for immediately rescheduled work items
          if(workitem->_timepoint1 == std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(1)))
          {
            when = dispatch_time(DISPATCH_TIME_NOW, 0);
          }
          else
          {
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(workitem->_timepoint1 - std::chrono::steady_clock::now()).count();
            if(duration > 1000000000LL)
            {
              // Because GCD has no way of cancelling timers, nor assigning them to a group,
              // we clamp the timer to 1 second. Then if cancellation is ever done to the group,
              // the worst possible wait is 1 second. _timerthread will reschedule the timer
              // if it gets called short.
              duration = 1000000000LL;
            }
            when = dispatch_time(DISPATCH_TIME_NOW, duration);
          }
        }
        else if(workitem->_has_timer_set_absolute())
        {
          deadline d(workitem->_timepoint2);
          auto now = std::chrono::system_clock::now();
          if(workitem->_timepoint2 - now > std::chrono::seconds(1))
          {
            d = now + std::chrono::seconds(1);
          }
          when = dispatch_walltime(&d.utc, 0);
        }
        else
        {
          when = dispatch_time(DISPATCH_TIME_NOW, 1);  // smallest possible non immediate duration from now
        }
        // std::cout << "*** timer " << workitem << std::endl;
        dispatch_after_f(when, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), workitem, _gcd_timer_callback);
#elif defined(_WIN32)
        LARGE_INTEGER li;
        DWORD slop = 1000;
        if(workitem->_has_timer_set_relative())
        {
          // Special constant for immediately rescheduled work items
          if(workitem->_timepoint1 == std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(1)))
          {
            li.QuadPart = -1;  // smallest possible non immediate duration from now
          }
          else
          {
            li.QuadPart = std::chrono::duration_cast<std::chrono::nanoseconds>(workitem->_timepoint1 - std::chrono::steady_clock::now()).count() / 100;
            if(li.QuadPart < 0)
            {
              li.QuadPart = 0;
            }
            if(li.QuadPart / 8 < (int64_t) slop)
            {
              slop = (DWORD)(li.QuadPart / 8);
            }
            li.QuadPart = -li.QuadPart;  // negative is relative
          }
        }
        else if(workitem->_has_timer_set_absolute())
        {
          li = windows_nt_kernel::from_timepoint(workitem->_timepoint2);
        }
        else
        {
          li.QuadPart = -1;  // smallest possible non immediate duration from now
        }
        FILETIME ft;
        ft.dwHighDateTime = (DWORD) li.HighPart;
        ft.dwLowDateTime = li.LowPart;
        // std::cout << "*** timer " << workitem << std::endl;
        SetThreadpoolTimer((PTP_TIMER) workitem->_internaltimerh, &ft, 0, slop);
#else
        workqueue_guard gg(workqueue_lock);
        for(auto *p = workqueue.get(); p != nullptr; p = p->next.get())
        {
          if(p->nesting_level == parent->_nesting_level)
          {
            p->append_timer(workitem);
            break;
          }
        }
#endif
      }
      else
      {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
        intptr_t priority = DISPATCH_QUEUE_PRIORITY_LOW;
        {
          global_dynamic_thread_pool_impl::workqueue_guard gg(workqueue_lock);
          if(workqueue->nesting_level == parent->_nesting_level)
          {
            priority = DISPATCH_QUEUE_PRIORITY_HIGH;
          }
          else if(workqueue->nesting_level == parent->_nesting_level + 1)
          {
            priority = DISPATCH_QUEUE_PRIORITY_DEFAULT;
          }
        }
        // std::cout << "*** submit " << workitem << std::endl;
        dispatch_group_async_f(parent->_grouph, dispatch_get_global_queue(priority, 0), workitem, _gcd_dispatch_callback);
#elif defined(_WIN32)
        // Set the priority of the group according to distance from the top
        TP_CALLBACK_PRIORITY priority = TP_CALLBACK_PRIORITY_LOW;
        {
          global_dynamic_thread_pool_impl::workqueue_guard gg(workqueue_lock);
          if(workqueue->nesting_level == parent->_nesting_level)
          {
            priority = TP_CALLBACK_PRIORITY_HIGH;
          }
          else if(workqueue->nesting_level == parent->_nesting_level + 1)
          {
            priority = TP_CALLBACK_PRIORITY_NORMAL;
          }
        }
        SetThreadpoolCallbackPriority(parent->_grouph, priority);
        // std::cout << "*** submit " << workitem << std::endl;
        SubmitThreadpoolWork((PTP_WORK) workitem->_internalworkh);
#else
        global_dynamic_thread_pool_impl::workqueue_guard gg(workqueue_lock);
        if(submit_into_highest_priority)
        {
          // TODO: It would be super nice if we prepended this instead if it came from a timer
          first_execute.append_active(workitem);
          // std::cout << "append_active _nesting_level = " << parent->_nesting_level << std::endl;
        }
        else
        {
          for(auto *p = workqueue.get(); p != nullptr; p = p->next.get())
          {
            if(p->nesting_level == parent->_nesting_level)
            {
              // TODO: It would be super nice if we prepended this instead if it came from a timer
              p->append_active(workitem);
              // std::cout << "append_active _nesting_level = " << parent->_nesting_level << std::endl;
              break;
            }
          }
        }
#endif
      }

#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
      std::cout << "*** DTP submits work item " << workitem << std::endl;
#endif
      const auto active_work_items = total_submitted_workitems.fetch_add(1, std::memory_order_relaxed) + 1;
      if(!defer_pool_wake)
      {
        {
          threadpool_guard gg(threadpool_lock);
          if(threadpool_active.count == 0 && threadpool_sleeping.count == 0)
          {
            _add_thread(gg);
          }
          else if(threadpool_sleeping.count > 0 && active_work_items > threadpool_active.count)
          {
            // Try to wake the most recently slept first
            auto *t = threadpool_sleeping.back;
            auto now = std::chrono::steady_clock::now();
            for(size_t n = std::min(active_work_items - threadpool_active.count, threadpool_sleeping.count); n > 0; n--)
            {
              t->last_did_work = now;  // prevent reap
              t->cond.notify_one();
              t = t->_prev;
            }
          }
        }
      }
#endif
    }
  }

  inline result<void> global_dynamic_thread_pool_impl::submit(dynamic_thread_pool_group_impl_guard &g, dynamic_thread_pool_group_impl *group,
                                                              span<dynamic_thread_pool_group::work_item *> work) noexcept
  {
    try
    {
      if(work.empty())
      {
        return success();
      }
      for(auto *i : work)
      {
        if(i->_parent.load(std::memory_order_relaxed) != nullptr)
        {
          return errc::address_in_use;
        }
      }
      auto uninit = make_scope_exit([&]() noexcept {
        for(auto *i : work)
        {
          _remove_from_list(group->_work_items_active, i);
#if defined(_WIN32)
          if(nullptr != i->_internaltimerh)
          {
            CloseThreadpoolTimer((PTP_TIMER) i->_internaltimerh);
            i->_internaltimerh = nullptr;
          }
          if(nullptr != i->_internalworkh)
          {
            CloseThreadpoolWork((PTP_WORK) i->_internalworkh);
            i->_internalworkh = nullptr;
          }
#endif
          i->_parent.store(nullptr, std::memory_order_release);
        }
      });
      for(auto *i : work)
      {
        deadline d(std::chrono::seconds(0));
        i->_parent.store(group, std::memory_order_release);
        i->_nextwork.store(i->next(d), std::memory_order_release);
        if(-1 == i->_nextwork.load(std::memory_order_acquire))
        {
          _append_to_list(group->_work_items_done, i);
        }
        else
        {
#if defined(_WIN32)
          i->_internalworkh = CreateThreadpoolWork(_win32_worker_thread_callback, i, group->_grouph);
          if(nullptr == i->_internalworkh)
          {
            return win32_error();
          }
#endif
          OUTCOME_TRY(_prepare_work_item_delay(i, group->_grouph, d));
          _prepend_to_list(group->_work_items_active, i);
        }
      }
      uninit.release();
      g.unlock();
      {
        for(auto *i : work)
        {
#if !LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD && !defined(_WIN32)
          group->_newly_added_active_work_items++;
          group->_active_work_items_remaining++;
#endif
          _submit_work_item(true, i, i != work.back());
        }
      }
      g.lock();
      return success();
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

  inline void global_dynamic_thread_pool_impl::_work_item_done(dynamic_thread_pool_group_impl_guard &g, dynamic_thread_pool_group::work_item *i) noexcept
  {
    (void) g;
    // std::cout << "*** _work_item_done " << i << std::endl;
    auto *parent = i->_parent.load(std::memory_order_relaxed);
    _remove_from_list(parent->_work_items_active, i);
    _append_to_list(parent->_work_items_done, i);
#if defined(_WIN32)
    if(i->_internalworkh_inuse > 0)
    {
      i->_internalworkh_inuse = 2;
    }
    else
    {
      if(i->_internaltimerh != nullptr)
      {
        CloseThreadpoolTimer((PTP_TIMER) i->_internaltimerh);
        i->_internaltimerh = nullptr;
      }
      if(i->_internalworkh != nullptr)
      {
        CloseThreadpoolWork((PTP_WORK) i->_internalworkh);
        i->_internalworkh = nullptr;
      }
    }
#endif
    if(parent->_work_items_active.count == 0)
    {
      i = nullptr;
      auto *v = parent->_work_items_done.front, *n = v;
      for(; v != nullptr; v = n)
      {
        v->_parent.store(nullptr, std::memory_order_release);
        v->_nextwork.store(-1, std::memory_order_release);
        n = v->_next;
      }
      n = v = parent->_work_items_done.front;
      parent->_work_items_done.front = parent->_work_items_done.back = nullptr;
      parent->_work_items_done.count = 0;
      parent->_stopping.store(false, std::memory_order_release);
      parent->_completing.store(true, std::memory_order_release);  // cause submissions to enter _work_items_delayed
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
      std::cout << "*** DTP executes group_complete for group " << parent << std::endl;
#endif
      for(; v != nullptr; v = n)
      {
        n = v->_next;
        v->group_complete(parent->_abnormal_completion_cause);
      }
      parent->_stopped.store(true, std::memory_order_release);
      parent->_completing.store(false, std::memory_order_release);  // cease submitting to _work_items_delayed
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
      std::cout << "*** DTP group_complete done for group " << parent << ". _work_items_delayed.count = " << parent->_work_items_delayed.count << std::endl;
#endif
      if(parent->_work_items_delayed.count > 0)
      {
        /* If there are waits on this group to complete, forward progress those now.
         */
        while(parent->_waits.load(std::memory_order_relaxed) > 0)
        {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
          std::cout << "*** DTP group_complete blocks on waits for group " << parent << std::endl;
#endif
          g.unlock();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          g.lock();
        }
        // Now submit all delayed work
        while(parent->_work_items_delayed.count > 0)
        {
          i = parent->_work_items_delayed.front;
          _remove_from_list(parent->_work_items_delayed, i);
          auto r = submit(g, parent, {&i, 1});
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_PRINTING
          std::cout << "*** DTP group_complete submits delayed work item " << i << " for group " << parent << " which saw error ";
          if(r)
          {
            std::cout << "none" << std::endl;
          }
          else
          {
            std::cout << r.error().message() << std::endl;
          }
#endif
          if(!r)
          {
            parent->_work_items_delayed = {};
            (void) stop(g, parent, std::move(r));
            break;
          }
        }
      }
    }
  }

  inline result<void> global_dynamic_thread_pool_impl::stop(dynamic_thread_pool_group_impl_guard &g, dynamic_thread_pool_group_impl *group,
                                                            result<void> err) noexcept
  {
    (void) g;
    if(group->_abnormal_completion_cause)
    {
      group->_abnormal_completion_cause = std::move(err);
    }
    group->_stopping.store(true, std::memory_order_release);
    return success();
  }


  inline result<void> global_dynamic_thread_pool_impl::wait(dynamic_thread_pool_group_impl_guard &g, bool reap, dynamic_thread_pool_group_impl *group,
                                                            deadline d) noexcept
  {
    LLFIO_DEADLINE_TO_SLEEP_INIT(d);
    if(!d || d.nsecs > 0)
    {
      /* To ensure forward progress, we need to gate new waits during delayed work submission.
      Otherwise waits may never exit if the window where _work_items_active.count == 0 is
      missed.
      */
      while(group->_work_items_delayed.count > 0)
      {
        g.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        g.lock();
      }
      group->_waits.fetch_add(1, std::memory_order_release);
      auto unwaitcount = make_scope_exit([&]() noexcept { group->_waits.fetch_sub(1, std::memory_order_release); });
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
      while(group->_work_items_active.count > 0)
      {
        LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d);
        dispatch_time_t timeout = DISPATCH_TIME_FOREVER;
        if(d)
        {
          std::chrono::nanoseconds duration;
          LLFIO_DEADLINE_TO_PARTIAL_TIMEOUT(duration, d);
          timeout = dispatch_time(DISPATCH_TIME_NOW, duration.count());
        }
        g.unlock();
        dispatch_group_wait(group->_grouph, timeout);
        g.lock();
        // if(1 == group->_work_items_active.count)
        //{
        //  std::cout << "*** wait item remaining is " << group->_work_items_active.front << std::endl;
        //  std::this_thread::sleep_for(std::chrono::seconds(1));
        //}
      }
#elif defined(_WIN32)
      auto &tls = detail::global_dynamic_thread_pool_thread_local_state();
      if(tls.current_callback_instance != nullptr)
      {
        // I am being called from within a thread worker. Tell
        // the thread pool that I am not going to exit promptly.
        CallbackMayRunLong(tls.current_callback_instance);
      }
      // Is this a cancellation?
      if(group->_stopping.load(std::memory_order_relaxed))
      {
        while(group->_work_items_active.count > 0)
        {
          auto *i = group->_work_items_active.front;
          if(nullptr != i->_internalworkh)
          {
            if(0 == i->_internalworkh_inuse)
            {
              i->_internalworkh_inuse = 1;
            }
            g.unlock();
            WaitForThreadpoolWorkCallbacks((PTP_WORK) i->_internalworkh, true);
            g.lock();
            if(i->_internalworkh_inuse == 2)
            {
              if(nullptr != i->_internalworkh)
              {
                CloseThreadpoolWork((PTP_WORK) i->_internalworkh);
                i->_internalworkh = nullptr;
              }
              if(nullptr != i->_internaltimerh)
              {
                CloseThreadpoolTimer((PTP_TIMER) i->_internaltimerh);
                i->_internaltimerh = nullptr;
              }
            }
            i->_internalworkh_inuse = 0;
          }
          if(nullptr != i->_internaltimerh)
          {
            if(0 == i->_internalworkh_inuse)
            {
              i->_internalworkh_inuse = 1;
            }
            g.unlock();
            WaitForThreadpoolTimerCallbacks((PTP_TIMER) i->_internaltimerh, true);
            g.lock();
            if(i->_internalworkh_inuse == 2)
            {
              if(nullptr != i->_internalworkh)
              {
                CloseThreadpoolWork((PTP_WORK) i->_internalworkh);
                i->_internalworkh = nullptr;
              }
              if(nullptr != i->_internaltimerh)
              {
                CloseThreadpoolTimer((PTP_TIMER) i->_internaltimerh);
                i->_internaltimerh = nullptr;
              }
            }
            i->_internalworkh_inuse = 0;
          }
          if(group->_work_items_active.count > 0 && group->_work_items_active.front == i)
          {
            // This item got cancelled before it started
            _work_item_done(g, group->_work_items_active.front);
          }
        }
        assert(!group->_stopping.load(std::memory_order_relaxed));
      }
      else if(!d)
      {
        while(group->_work_items_active.count > 0)
        {
          auto *i = group->_work_items_active.front;
          if(nullptr != i->_internalworkh)
          {
            if(0 == i->_internalworkh_inuse)
            {
              i->_internalworkh_inuse = 1;
            }
            g.unlock();
            WaitForThreadpoolWorkCallbacks((PTP_WORK) i->_internalworkh, false);
            g.lock();
            if(i->_internalworkh_inuse == 2)
            {
              if(nullptr != i->_internalworkh)
              {
                CloseThreadpoolWork((PTP_WORK) i->_internalworkh);
                i->_internalworkh = nullptr;
              }
              if(nullptr != i->_internaltimerh)
              {
                CloseThreadpoolTimer((PTP_TIMER) i->_internaltimerh);
                i->_internaltimerh = nullptr;
              }
            }
            i->_internalworkh_inuse = 0;
          }
          if(nullptr != i->_internaltimerh)
          {
            if(0 == i->_internalworkh_inuse)
            {
              i->_internalworkh_inuse = 1;
            }
            g.unlock();
            WaitForThreadpoolTimerCallbacks((PTP_TIMER) i->_internaltimerh, false);
            g.lock();
            if(i->_internalworkh_inuse == 2)
            {
              if(nullptr != i->_internalworkh)
              {
                CloseThreadpoolWork((PTP_WORK) i->_internalworkh);
                i->_internalworkh = nullptr;
              }
              if(nullptr != i->_internaltimerh)
              {
                CloseThreadpoolTimer((PTP_TIMER) i->_internaltimerh);
                i->_internaltimerh = nullptr;
              }
            }
            i->_internalworkh_inuse = 0;
          }
        }
      }
      else
      {
        while(group->_work_items_active.count > 0)
        {
          LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d);
          g.unlock();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          g.lock();
        }
      }
#else
      while(group->_work_items_active.count > 0)
      {
        LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d);
        g.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        g.lock();
      }
#endif
    }
    if(group->_work_items_active.count > 0)
    {
      return errc::timed_out;
    }
    if(reap)
    {
      return std::move(group->_abnormal_completion_cause);
    }
    return success();
  }

  inline void global_dynamic_thread_pool_impl::_timerthread(dynamic_thread_pool_group::work_item *workitem, threadh_type /*unused*/)
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    assert(workitem->_nextwork.load(std::memory_order_relaxed) != -1);
    assert(workitem->_has_timer_set());
    auto *parent = workitem->_parent.load(std::memory_order_relaxed);
    // std::cout << "*** _timerthread " << workitem << std::endl;
    if(parent->_stopping.load(std::memory_order_relaxed))
    {
      dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
      _work_item_done(g, workitem);
      return;
    }
    if(workitem->_has_timer_set_relative())
    {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
      auto now = std::chrono::steady_clock::now();
      if(workitem->_timepoint1 - now > std::chrono::seconds(0))
      {
        // Timer fired short, so schedule it again
        _submit_work_item(false, workitem, false);
        return;
      }
#endif
      workitem->_timepoint1 = {};
    }
    if(workitem->_has_timer_set_absolute())
    {
#if LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD
      auto now = std::chrono::system_clock::now();
      if(workitem->_timepoint2 - now > std::chrono::seconds(0))
      {
        // Timer fired short, so schedule it again
        _submit_work_item(false, workitem, false);
        return;
      }
#endif
      workitem->_timepoint2 = {};
    }
    assert(!workitem->_has_timer_set());
    if(workitem->_nextwork.load(std::memory_order_acquire) == 0)
    {
      deadline d(std::chrono::seconds(0));
      workitem->_nextwork.store(workitem->next(d), std::memory_order_release);
      auto r2 = _prepare_work_item_delay(workitem, parent->_grouph, d);
      if(!r2)
      {
        dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
        (void) stop(g, parent, std::move(r2));
        _work_item_done(g, workitem);
        return;
      }
      if(-1 == workitem->_nextwork.load(std::memory_order_relaxed))
      {
        dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
        _work_item_done(g, workitem);
        return;
      }
      _submit_work_item(false, workitem, false);
      return;
    }
    _submit_work_item(false, workitem, false);
  }

  // Worker thread entry point
  inline void global_dynamic_thread_pool_impl::_workerthread(dynamic_thread_pool_group::work_item *workitem, threadh_type selfthreadh)
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    //{
    //  _lock_guard g(parent->_lock);
    //  std::cout << "*** _workerthread " << workitem << " begins with work " << workitem->_nextwork.load(std::memory_order_relaxed) << std::endl;
    //}
    assert(workitem->_nextwork.load(std::memory_order_relaxed) != -1);
    assert(workitem->_nextwork.load(std::memory_order_relaxed) != 0);
    auto *parent = workitem->_parent.load(std::memory_order_relaxed);
    if(parent->_stopping.load(std::memory_order_relaxed))
    {
      dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
      _work_item_done(g, workitem);
      return;
    }
    auto &tls = detail::global_dynamic_thread_pool_thread_local_state();
    auto old_thread_local_state = tls;
    tls.workitem = workitem;
    tls.current_callback_instance = selfthreadh;
    tls.nesting_level = parent->_nesting_level + 1;
    auto r = (*workitem)(workitem->_nextwork.load(std::memory_order_acquire));
    workitem->_nextwork.store(0, std::memory_order_release);  // call next() next time
    tls = old_thread_local_state;
    // std::cout << "*** _workerthread " << workitem << " ends with work " << workitem->_nextwork << std::endl;
    if(!r)
    {
      dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
      (void) stop(g, parent, std::move(r));
      _work_item_done(g, workitem);
      workitem = nullptr;
    }
    else if(parent->_stopping.load(std::memory_order_relaxed))
    {
      dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
      _work_item_done(g, workitem);
    }
    else
    {
      deadline d(std::chrono::seconds(0));
      workitem->_nextwork.store(workitem->next(d), std::memory_order_release);
      auto r2 = _prepare_work_item_delay(workitem, parent->_grouph, d);
      if(!r2)
      {
        dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
        (void) stop(g, parent, std::move(r2));
        _work_item_done(g, workitem);
        return;
      }
      if(-1 == workitem->_nextwork.load(std::memory_order_relaxed))
      {
        dynamic_thread_pool_group_impl_guard g(parent->_lock);  // lock group
        _work_item_done(g, workitem);
        return;
      }
      _submit_work_item(false, workitem, false);
    }
  }
}  // namespace detail


/****************************************** io_aware_work_item *********************************************/

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC dynamic_thread_pool_group::io_aware_work_item::io_aware_work_item(span<io_handle_awareness> hs)
    : _handles([](span<io_handle_awareness> hs) -> span<io_handle_awareness> {
      float all = 0;
      for(auto &i : hs)
      {
        all += i.reads + i.writes + i.barriers;
      }
      for(auto &i : hs)
      {
        if(all == 0.0f)
        {
          i.reads = i.writes = 0.5f;
          i.barriers = 0.0f;
        }
        else
        {
          i.reads /= all;
          i.writes /= all;
          i.barriers /= all;
        }
      }
      auto &impl = detail::global_dynamic_thread_pool();
      detail::global_dynamic_thread_pool_impl::io_aware_work_item_handles_guard g(impl.io_aware_work_item_handles_lock);
      for(auto &h : hs)
      {
        if(!h.h->is_seekable())
        {
          throw std::runtime_error("Supplied handle is not seekable");
        }
        auto *fh = static_cast<file_handle *>(h.h);
        auto unique_id = fh->unique_id();
        auto it = impl.io_aware_work_item_handles.find(unique_id);
        if(it == impl.io_aware_work_item_handles.end())
        {
          it = impl.io_aware_work_item_handles.emplace(unique_id, detail::global_dynamic_thread_pool_impl::io_aware_work_item_statfs{}).first;
          auto r = it->second.statfs.fill(*fh, statfs_t::want::iosinprogress | statfs_t::want::iosbusytime);
          if(!r || it->second.statfs.f_iosinprogress == (uint32_t) -1)
          {
            impl.io_aware_work_item_handles.erase(it);
            if(!r)
            {
              r.value();
            }
            throw std::runtime_error("statfs::f_iosinprogress unavailable for supplied handle");
          }
          it->second.last_updated = std::chrono::steady_clock::now();
        }
        it->second.refcount++;
        h._internal = &*it;
      }
      return hs;
    }(hs))
{
  LLFIO_LOG_FUNCTION_CALL(this);
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC dynamic_thread_pool_group::io_aware_work_item::~io_aware_work_item()
{
  LLFIO_LOG_FUNCTION_CALL(this);
  auto &impl = detail::global_dynamic_thread_pool();
  detail::global_dynamic_thread_pool_impl::io_aware_work_item_handles_guard g(impl.io_aware_work_item_handles_lock);
  using value_type = decltype(impl.io_aware_work_item_handles)::value_type;
  for(auto &h : _handles)
  {
    auto *i = (value_type *) h._internal;
    if(0 == --i->second.refcount)
    {
      impl.io_aware_work_item_handles.erase(i->first);
    }
  }
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC intptr_t dynamic_thread_pool_group::io_aware_work_item::next(deadline &d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  {
    auto &impl = detail::global_dynamic_thread_pool();
    auto now = std::chrono::steady_clock::now();
    detail::global_dynamic_thread_pool_impl::io_aware_work_item_handles_guard g(impl.io_aware_work_item_handles_lock);
    using value_type = decltype(impl.io_aware_work_item_handles)::value_type;
    for(auto &h : _handles)
    {
      auto *i = (value_type *) h._internal;
      if(std::chrono::duration_cast<std::chrono::milliseconds>(now - i->second.last_updated) >= std::chrono::milliseconds(100))
      {
        // auto old_iosinprogress = i->second.statfs.f_iosinprogress;
        auto elapsed = now - i->second.last_updated;
        (void) i->second.statfs.fill(*h.h, statfs_t::want::iosinprogress | statfs_t::want::iosbusytime);
        i->second.last_updated = now;

        if(elapsed > std::chrono::seconds(5))
        {
          i->second.average_busy = i->second.statfs.f_iosbusytime;
          i->second.average_queuedepth = (float) i->second.statfs.f_iosinprogress;
        }
        else
        {
          i->second.average_busy = (i->second.average_busy * 0.9f) + (i->second.statfs.f_iosbusytime * 0.1f);
          i->second.average_queuedepth = (i->second.average_queuedepth * 0.9f) + (i->second.statfs.f_iosinprogress * 0.1f);
        }
        if(i->second.average_busy < this->max_iosbusytime && i->second.average_queuedepth < this->min_iosinprogress)
        {
          i->second.default_deadline = std::chrono::seconds(0);  // remove pacing
        }
        else if(i->second.average_queuedepth > this->max_iosinprogress)
        {
          if(0 == i->second.default_deadline.nsecs)
          {
            i->second.default_deadline = std::chrono::milliseconds(1);  // start with 1ms, it'll reduce from there if needed
          }
          else if((i->second.default_deadline.nsecs >> 4) > 0)
          {
            i->second.default_deadline.nsecs += i->second.default_deadline.nsecs >> 4;
          }
          else
          {
            i->second.default_deadline.nsecs++;
          }
        }
        else if(i->second.average_queuedepth < this->min_iosinprogress)
        {
          if(i->second.default_deadline.nsecs > (i->second.default_deadline.nsecs >> 4) && (i->second.default_deadline.nsecs >> 4) > 0)
          {
            i->second.default_deadline.nsecs -= i->second.default_deadline.nsecs >> 4;
          }
          else if(i->second.default_deadline.nsecs > 1)
          {
            i->second.default_deadline.nsecs--;
          }
        }
      }
      if(d.nsecs < i->second.default_deadline.nsecs)
      {
        d = i->second.default_deadline;
      }
    }
  }
  return io_aware_next(d);
}


LLFIO_V2_NAMESPACE_END
