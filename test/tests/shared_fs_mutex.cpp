/* Integration testing for algorithm::shared_fs_mutex
(C) 2016-2017 Niall Douglas <http://www.nedproductions.biz/> (13 commits)
File Created: Aug 2016


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

#define QUICKCPPLIB_BOOST_UNIT_TEST_CUSTOM_MAIN_DEFINED

#include "../test_kernel_decl.hpp"

KERNELTEST_TEST_KERNEL(unit, llfio, shared_fs_mutex, entity_endian, "Tests that llfio::algorithm::shared_fs_mutex::entity_type has the right endian", [] {
  LLFIO_V2_NAMESPACE::algorithm::shared_fs_mutex::shared_fs_mutex::entity_type v(0, true);
  BOOST_REQUIRE(v._init != 1UL);  // NOLINT
}())

#include <codecvt>
#include <condition_variable>
#include <future>
#include <unordered_map>

KERNELTEST_V1_NAMESPACE_BEGIN
struct waitable_done
{
  std::atomic<int> done;

  std::mutex _m;
  std::condition_variable _cv;
  explicit waitable_done(int v = 0)
      : done(v)
  {
  }
  template <class Rep, class Period> bool wait_until_done_for(const std::chrono::duration<Rep, Period> &rel)
  {
    std::unique_lock<std::mutex> h(_m);
    return _cv.wait_for(h, rel, [&] { return done != 0; });
  }
  template <class Clock, class Duration> bool wait_until_done_until(const std::chrono::time_point<Clock, Duration> &dl)
  {
    std::unique_lock<std::mutex> h(_m);
    return _cv.wait_until(h, dl, [&] { return done != 0; });
  }
  void wait_until_done()
  {
    std::unique_lock<std::mutex> h(_m);
    _cv.wait(h, [&] { return done != 0; });
  }
  void set_done(int v)
  {
    done = v;
    _cv.notify_all();
  }
};

static std::unordered_map<std::string, std::function<std::string(waitable_done &, size_t, const char *)>> kerneltest_child_worker_registry;

struct kerneltest_child_worker_registration
{
  std::string name;
  template <class U> kerneltest_child_worker_registration(std::string _name, U &&f) noexcept : name(std::move(_name)) { kerneltest_child_worker_registry.insert(std::make_pair(name, std::forward<U>(f))); }
  kerneltest_child_worker_registration(kerneltest_child_worker_registration &&o) noexcept : name(std::move(o.name)) {}
  kerneltest_child_worker_registration(const kerneltest_child_worker_registration &o) = delete;
  kerneltest_child_worker_registration &operator=(const kerneltest_child_worker_registration &o) = delete;
  kerneltest_child_worker_registration &operator=(kerneltest_child_worker_registration &&o) = delete;
  ~kerneltest_child_worker_registration()
  {
    if(!name.empty())
    {
      kerneltest_child_worker_registry.erase(name);
    }
  }
};
template <class U> kerneltest_child_worker_registration register_child_worker(const char *name, U &&f) noexcept
{
  return kerneltest_child_worker_registration(name, std::forward<U>(f));
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127)  // conditional expression is constant
#endif
namespace detail
{
  void print_args(std::ostream & /*unused*/) {}
  template <class Arg, class... Args> void print_args(std::ostream &s, Arg &&arg, Args &&... args)
  {
    if(std::is_constructible<std::string, Arg>::value)
    {
      s << "\"";
    }
    s << arg;
    if(std::is_constructible<std::string, Arg>::value)
    {
      s << "\"";
    }
    s << ",";
    print_args(s, std::forward<Args>(args)...);
  }
  template <class Arg> void print_args(std::ostream &s, Arg &&arg) { s << arg; }
  void print_args(std::wostream & /*unused*/) {}
  template <class Arg, class... Args> void print_args(std::wostream &s, Arg &&arg, Args &&... args)
  {
    if(std::is_constructible<std::wstring, Arg>::value || std::is_constructible<std::string, Arg>::value)
    {
      s << "\"";
    }
    s << arg;
    if(std::is_constructible<std::wstring, Arg>::value || std::is_constructible<std::string, Arg>::value)
    {
      s << "\"";
    }
    s << ",";
    print_args(s, std::forward<Args>(args)...);
  }
  template <class Arg> void print_args(std::wostream &s, Arg &&arg) { s << arg; }
}  // namespace detail
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct child_workers
{
  std::vector<child_process::child_process> workers;
  struct result
  {
    std::string cerr;
    intptr_t retcode{0};
    std::string results;
  };
  std::vector<result> results;

  template <class... Args> child_workers(std::string name, size_t workersno, Args &&... _args)
  {
    std::basic_stringstream<filesystem::path::value_type> ss1, ss2, ss3;
    auto convstr = [](const std::string &v) { return filesystem::path(v).native(); };
    ss1 << "--kerneltestchild," << convstr(name) << ",";
    ss3 << ",";
    detail::print_args(ss3, std::forward<Args>(_args)...);
    results.resize(workersno);
    for(size_t n = 0; n < workersno; n++)
    {
      ss2 = decltype(ss2)();
      ss2 << ss1.str() << n << ss3.str();
      std::vector<filesystem::path::string_type> args{ss2.str()};
      auto newchild = child_process::child_process::launch(child_process::current_process_path(), std::move(args), child_process::current_process_env(), true);
#if 0
      if(!newchild)
      {
        fprintf(stderr, "Failed to launch new child process due to %s. Press Return to fatal exit.\n", newchild.error().message().c_str());
        getchar();
        abort();
      }
#endif
      workers.push_back(std::move(newchild).value());
      results[n].retcode = 0;
    }
  }
  void wait_until_ready()
  {
    char buffer[1024];
    for(size_t n = 0; n < workers.size(); n++)
    {
      auto &i = workers[n].cout();
      if(!i.getline(buffer, sizeof(buffer)))  // NOLINT
      {
        results[n].cerr = "ERROR: Child seems to have vanished! (wait_until_ready)";
        results[n].retcode = 99;
        continue;
      }
      if(0 != strncmp(buffer, "READY", 5))  // NOLINT
      {
        results[n].cerr = "ERROR: Child wrote unexpected output '" + std::string(buffer) + "' (wait_until_ready)";  // NOLINT
        results[n].retcode = 98;
        continue;
      }
    }
  }
  void go()
  {
    for(size_t n = 0; n < workers.size(); n++)
    {
      if(0 == results[n].retcode)
      {
        workers[n].cin() << "GO" << std::endl;
      }
    }
  }
  void stop()
  {
    for(size_t n = 0; n < workers.size(); n++)
    {
      if(0 == results[n].retcode)
      {
        workers[n].cin() << "STOP" << std::endl;
      }
    }
  }
  void join()
  {
    char buffer[1024];
    for(size_t n = 0; n < workers.size(); n++)
    {
      auto &child = workers[n];
      if(0 == results[n].retcode)
      {
        if(!child.cout().getline(buffer, sizeof(buffer)))  // NOLINT
        {
          results[n].cerr = child.is_running() ? "ERROR: Child pipe is unreadable! (join)" : "ERROR: Child seems to have vanished! (join)";
          results[n].retcode = 99;
        }
        else if(0 != strncmp(buffer, "RESULTS ", 8))  // NOLINT
        {
          results[n].cerr = "ERROR: Child wrote unexpected output '" + std::string(buffer) + "' (join)";  // NOLINT
          results[n].retcode = 98;
        }
        else
        {
          results[n].results = buffer + 8;  // NOLINT
          if(results[n].results.back() == '\r')
          {
            results[n].results.resize(results[n].results.size() - 1);
          }
        }
      }
#ifdef NDEBUG
      std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
#else
      std::chrono::steady_clock::time_point deadline;
#endif
      intptr_t ret = child.wait_until(deadline).value();
      if(ret != 0)
      {
        results[n].retcode = ret;
      }
    }
  }
};
template <class... Args> inline child_workers launch_child_workers(std::string name, size_t workers, Args &&... args)
{
  return child_workers(std::move(name), workers, std::forward<Args>(args)...);
}


KERNELTEST_V1_NAMESPACE_END

/*














*/

struct shared_memory
{
  std::atomic<long> current_exclusive;
  char _cacheline1[64];
  enum mutex_kind_type
  {
    atomic_append,
    byte_ranges,
    safe_byte_ranges,
    lock_files,
    memory_map
  } mutex_kind;
  enum test_type
  {
    exclusive = 0,
    shared = 1,
    both = 2
  } testtype;
  char _cacheline2[64];
  std::atomic<long> current_shared;
};

static inline void _check_child_worker(const std::string &i)
{
  BOOST_CHECK((i[0] == 'o' && i[1] == 'k'));
  std::cout << "Child reports " << i << std::endl;
}

static inline void check_child_worker(const KERNELTEST_V1_NAMESPACE::child_workers::result &i)
{
  if(!i.cerr.empty())
  {
    BOOST_CHECK(i.cerr.empty());
    std::cout << "Child worker failed with '" << i.cerr << "' (return code was " << i.retcode << ")" << std::endl;
  }
  else if(i.retcode != 0)
  {
    BOOST_CHECK(i.retcode == 0);
    std::cout << "Child worker process failed with return code " << i.retcode << std::endl;
  }
  else
  {
    BOOST_CHECK(i.retcode == 0);
    _check_child_worker(i.results);
  }
}

static std::string _TestSharedFSMutexCorrectnessChildWorker(KERNELTEST_V1_NAMESPACE::waitable_done &waitable, size_t childidx, shared_memory *shmem)  // NOLINT
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  ++shmem->current_exclusive;
  while(-1 != shmem->current_exclusive)
  {
    std::this_thread::yield();
  }
  std::unique_ptr<llfio::algorithm::shared_fs_mutex::shared_fs_mutex> lock;
  switch(shmem->mutex_kind)
  {
  case shared_memory::mutex_kind_type::atomic_append:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::atomic_append>(llfio::algorithm::shared_fs_mutex::atomic_append::fs_mutex_append({}, "lockfile").value());
    break;
  case shared_memory::mutex_kind_type::byte_ranges:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::byte_ranges>(llfio::algorithm::shared_fs_mutex::byte_ranges::fs_mutex_byte_ranges({}, "lockfile").value());
    break;
  case shared_memory::mutex_kind_type::safe_byte_ranges:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::safe_byte_ranges>(llfio::algorithm::shared_fs_mutex::safe_byte_ranges::fs_mutex_safe_byte_ranges({}, "lockfile").value());
    break;
  case shared_memory::mutex_kind_type::lock_files:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::lock_files>(llfio::algorithm::shared_fs_mutex::lock_files::fs_mutex_lock_files(llfio::path_handle::path(".").value()).value());
    break;
  case shared_memory::mutex_kind_type::memory_map:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::memory_map<>>(llfio::algorithm::shared_fs_mutex::memory_map<>::fs_mutex_map({}, "lockfile").value());
    break;
  }
  ++shmem->current_shared;
  while(0 != shmem->current_shared)
  {
    std::this_thread::yield();
  }
  long maxreaders = 0;
  do
  {
    if(shmem->testtype == shared_memory::test_type::exclusive || (shmem->testtype == shared_memory::test_type::both && childidx < 2))
    {
      auto h = lock->lock(llfio::algorithm::shared_fs_mutex::shared_fs_mutex::entity_type(0, true)).value();
      long oldval = shmem->current_exclusive.exchange(static_cast<long>(childidx));
      if(oldval != -1)
      {
        return "Child " + std::to_string(childidx) + " granted exclusive lock when child " + std::to_string(oldval) + " already has exclusive lock!";
      }
      oldval = shmem->current_shared;
      if(oldval != 0)
      {
        return "Child " + std::to_string(childidx) + " holding exclusive lock yet " + std::to_string(oldval) + " users hold the shared lock!";
      }
      oldval = shmem->current_exclusive.exchange(-1);
      if(oldval != static_cast<long>(childidx))
      {
        return "Child " + std::to_string(childidx) + " released exclusive lock to find child " + std::to_string(oldval) + " had stolen my lock!";
      }
    }
    else if(shmem->testtype == shared_memory::test_type::shared || shmem->testtype == shared_memory::test_type::both)
    {
      auto h = lock->lock(llfio::algorithm::shared_fs_mutex::shared_fs_mutex::entity_type(0, false)).value();
      long oldval = ++shmem->current_shared;
      if(oldval > maxreaders)
      {
        maxreaders = oldval;
      }
      oldval = shmem->current_exclusive;
      if(oldval != -1)
      {
        return "Child " + std::to_string(childidx) + " granted shared lock when child " + std::to_string(oldval) + " already has exclusive lock!";
      }
      --shmem->current_shared;
    }
  } while(0 == waitable.done);
  return "ok, max concurrent readers was " + std::to_string(maxreaders);
}
static auto TestSharedFSMutexCorrectnessChildWorker = KERNELTEST_V1_NAMESPACE::register_child_worker("TestSharedFSMutexCorrectness", [](KERNELTEST_V1_NAMESPACE::waitable_done &waitable, size_t childidx, const char * /*unused*/) -> std::string {  // NOLINT
  namespace llfio = LLFIO_V2_NAMESPACE;
  auto shared_mem_file = llfio::file_handle::file({}, "shared_memory", llfio::file_handle::mode::write, llfio::file_handle::creation::open_existing, llfio::file_handle::caching::temporary).value();
  auto shared_mem_file_section = llfio::section_handle::section(shared_mem_file, sizeof(shared_memory), llfio::section_handle::flag::readwrite).value();
  auto shared_mem_file_map = llfio::map_handle::map(shared_mem_file_section).value();
  auto *shmem = reinterpret_cast<shared_memory *>(shared_mem_file_map.address());  // NOLINT
  return _TestSharedFSMutexCorrectnessChildWorker(waitable, childidx, shmem);
});


/*
Test 1: X child processes use the lock for exclusive and shared usage. Verify we never allow exclusive with anything
else. Verify shared allows other shared.
*/

void TestSharedFSMutexCorrectness(shared_memory::mutex_kind_type mutex_kind, shared_memory::test_type testtype, bool threads_not_processes)
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  auto shared_mem_file = llfio::file_handle::file({}, "shared_memory", llfio::file_handle::mode::write, llfio::file_handle::creation::if_needed, llfio::file_handle::caching::temporary, llfio::file_handle::flag::unlink_on_first_close).value();
  shared_mem_file.truncate(sizeof(shared_memory)).value();
  auto shared_mem_file_section = llfio::section_handle::section(shared_mem_file, sizeof(shared_memory), llfio::section_handle::flag::readwrite).value();
  auto shared_mem_file_map = llfio::map_handle::map(shared_mem_file_section).value();
  auto *shmem = reinterpret_cast<shared_memory *>(shared_mem_file_map.address());  // NOLINT
  shmem->current_shared = -static_cast<long>(std::thread::hardware_concurrency());
  shmem->current_exclusive = -static_cast<long>(std::thread::hardware_concurrency()) - 1;
  shmem->mutex_kind = mutex_kind;
  shmem->testtype = testtype;

  if(threads_not_processes)
  {
    KERNELTEST_V1_NAMESPACE::waitable_done waitable(0);
    std::vector<std::pair<std::future<std::string>, std::thread>> thread_workers;
    thread_workers.reserve(std::thread::hardware_concurrency());
    for(size_t n = 0; n < std::thread::hardware_concurrency(); n++)
    {
      std::packaged_task<std::string(KERNELTEST_V1_NAMESPACE::waitable_done &, size_t, shared_memory *)> task(_TestSharedFSMutexCorrectnessChildWorker);
      auto f = task.get_future();
      thread_workers.emplace_back(std::move(f), std::thread(std::move(task), std::ref(waitable), n, shmem));
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
    waitable.set_done(1);
    for(auto &i : thread_workers)
    {
      i.second.join();
      _check_child_worker(i.first.get());
    }
  }
  else
  {
    auto child_workers = KERNELTEST_V1_NAMESPACE::launch_child_workers("TestSharedFSMutexCorrectness", std::thread::hardware_concurrency());
    child_workers.wait_until_ready();
// I now have hardware_concurrency child worker processes ready to go, so execute the child worker
#if 0
    std::cout << "Please attach debuggers and press Enter" << std::endl;
    getchar();
#endif
    child_workers.go();
    // They will all open the shared memory and gate on current_exclusive until all reach the exact same point
    // They will then all create the lock concurrently and gate on current_shared until all reach the exact same point
    // They then will iterate locking and unlocking the same entity using the shared memory to verify it never happens
    // that more than one ever holds an exclusive lock, or any shared lock occurs when an exclusive lock is held
    std::this_thread::sleep_for(std::chrono::seconds(5));
    child_workers.stop();
    child_workers.join();
    for(auto &i : child_workers.results)
    {
      check_child_worker(i);
    }
  }
}

KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_byte_ranges, exclusives, "Tests that llfio::algorithm::shared_fs_mutex::byte_ranges implementation implements exclusive locking", [] { TestSharedFSMutexCorrectness(shared_memory::byte_ranges, shared_memory::exclusive, false); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_byte_ranges, shared, "Tests that llfio::algorithm::shared_fs_mutex::byte_ranges implementation implements shared locking", [] { TestSharedFSMutexCorrectness(shared_memory::byte_ranges, shared_memory::shared, false); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_byte_ranges, both, "Tests that llfio::algorithm::shared_fs_mutex::byte_ranges implementation implements a mixture of exclusive and shared locking", [] { TestSharedFSMutexCorrectness(shared_memory::byte_ranges, shared_memory::both, false); }())

KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_memory_map, exclusives, "Tests that llfio::algorithm::shared_fs_mutex::memory_map implementation implements exclusive locking", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::exclusive, false); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_memory_map, shared, "Tests that llfio::algorithm::shared_fs_mutex::memory_map implementation implements shared locking", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::shared, false); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_memory_map, both, "Tests that llfio::algorithm::shared_fs_mutex::memory_map implementation implements a mixture of exclusive and shared locking", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::both, false); }())

KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_safe_byte_ranges_process, exclusives, "Tests that llfio::algorithm::shared_fs_mutex::safe_byte_ranges implementation implements exclusive locking with processes", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::exclusive, false); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_safe_byte_ranges_process, shared, "Tests that llfio::algorithm::shared_fs_mutex::safe_byte_ranges implementation implements shared locking with processes", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::shared, false); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_safe_byte_ranges_process, both, "Tests that llfio::algorithm::shared_fs_mutex::safe_byte_ranges implementation implements a mixture of exclusive and shared locking with processes",
                       [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::both, false); }())

KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_safe_byte_ranges_thread, exclusives, "Tests that llfio::algorithm::shared_fs_mutex::safe_byte_ranges implementation implements exclusive locking with threads", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::exclusive, true); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_safe_byte_ranges_thread, shared, "Tests that llfio::algorithm::shared_fs_mutex::safe_byte_ranges implementation implements shared locking with threads", [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::shared, true); }())
KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_safe_byte_ranges_thread, both, "Tests that llfio::algorithm::shared_fs_mutex::safe_byte_ranges implementation implements a mixture of exclusive and shared locking with threads",
                       [] { TestSharedFSMutexCorrectness(shared_memory::memory_map, shared_memory::both, true); }())

/*

Test 2: X child processes all try to construct the lock at once, lock something shared, unlock it and destruct. A
counter determines if everybody locked the same thing. Important there is no blocking.

*/

// TODO(ned)


/*

Test 3: We have a constant ongoing stream of child processes constructing the lock, locking something, unlocking,
and destructing the lock. This should find interesting races in the more complex lock constructors and destructors

*/
static void TestSharedFSMutexConstructDestruct(shared_memory::mutex_kind_type mutex_kind)
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  auto shared_mem_file = llfio::file_handle::file({}, "shared_memory", llfio::file_handle::mode::write, llfio::file_handle::creation::if_needed, llfio::file_handle::caching::temporary, llfio::file_handle::flag::unlink_on_first_close).value();
  shared_mem_file.truncate(sizeof(shared_memory)).value();
  auto shared_mem_file_section = llfio::section_handle::section(shared_mem_file, sizeof(shared_memory), llfio::section_handle::flag::readwrite).value();
  auto shared_mem_file_map = llfio::map_handle::map(shared_mem_file_section).value();
  auto *shmem = reinterpret_cast<shared_memory *>(shared_mem_file_map.address());  // NOLINT
  shmem->current_shared = -2;
  shmem->current_exclusive = -3;
  shmem->mutex_kind = mutex_kind;
  shmem->testtype = shared_memory::test_type::exclusive;

  // Launch two child workers who constantly lock and unlock, checking correctness
  auto child_workers1 = KERNELTEST_V1_NAMESPACE::launch_child_workers("TestSharedFSMutexCorrectness", 2);
  child_workers1.wait_until_ready();
  child_workers1.go();

  // Now repeatedly launch hardware concurrency child workers who constantly open the lock, lock once, unlock and close the lock
  auto begin = std::chrono::steady_clock::now();
  while(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - begin).count() < 5)
  {
    auto child_workers = KERNELTEST_V1_NAMESPACE::launch_child_workers("TestSharedFSMutexConstructDestruct", std::thread::hardware_concurrency(), static_cast<size_t>(mutex_kind));
    child_workers.wait_until_ready();
    child_workers.go();
    child_workers.stop();
    child_workers.join();
    for(auto &i : child_workers.results)
    {
      check_child_worker(i);
    }
  }
  child_workers1.stop();
  child_workers1.join();
  for(auto &i : child_workers1.results)
  {
    check_child_worker(i);
  }
}

static auto TestSharedFSMutexConstructDestructChildWorker = KERNELTEST_V1_NAMESPACE::register_child_worker("TestSharedFSMutexConstructDestruct", [](KERNELTEST_V1_NAMESPACE::waitable_done & /*unused*/, size_t /*unused*/, const char *params) -> std::string {  // NOLINT
  namespace llfio = LLFIO_V2_NAMESPACE;
  std::unique_ptr<llfio::algorithm::shared_fs_mutex::shared_fs_mutex> lock;
  auto mutex_kind = static_cast<shared_memory::mutex_kind_type>(atoi(params));  // NOLINT
  switch(mutex_kind)
  {
  case shared_memory::mutex_kind_type::atomic_append:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::atomic_append>(llfio::algorithm::shared_fs_mutex::atomic_append::fs_mutex_append({}, "lockfile").value());
    break;
  case shared_memory::mutex_kind_type::byte_ranges:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::byte_ranges>(llfio::algorithm::shared_fs_mutex::byte_ranges::fs_mutex_byte_ranges({}, "lockfile").value());
    break;
  case shared_memory::mutex_kind_type::safe_byte_ranges:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::safe_byte_ranges>(llfio::algorithm::shared_fs_mutex::safe_byte_ranges::fs_mutex_safe_byte_ranges({}, "lockfile").value());
    break;
  case shared_memory::mutex_kind_type::lock_files:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::lock_files>(llfio::algorithm::shared_fs_mutex::lock_files::fs_mutex_lock_files(llfio::path_handle::path(".").value()).value());
    break;
  case shared_memory::mutex_kind_type::memory_map:
    lock = std::make_unique<llfio::algorithm::shared_fs_mutex::memory_map<>>(llfio::algorithm::shared_fs_mutex::memory_map<>::fs_mutex_map({}, "lockfile").value());
    break;
  }
  // Take a shared lock of a different entity
  auto h = lock->lock(llfio::algorithm::shared_fs_mutex::shared_fs_mutex::entity_type(1, false)).value();
  return "ok";
});

KERNELTEST_TEST_KERNEL(integration, llfio, shared_fs_mutex_memory_map, construct_destruct, "Tests that llfio::algorithm::shared_fs_mutex::memory_map constructor and destructor are race free", [] { TestSharedFSMutexConstructDestruct(shared_memory::memory_map); }())


/*













*/

int main(int argc, char *argv[])
{
  using namespace KERNELTEST_V1_NAMESPACE;
  for(int n = 1; n < argc; n++)
  {
    if(0 == strncmp(argv[n], "--kerneltestchild,", 18))  // NOLINT
    {
      // Format is --kerneltestchild,name,childidx,pars...
      char *comma = strchr(argv[n] + 18, ',');  // NOLINT
      if(nullptr == comma)
      {
        return 1;
      }
      std::string name(argv[n] + 18, comma - (argv[n] + 18));  // NOLINT
      size_t thischild = strtoul(comma + 1, nullptr, 10);      // NOLINT
      comma = strchr(comma + 1, ',');                          // NOLINT
      if(nullptr == comma)
      {
        return 1;
      }
      const char *param = comma + 1;  // NOLINT
      auto it = kerneltest_child_worker_registry.find(name);
      if(it == kerneltest_child_worker_registry.end())
      {
        std::cerr << "KernelTest child worker " << thischild << " fails to find work '" << name << "', exiting" << std::endl;
        return 1;
      }
      try
      {
        // Fire up a worker thread and get him to block
        waitable_done waitable(-1);
        std::string result;
        std::thread worker([&] {
          while(waitable.done == -1)
          {
            std::this_thread::yield();
          }
          result = it->second(waitable, thischild, param);
        });
        std::cout << "READY(" << thischild << ")" << std::endl;
        for(;;)
        {
          char buffer[1024];
          // This blocks
          if(!std::cin.getline(buffer, sizeof(buffer)))  // NOLINT
          {
            waitable.set_done(1);
            worker.join();
            return 1;
          }
          if(0 == strcmp(buffer, "GO"))  // NOLINT
          {
            // Launch worker thread
            waitable.set_done(0);
          }
          else if(0 == strcmp(buffer, "STOP"))  // NOLINT
          {
            waitable.set_done(1);
            worker.join();
            std::cout << "RESULTS " << result << std::endl;
            return 0;
          }
        }
      }
      catch(const std::exception &e)
      {
        std::cerr << "KernelTest child worker " << thischild << " throws exception '" << e.what() << "'" << std::endl;
        return 1;
      }
      catch(...)
      {
        std::cerr << "KernelTest child worker " << thischild << " throws exception 'unknown'" << std::endl;
        return 1;
      }
    }
  }
  LLFIO_V2_NAMESPACE::filesystem::create_directory("shared_fs_mutex_testdir");
  LLFIO_V2_NAMESPACE::filesystem::current_path("shared_fs_mutex_testdir");
  int result = QUICKCPPLIB_BOOST_UNIT_TEST_RUN_TESTS(argc, argv);
  return result;
}
