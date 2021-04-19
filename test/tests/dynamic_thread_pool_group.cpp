/* Integration test kernel for dynamic_thread_pool_group
(C) 2020 Niall Douglas <http://www.nedproductions.biz/> (2 commits)
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

//#define LLFIO_LOGGING_LEVEL 99

#include "../test_kernel_decl.hpp"

#include <cmath>  // for sqrt

static inline void TestDynamicThreadPoolGroupWorks()
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  // llfio::log_level_guard llg(llfio::log_level::all);
  struct work_item;
  struct shared_state_t
  {
    std::atomic<intptr_t> p{0};
    std::atomic<size_t> concurrency{0}, max_concurrency{0}, group_completes{0};
    std::vector<size_t> executed;
    llfio::dynamic_thread_pool_group_ptr tpg{llfio::make_dynamic_thread_pool_group().value()};
    std::atomic<bool> cancelling{false};
  } shared_state;
  struct work_item final : public llfio::dynamic_thread_pool_group::work_item
  {
    using _base = llfio::dynamic_thread_pool_group::work_item;
    shared_state_t *shared{nullptr};
    std::atomic<bool> within{false};

    work_item() = default;
    explicit work_item(shared_state_t *_shared)
        : shared(_shared)
    {
    }
    work_item(work_item &&o) noexcept
        : _base(std::move(o))
        , shared(o.shared)
    {
    }

    virtual intptr_t next(llfio::deadline &d) noexcept override
    {
      bool expected = false;
      BOOST_CHECK(within.compare_exchange_strong(expected, true));
      (void) d;
      BOOST_CHECK(parent() == shared->tpg.get());
      auto ret = shared->p.fetch_sub(1);
      if(ret < 0)
      {
        ret = -1;
      }
      // std::cout << "   next() returns " << ret << std::endl;
      expected = true;
      BOOST_CHECK(within.compare_exchange_strong(expected, false));
      return ret;
    }
    virtual llfio::result<void> operator()(intptr_t work) noexcept override
    {
      bool expected = false;
      BOOST_CHECK(within.compare_exchange_strong(expected, true));
      auto concurrency = shared->concurrency.fetch_add(1) + 1;
      for(size_t expected_ = shared->max_concurrency; concurrency > expected_;)
      {
        shared->max_concurrency.compare_exchange_weak(expected_, concurrency);
      }
      BOOST_CHECK(parent() == shared->tpg.get());
      BOOST_CHECK(llfio::dynamic_thread_pool_group::current_nesting_level() == 1);
      BOOST_CHECK(llfio::dynamic_thread_pool_group::current_work_item() == this);
      // std::cout << "   () executes " << work << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      auto *executed = (std::atomic<size_t> *) &shared->executed[work];
      executed->fetch_add(1);
      shared->concurrency.fetch_sub(1);
      expected = true;
      BOOST_CHECK(within.compare_exchange_strong(expected, false));
      return llfio::success();
    }
    virtual void group_complete(const llfio::result<void> &cancelled) noexcept override
    {
      bool expected = false;
      BOOST_CHECK(within.compare_exchange_strong(expected, true));
      BOOST_CHECK(parent() == nullptr);
      BOOST_CHECK(shared->cancelling == cancelled.has_error());
      // std::cout << "   group_complete()" << std::endl;
      shared->group_completes.fetch_add(1);
      expected = true;
      BOOST_CHECK(within.compare_exchange_strong(expected, false));
    }
  };
  std::vector<work_item> workitems;
  auto reset = [&](size_t count) {
    workitems.clear();
    shared_state.executed.clear();
    shared_state.executed.resize(count + 1);
    for(size_t n = 0; n < count; n++)
    {
      workitems.emplace_back(&shared_state);
    }
    shared_state.p = (intptr_t) count;
    shared_state.concurrency = 0;
    shared_state.max_concurrency = 0;
    shared_state.group_completes = 0;
  };
  auto submit = [&] {
    auto **wis = (llfio::dynamic_thread_pool_group::work_item **) alloca(sizeof(work_item *) * workitems.size());
    for(size_t n = 0; n < workitems.size(); n++)
    {
      wis[n] = &workitems[n];
    }
    BOOST_CHECK(!shared_state.tpg->stopping());
    BOOST_CHECK(shared_state.tpg->stopped());
    BOOST_CHECK(llfio::dynamic_thread_pool_group::current_nesting_level() == 0);
    BOOST_CHECK(llfio::dynamic_thread_pool_group::current_work_item() == nullptr);
    for(size_t n = 0; n < workitems.size(); n++)
    {
      BOOST_CHECK(workitems[n].parent() == nullptr);
    }
    shared_state.tpg->submit({wis, workitems.size()}).value();
    BOOST_CHECK(!shared_state.tpg->stopping());
    BOOST_CHECK(!shared_state.tpg->stopped());
    for(size_t n = 0; n < workitems.size(); n++)
    {
      BOOST_CHECK(workitems[n].parent() == shared_state.tpg.get());
    }
    BOOST_CHECK(llfio::dynamic_thread_pool_group::current_nesting_level() == 0);
    BOOST_CHECK(llfio::dynamic_thread_pool_group::current_work_item() == nullptr);
  };
  auto check = [&] {
    auto r = shared_state.tpg->wait();
    if(!r)
    {
      std::cerr << "ERROR: wait() reports failure " << r.error().message() << std::endl;
      r.value();
    }
    BOOST_CHECK(!shared_state.tpg->stopping());
    BOOST_CHECK(shared_state.tpg->stopped());
    BOOST_CHECK(llfio::dynamic_thread_pool_group::current_nesting_level() == 0);
    BOOST_CHECK(llfio::dynamic_thread_pool_group::current_work_item() == nullptr);
    for(size_t n = 0; n < workitems.size(); n++)
    {
      BOOST_CHECK(workitems[n].parent() == nullptr);
    }
    BOOST_CHECK(shared_state.group_completes == workitems.size());
    BOOST_CHECK(shared_state.executed[0] == 0);
    if(shared_state.cancelling)
    {
      size_t executed = 0, notexecuted = 0;
      for(size_t n = 1; n <= workitems.size(); n++)
      {
        if(shared_state.executed[n] == 1)
        {
          executed++;
        }
        else
        {
          notexecuted++;
        }
      }
      std::cout << "During cancellation, executed " << executed << " and did not execute " << notexecuted << std::endl;
    }
    else
    {
      for(size_t n = 1; n <= workitems.size(); n++)
      {
        BOOST_CHECK(shared_state.executed[n] == 1);
        if(shared_state.executed[n] != 1)
        {
          std::cout << "shared_state.executed[" << n << "] = " << shared_state.executed[n] << std::endl;
        }
      }
    }
    std::cout << "Maximum concurrency achieved with " << workitems.size() << " work items = " << shared_state.max_concurrency << "\n" << std::endl;
  };
  auto print_exception_throw = llfio::make_scope_fail([]() noexcept { std::cout << "NOTE: Exception throw occurred!" << std::endl; });

  // Test a single work item
  reset(1);
  submit();
  check();

  // Test 10 work items
  reset(10);
  submit();
  check();

  // Test 1000 work items
  reset(1000);
  submit();
  check();

  // Test 1000 work items with stop
  reset(1000);
  submit();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  shared_state.cancelling = true;
  shared_state.tpg->stop().value();
  BOOST_CHECK(shared_state.tpg->stopping());
  auto r = shared_state.tpg->wait();
  BOOST_CHECK(!shared_state.tpg->stopping());
  BOOST_REQUIRE(!r);
  BOOST_CHECK(r.error() == llfio::errc::operation_canceled);
  check();
}

static inline void TestDynamicThreadPoolGroupNestingWorks()
{
  if(std::thread::hardware_concurrency() < 4)
  {
    std::cout << "NOTE: Skipping TestDynamicThreadPoolGroupNestingWorks as hardware concurrency is below 4." << std::endl;
    return;
  }
  namespace llfio = LLFIO_V2_NAMESPACE;
  static constexpr size_t MAX_NESTING = 10;
  static constexpr int COUNT_PER_WORK_ITEM = 1000;
  struct shared_state_t
  {
    std::mutex lock;
    std::unordered_map<uint64_t, size_t> time_bucket;
    llfio::dynamic_thread_pool_group_ptr tpg;
    double stddev{0};
    void calc_stddev()
    {
      stddev = 0;
      uint64_t mean = 0, count = 0;
      for(auto &i : time_bucket)
      {
        mean += i.first * i.second;
        count += i.second;
      }
      mean /= count;
      for(auto &i : time_bucket)
      {
        double diff = (double) abs((int64_t) i.first - (int64_t) mean);
        stddev += diff * diff * i.second;
      }
      stddev /= count;
      stddev = sqrt(stddev);
    }
  } shared_states[MAX_NESTING];
  struct work_item final : public llfio::dynamic_thread_pool_group::work_item
  {
    using _base = llfio::dynamic_thread_pool_group::work_item;
    const size_t nesting{0};
    llfio::span<shared_state_t> shared_states;
    std::atomic<int> count{COUNT_PER_WORK_ITEM};
    std::unique_ptr<work_item> childwi;

    work_item() = default;
    explicit work_item(size_t _nesting, llfio::span<shared_state_t> _shared_states)
        : nesting(_nesting)
        , shared_states(_shared_states)
    {
      if(nesting + 1 < MAX_NESTING)
      {
        childwi = std::make_unique<work_item>(nesting + 1, shared_states);
      }
    }
    work_item(work_item &&o) noexcept
        : _base(std::move(o))
        , nesting(o.nesting)
        , shared_states(o.shared_states)
        , childwi(std::move(o.childwi))
    {
    }

    virtual intptr_t next(llfio::deadline & /*unused*/) noexcept override
    {
      auto ret = count.fetch_sub(1);
      if(ret <= 0)
      {
        ret = -1;
      }
      return ret;
    }
    virtual llfio::result<void> operator()(intptr_t work) noexcept override
    {
      auto supposed_nesting_level = llfio::dynamic_thread_pool_group::current_nesting_level();
      BOOST_CHECK(nesting + 1 == supposed_nesting_level);
      if(nesting + 1 != supposed_nesting_level)
      {
        std::cerr << "current_nesting_level() reports " << supposed_nesting_level << " not " << (nesting + 1) << std::endl;
      }
      uint64_t idx = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      std::lock_guard<std::mutex> g(shared_states[nesting].lock);
      // std::cout << "wi " << this << " nesting " << nesting << " work " << work << std::endl;
      if(COUNT_PER_WORK_ITEM == work && childwi)
      {
        if(!shared_states[nesting].tpg)
        {
          shared_states[nesting].tpg = llfio::make_dynamic_thread_pool_group().value();
        }
        OUTCOME_TRY(shared_states[nesting].tpg->submit(childwi.get()));
      }
      shared_states[nesting].time_bucket[idx]++;
      return llfio::success();
    }
    // virtual void group_complete(const llfio::result<void> &/*unused*/) noexcept override { }
  };
  std::vector<work_item> workitems;
  for(size_t n = 0; n < 100; n++)
  {
    workitems.emplace_back(0, shared_states);
  }
  auto tpg = llfio::make_dynamic_thread_pool_group().value();
  tpg->submit(llfio::span<work_item>(workitems)).value();
  tpg->wait().value();
  for(size_t n = 0; n < MAX_NESTING - 1; n++)
  {
    std::unique_lock<std::mutex> g(shared_states[n].lock);
    while(!shared_states[n].tpg)
    {
      g.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      g.lock();
    }
    g.unlock();
    shared_states[n].tpg->wait().value();
  }
  for(size_t n = 0; n < MAX_NESTING; n++)
  {
    shared_states[n].calc_stddev();
    std::cout << "   Standard deviation for nesting level " << (n + 1) << " was " << shared_states[n].stddev << std::endl;
  }
  BOOST_CHECK(shared_states[MAX_NESTING - 1].stddev < shared_states[MAX_NESTING / 4].stddev * 3 / 4);
}

static inline void TestDynamicThreadPoolGroupIoAwareWorks()
{
  if(getenv("CI") != nullptr)
  {
    return;
  }
  namespace llfio = LLFIO_V2_NAMESPACE;
  static constexpr size_t WORK_ITEMS = 1000;
  static constexpr size_t IO_SIZE = 1 * 65536;
  struct shared_state_t
  {
    llfio::file_handle h;
    llfio::dynamic_thread_pool_group::io_aware_work_item::io_handle_awareness awareness;
    std::atomic<size_t> concurrency{0}, max_concurrency{0};
    std::atomic<uint64_t> current_pacing{0};
  } shared_state;
  struct work_item final : public llfio::dynamic_thread_pool_group::io_aware_work_item
  {
    using _base = llfio::dynamic_thread_pool_group::io_aware_work_item;
    shared_state_t *shared_state;

    work_item() = default;
    explicit work_item(shared_state_t *_shared_state)
        : _base({&_shared_state->awareness, 1})
        , shared_state(_shared_state)
    {
    }
    work_item(work_item &&o) noexcept
        : _base(std::move(o))
        , shared_state(o.shared_state)
    {
    }

    virtual intptr_t io_aware_next(llfio::deadline &d) noexcept override
    {
      shared_state->current_pacing.store(d.nsecs, std::memory_order_relaxed);
      return 1;
    }
    virtual llfio::result<void> operator()(intptr_t /*unused*/) noexcept override
    {
      auto concurrency = shared_state->concurrency.fetch_add(1, std::memory_order_relaxed) + 1;
      for(size_t expected_ = shared_state->max_concurrency; concurrency > expected_;)
      {
        shared_state->max_concurrency.compare_exchange_weak(expected_, concurrency);
      }
      static thread_local std::vector<llfio::byte, llfio::utils::page_allocator<llfio::byte>> buffer(IO_SIZE);
      OUTCOME_TRY(shared_state->h.read((concurrency - 1) * IO_SIZE, {{buffer}}));
      shared_state->concurrency.fetch_sub(1, std::memory_order_relaxed);
      return llfio::success();
    }
    // virtual void group_complete(const llfio::result<void> &/*unused*/) noexcept override { }
  };
  shared_state.h = llfio::file_handle::temp_file({}, llfio::file_handle::mode::write, llfio::file_handle::creation::only_if_not_exist,
                                                 llfio::file_handle::caching::only_metadata)
                   .value();
  shared_state.awareness.h = &shared_state.h;
  shared_state.h.truncate(WORK_ITEMS * IO_SIZE).value();
  {
    auto print_statfs = [](const llfio::file_handle &h, const llfio::statfs_t &statfs) {
      std::cout << "\nFor file " << h.current_path().value() << ":";
      std::cout << "\n fundamental filesystem block size = " << statfs.f_bsize;
      std::cout << "\n optimal transfer block size = " << statfs.f_iosize;
      std::cout << "\n total data blocks in filesystem = " << statfs.f_blocks;
      std::cout << "\n free blocks in filesystem = " << statfs.f_bfree;
      std::cout << "\n free blocks avail to non-superuser = " << statfs.f_bavail;
      std::cout << "\n total file nodes in filesystem = " << statfs.f_files;
      std::cout << "\n free nodes avail to non-superuser = " << statfs.f_ffree;
      std::cout << "\n maximum filename length = " << statfs.f_namemax;
      std::cout << "\n filesystem type name = " << statfs.f_fstypename;
      std::cout << "\n mounted filesystem = " << statfs.f_mntfromname;
      std::cout << "\n directory on which mounted = " << statfs.f_mntonname;
      std::cout << "\n i/o's currently in progress (i.e. queue depth) = " << statfs.f_iosinprogress;
      std::cout << "\n percentage of time spent doing i/o (1.0 = 100%) = " << statfs.f_iosbusytime;
      std::cout << std::endl;
    };
    llfio::statfs_t s;
    s.fill(shared_state.h).value();
    print_statfs(shared_state.h, s);
  }
  alignas(4096) llfio::byte buffer[IO_SIZE];
  llfio::utils::random_fill((char *) buffer, sizeof(buffer));
  std::vector<work_item> workitems;
  try
  {
    for(size_t n = 0; n < WORK_ITEMS; n++)
    {
      workitems.emplace_back(&shared_state);
      shared_state.h.write(n * IO_SIZE, {{buffer, sizeof(buffer)}}).value();
    }
  }
  catch(const std::runtime_error &e)
  {
    std::cout << "\nNOTE: Received exception '" << e.what()
              << "' when trying to construct dynamic_thread_pool_group::io_aware_work_item, assuming this platform does not implement statfs::f_iosinprogress "
                 "and skipping test."
              << std::endl;
    return;
  }
  auto tpg = llfio::make_dynamic_thread_pool_group().value();
  tpg->submit(llfio::span<work_item>(workitems)).value();
  auto begin = std::chrono::steady_clock::now();
  size_t paced = 0;
  while(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - begin) < std::chrono::seconds(60))
  {
    llfio::statfs_t statfs;
    statfs.fill(shared_state.h, llfio::statfs_t::want::iosinprogress | llfio::statfs_t::want::iosbusytime | llfio::statfs_t::want::mntonname).value();
    std::cout << "\nStorage device at " << statfs.f_mntonname << " is at " << (100.0f * statfs.f_iosbusytime) << "% utilisation and has an i/o queue depth of "
              << statfs.f_iosinprogress << ". Current concurrency is " << shared_state.concurrency.load(std::memory_order_relaxed) << " and current pacing is "
              << (shared_state.current_pacing.load(std::memory_order_relaxed) / 1000.0) << " microseconds." << std::endl;
    if(shared_state.current_pacing.load(std::memory_order_relaxed) > 0)
    {
      paced++;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  std::cout << "\nStopping ..." << std::endl;
  tpg->stop().value();
  while(!tpg->stopped())
  {
    std::cout << "\nCurrent concurrency is " << shared_state.concurrency.load(std::memory_order_relaxed) << " and current pacing is "
              << (shared_state.current_pacing.load(std::memory_order_relaxed) / 1000.0) << " microseconds." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  auto r = tpg->wait();
  if(!r && r.error() != llfio::errc::operation_canceled)
  {
    r.value();
  }
  BOOST_CHECK(paced > 0);
}

KERNELTEST_TEST_KERNEL(integration, llfio, dynamic_thread_pool_group, works, "Tests that llfio::dynamic_thread_pool_group works as expected",
                       TestDynamicThreadPoolGroupWorks())
KERNELTEST_TEST_KERNEL(integration, llfio, dynamic_thread_pool_group, nested, "Tests that nesting of llfio::dynamic_thread_pool_group works as expected",
                       TestDynamicThreadPoolGroupNestingWorks())
KERNELTEST_TEST_KERNEL(integration, llfio, dynamic_thread_pool_group, io_aware_work_item,
                       "Tests that llfio::dynamic_thread_pool_group::io_aware_work_item works as expected", TestDynamicThreadPoolGroupIoAwareWorks())
