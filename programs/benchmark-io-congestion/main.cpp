/* Test i/o congestion mitigation strategies
(C) 2021 Niall Douglas <http://www.nedproductions.biz/> (6 commits)
File Created: Mar 2021


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

//! Seconds to run the benchmark
static constexpr unsigned BENCHMARK_DURATION = 10;
//! Maximum work items to create
static constexpr unsigned MAX_WORK_ITEMS = 4096;
// Size of buffer to SHA256
static constexpr unsigned SHA256_BUFFER_SIZE = 4 * 1024;  // 64Kb
// Size of test file
static constexpr unsigned long long TEST_FILE_SIZE = 4ULL * 1024 * 1024 * 1024;  // 4Gb

#include "../../include/llfio/llfio.hpp"

#include "quickcpplib/algorithm/small_prng.hpp"

#include <cfloat>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
#include <tuple>
#include <vector>

#if __has_include("../asio/asio/include/asio.hpp")
#define ENABLE_ASIO 1
#if defined(__clang__) && defined(_MSC_VER)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-include"
#endif
#include "../asio/asio/include/asio.hpp"
#if defined(__clang__) && defined(_MSC_VER)
#pragma clang diagnostic pop
#endif
#endif

namespace llfio = LLFIO_V2_NAMESPACE;

struct benchmark_results
{
  std::chrono::microseconds duration;
  llfio::utils::process_memory_usage memory_usage;
};

inline QUICKCPPLIB_NOINLINE void memcpy_s(llfio::byte *dest, const llfio::byte *s, size_t len)
{
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  while(len >= 4 * sizeof(__m128i))
  {
    __m128i a = *(const __m128i *__restrict) s;
    s += sizeof(__m128i);
    __m128i b = *(const __m128i *__restrict) s;
    s += sizeof(__m128i);
    __m128i c = *(const __m128i *__restrict) s;
    s += sizeof(__m128i);
    __m128i d = *(const __m128i *__restrict) s;
    s += sizeof(__m128i);
    *(__m128i * __restrict) dest = a;
    dest += sizeof(__m128i);
    *(__m128i * __restrict) dest = b;
    dest += sizeof(__m128i);
    *(__m128i * __restrict) dest = c;
    dest += sizeof(__m128i);
    *(__m128i * __restrict) dest = d;
    dest += sizeof(__m128i);
    len -= 4 * sizeof(__m128i);
  }
  while(len >= sizeof(__m128i))
  {
    *(__m128i * __restrict) dest = *(const __m128i *__restrict) s;
    dest += sizeof(__m128i);
    s += sizeof(__m128i);
    len -= sizeof(__m128i);
  }
#endif
  while(len >= sizeof(uint64_t))
  {
    *(volatile uint64_t * __restrict) dest = *(const uint64_t *__restrict) s;
    dest += sizeof(uint64_t);
    s += sizeof(uint64_t);
    len -= sizeof(uint64_t);
  }
  if(len >= sizeof(uint32_t))
  {
    *(volatile uint32_t * __restrict) dest = *(const uint32_t *__restrict) s;
    dest += sizeof(uint32_t);
    s += sizeof(uint32_t);
    len -= sizeof(uint32_t);
  }
  if(len >= sizeof(uint16_t))
  {
    *(volatile uint16_t * __restrict) dest = *(const uint16_t *__restrict) s;
    dest += sizeof(uint16_t);
    s += sizeof(uint16_t);
    len -= sizeof(uint16_t);
  }
  if(len >= sizeof(uint8_t))
  {
    *(volatile uint8_t * __restrict) dest = *(const uint8_t *__restrict) s;
    dest += sizeof(uint8_t);
    s += sizeof(uint8_t);
    len -= sizeof(uint8_t);
  }
}

struct llfio_runner_unpaced
{
  std::atomic<bool> cancel{false};
  llfio::dynamic_thread_pool_group_ptr group = llfio::make_dynamic_thread_pool_group().value();
  std::vector<llfio::dynamic_thread_pool_group::work_item *> workitems;

  llfio_runner_unpaced(llfio::io_handle * /*unused*/) {}
  ~llfio_runner_unpaced()
  {
    for(auto *p : workitems)
    {
      delete p;
    }
  }
  template <class F> void add_workitem(F &&f)
  {
    struct workitem final : public llfio::dynamic_thread_pool_group::work_item
    {
      llfio_runner_unpaced *parent;
      F f;
      workitem(llfio_runner_unpaced *_parent, F &&_f)
          : parent(_parent)
          , f(std::move(_f))
      {
      }
      virtual intptr_t next(llfio::deadline & /*unused*/) noexcept override { return parent->cancel.load(std::memory_order_relaxed) ? -1 : 1; }
      virtual llfio::result<void> operator()(intptr_t /*unused*/) noexcept override
      {
        f();
        return llfio::success();
      }
    };
    workitems.push_back(new workitem(this, std::move(f)));
  }
  benchmark_results run(unsigned seconds)
  {
    group->submit(workitems).value();
    auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    auto memusage = llfio::utils::current_process_memory_usage().value();
    cancel.store(true, std::memory_order_release);
    group->wait().value();
    auto end = std::chrono::steady_clock::now();
    return {std::chrono::duration_cast<std::chrono::microseconds>(end - begin), memusage};
  }
};

struct llfio_runner_paced
{
  std::atomic<bool> cancel{false};
  llfio::dynamic_thread_pool_group_ptr group = llfio::make_dynamic_thread_pool_group().value();
  llfio::dynamic_thread_pool_group::io_aware_work_item::io_handle_awareness awareness;
  std::vector<llfio::dynamic_thread_pool_group::work_item *> workitems;
  std::atomic<int64_t> last_pace{0};

  llfio_runner_paced(llfio::io_handle *h)
      : awareness{h, 1.0f /* 100% reads */}
  {
  }
  ~llfio_runner_paced()
  {
    for(auto *p : workitems)
    {
      delete p;
    }
  }
  template <class F> void add_workitem(F &&f)
  {
    struct workitem final : public llfio::dynamic_thread_pool_group::io_aware_work_item
    {
      llfio_runner_paced *parent;
      F f;
      workitem(llfio_runner_paced *_parent, F &&_f)
          : llfio::dynamic_thread_pool_group::io_aware_work_item({&_parent->awareness, 1})
          , parent(_parent)
          , f(std::move(_f))
      {
      }
      virtual intptr_t io_aware_next(llfio::deadline &d) noexcept override
      {
#if 1
        auto last_pace = parent->last_pace.load(std::memory_order_relaxed);
        if(last_pace != d.nsecs)
        {
          parent->last_pace.store(d.nsecs, std::memory_order_relaxed);
          std::cout << "Pacing work by milliseconds " << (d.nsecs / 1000.0) << std::endl;
        }
#endif
        return parent->cancel.load(std::memory_order_relaxed) ? -1 : 1;
      }
      virtual llfio::result<void> operator()(intptr_t /*unused*/) noexcept override
      {
        f();
        return llfio::success();
      }
    };
    workitems.push_back(new workitem(this, std::move(f)));
  }
  benchmark_results run(unsigned seconds)
  {
    group->submit(workitems).value();
    auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    auto memusage = llfio::utils::current_process_memory_usage().value();
    cancel.store(true, std::memory_order_release);
    group->wait().value();
    auto end = std::chrono::steady_clock::now();
    return {std::chrono::duration_cast<std::chrono::microseconds>(end - begin), memusage};
  }
};


#if ENABLE_ASIO
struct asio_runner
{
  std::atomic<bool> cancel{false};
  asio::io_context ctx;

  asio_runner(llfio::io_handle * /*unused*/) {}
  template <class F> struct C
  {
    asio_runner *parent;
    F f;
    C(asio_runner *_parent, F &&_f)
        : parent(_parent)
        , f(std::move(_f))
    {
    }
    void operator()() const
    {
      f();
      if(!parent->cancel.load(std::memory_order_relaxed))
      {
        parent->ctx.post(*this);
      }
    }
  };
  template <class F> void add_workitem(F &&f) { ctx.post(C<F>(this, std::move(f))); }
  benchmark_results run(unsigned seconds)
  {
    std::vector<std::thread> threads;
    auto do_cleanup = [&]() noexcept {
      cancel.store(true, std::memory_order_release);
      for(auto &i : threads)
      {
        i.join();
      }
    };
    auto cleanup = llfio::make_scope_exit(do_cleanup);
    for(size_t n = 0; n < MAX_WORK_ITEMS; n++)
    {
      try
      {
        threads.emplace_back([&] { ctx.run(); });
      }
      catch(...)
      {
        std::cerr << "Thread creation failed at number " << n << std::endl;
        throw;
      }
    }
    auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    auto memusage = llfio::utils::current_process_memory_usage().value();
    cleanup.release();
    do_cleanup();
    auto end = std::chrono::steady_clock::now();
    return {std::chrono::duration_cast<std::chrono::microseconds>(end - begin), memusage};
  }
};
#endif

template <class Runner> void benchmark(llfio::mapped_file_handle &maph, const char *name)
{
  if(name != nullptr)
  {
    std::cout << "\nBenchmarking " << name << " ..." << std::endl;
  }
  else
  {
    std::cout << "\nWarming up ..." << std::endl;
  }
  struct shared_t
  {
    llfio::span<llfio::byte> ioregion;
    std::atomic<unsigned> concurrency{0};
    std::atomic<unsigned> max_concurrency{0};
  };
  struct worker
  {
    shared_t *shared;
    QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng rand;
    QUICKCPPLIB_NAMESPACE::algorithm::hash::sha256_hash::result_type hash;
    uint64_t count{0};

    void operator()()
    {
      auto concurrency = shared->concurrency.fetch_add(1, std::memory_order_relaxed) + 1;
      if(concurrency > shared->max_concurrency.load(std::memory_order_relaxed))
      {
        shared->max_concurrency.store(concurrency, std::memory_order_relaxed);
      }
#if 1
      auto offset = rand() % (TEST_FILE_SIZE - SHA256_BUFFER_SIZE - 1);
#else
      auto offset = rand() & (TEST_FILE_SIZE - 1);
      offset &= ~(SHA256_BUFFER_SIZE - 1);
#endif
      hash = QUICKCPPLIB_NAMESPACE::algorithm::hash::sha256_hash::hash(shared->ioregion.data() + offset, SHA256_BUFFER_SIZE);
      count++;
      shared->concurrency.fetch_sub(1, std::memory_order_relaxed);
    }
    explicit worker(shared_t *_shared, uint32_t mythreadidx)
        : shared(_shared)
        , rand(mythreadidx)
    {
    }
  };
  std::vector<worker> workers;
  struct result_t
  {
    size_t items;
    double throughput;
    size_t paged_in;
    unsigned max_concurrency;
  };
  std::vector<result_t> results;
  for(size_t items = 1; items <= MAX_WORK_ITEMS; items <<= 1)
  {
    if(name == nullptr && items != 16)
    {
      continue;
    }
    shared_t shared{maph.map().as_span()};
    workers.clear();
    for(uint32_t n = 0; n < items; n++)
    {
      workers.emplace_back(&shared, n);
    }
    Runner runner(&maph);
    for(auto &i : workers)
    {
      runner.add_workitem([&] { i(); });
    }
    auto out = runner.run(BENCHMARK_DURATION);
    uint64_t total = 0;
    for(auto &i : workers)
    {
      total += i.count;
    }
    results.push_back({items, 1000000.0 * total / out.duration.count(), out.memory_usage.total_address_space_paged_in, shared.max_concurrency});
    std::cout << "   For " << results.back().items << " work items got " << results.back().throughput << " SHA256 hashes/sec with "
              << (results.back().items * SHA256_BUFFER_SIZE / 1024.0 / 1024.0) << " Mb working set, " << results.back().max_concurrency
              << " maximum concurrency, and " << (results.back().paged_in / 1024.0 / 1024.0) << " Mb paged in." << std::endl;
    // std::cout << "      " << (out.memory_usage.total_address_space_in_use / 1024.0 / 1024.0) << ","
    //          << (out.memory_usage.total_address_space_paged_in / 1024.0 / 1024.0) << "," << (out.memory_usage.private_committed / 1024.0 / 1024.0) << ","
    //          << (out.memory_usage.private_paged_in / 1024.0 / 1024.0) << std::endl;
  }
  if(name != nullptr)
  {
    std::ofstream out(std::string(name) + "_results.csv");
    out << R"("Work items","SHA256 hashes/sec","Working set","Max concurrency","Paged in")";
    for(auto &i : results)
    {
      out << "\n"
          << i.items << "," << i.throughput << "," << (i.items * SHA256_BUFFER_SIZE / 1024.0 / 1024.0) << "," << i.max_concurrency << ","
          << (i.paged_in / 1024.0 / 1024.0);
    }
    out << std::endl;
  }
}

int main(int argc, char *argv[])
{
  try
  {
    llfio::path_handle where;
    if(argc > 1)
    {
      where = llfio::path_handle::path(argv[1]).value();
    }
    else
    {
      where = llfio::path_handle::path(llfio::filesystem::current_path()).value();
    }
    llfio::mapped_file_handle fileh;
    if(auto fileh_ = llfio::mapped_file_handle::mapped_file(TEST_FILE_SIZE, where, "testfile"))
    {
      fileh = std::move(fileh_).value();
      if(fileh.maximum_extent().value() != TEST_FILE_SIZE)
      {
        fileh.close().value();
      }
#if 0
      else
      {
        std::cout << "Prefaulting " << (TEST_FILE_SIZE / 1024.0 / 1024.0) << " Mb test file at " << fileh.current_path().value() << " ..." << std::endl;
        std::vector<llfio::byte> buffer(SHA256_BUFFER_SIZE);
        for(size_t n = 0; n < TEST_FILE_SIZE; n += SHA256_BUFFER_SIZE)
        {
          memcpy_s(buffer.data(), fileh.address() + n, SHA256_BUFFER_SIZE);
        }
      }
#endif
    }
    if(!fileh.is_valid())
    {
      fileh = llfio::mapped_file_handle::mapped_file(TEST_FILE_SIZE, where, "testfile", llfio::mapped_file_handle::mode::write,
                                                     llfio::mapped_file_handle::creation::always_new, llfio::mapped_file_handle::caching::reads_and_metadata)
              .value();
      std::cout << "Writing " << (TEST_FILE_SIZE / 1024.0 / 1024.0) << " Mb test file at " << fileh.current_path().value() << " ..." << std::endl;
      fileh.truncate(TEST_FILE_SIZE).value();
      memset(fileh.address(), 0xff, TEST_FILE_SIZE);
    }

    benchmark<llfio_runner_unpaced>(fileh, nullptr);

#if 0
    {
      std::string llfio_name("llfio unpaced (");
      llfio_name.append(llfio::dynamic_thread_pool_group::implementation_description());
      llfio_name.push_back(')');
      benchmark<llfio_runner_unpaced>(fileh, llfio_name.c_str());
    }
#endif

#if 1
    {
      std::string llfio_name("llfio paced (");
      llfio_name.append(llfio::dynamic_thread_pool_group::implementation_description());
      llfio_name.push_back(')');
      benchmark<llfio_runner_paced>(fileh, llfio_name.c_str());
    }
#endif

#if 0  // ENABLE_ASIO
    benchmark<asio_runner>(fileh, "asio");
#endif

    std::cout << "\nReminder: you may wish to delete " << fileh.current_path().value() << std::endl;
    return 0;
  }
  catch(const std::exception &e)
  {
    std::cerr << "FATAL: " << e.what() << std::endl;
    return 1;
  }
}