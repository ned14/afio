/* Integration test kernel for whether pipe handles work
(C) 2019 Niall Douglas <http://www.nedproductions.biz/> (2 commits)
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

#include "../test_kernel_decl.hpp"

#include <future>

static inline void TestBlockingPipeHandle()
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  auto readerthread = std::async([] {  // This immediately blocks in blocking mode
    llfio::pipe_handle reader = llfio::pipe_handle::pipe_create("llfio-pipe-handle-test").value();
    llfio::byte buffer[64];
    auto read = reader.read(0, {{buffer, 64}}).value();
    BOOST_REQUIRE(read == 5);
    BOOST_CHECK(0 == memcmp(buffer, "hello", 5));
    reader.close().value();
  });
  auto begin = std::chrono::steady_clock::now();
  while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() < 100)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if(std::future_status::ready == readerthread.wait_for(std::chrono::seconds(0)))
  {
    readerthread.get();  // rethrow exception
  }
  llfio::pipe_handle writer;
  begin = std::chrono::steady_clock::now();
  while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() < 1000)
  {
    auto r = llfio::pipe_handle::pipe_open("llfio-pipe-handle-test");
    if(r)
    {
      writer = std::move(r.value());
      break;
    }
  }
  BOOST_REQUIRE(writer.is_valid());
  auto written = writer.write(0, {{(const llfio::byte *) "hello", 5}}).value();
  BOOST_REQUIRE(written == 5);
  writer.barrier().value();
  writer.close().value();
  readerthread.get();
}

static inline void TestNonBlockingPipeHandle()
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  llfio::pipe_handle reader = llfio::pipe_handle::pipe_create("llfio-pipe-handle-test", llfio::pipe_handle::caching::all, llfio::pipe_handle::flag::multiplexable).value();
  llfio::byte buffer[64];
  {
    auto read = reader.read(0, {{buffer, 64}}, std::chrono::milliseconds(0));
    BOOST_REQUIRE(read.has_error());
    BOOST_REQUIRE(read.error() == llfio::errc::timed_out);
  }
  {
    auto read = reader.read(0, {{buffer, 64}}, std::chrono::seconds(1));
    BOOST_REQUIRE(read.has_error());
    BOOST_REQUIRE(read.error() == llfio::errc::timed_out);
  }
  llfio::pipe_handle writer = llfio::pipe_handle::pipe_open("llfio-pipe-handle-test", llfio::pipe_handle::caching::all, llfio::pipe_handle::flag::multiplexable).value();
  auto written = writer.write(0, {{(const llfio::byte *) "hello", 5}}).value();
  BOOST_REQUIRE(written == 5);
  writer.barrier().value();
  writer.close().value();
  auto read = reader.read(0, {{buffer, 64}}, std::chrono::milliseconds(0));
  BOOST_REQUIRE(read.value() == 5);
  BOOST_CHECK(0 == memcmp(buffer, "hello", 5));
  reader.close().value();
}

#ifdef OUTCOME_FOUND_COROUTINE_HEADER
static inline void TestCoroutinedPipeHandle()
{
  namespace llfio = LLFIO_V2_NAMESPACE;
  auto coro = []() -> llfio::io_handle::eager_awaitable<void> {
    llfio::pipe_handle reader = llfio::pipe_handle::pipe_create("llfio-pipe-handle-test", llfio::pipe_handle::caching::all, llfio::pipe_handle::flag::multiplexable).value();
    llfio::byte buffer[64];
    llfio::pipe_handle::buffer_type buffer1{buffer, 64}, buffer2{buffer, 64};
    // Start two parallel reads
    auto read1a = reader.co_read({{buffer1}, 0}, std::chrono::milliseconds(0));
    auto read2a = reader.co_read({{buffer2}, 0}, std::chrono::seconds(1));
    // Await on the first, who should be immediately ready due to zero deadline
    {
      BOOST_CHECK(read1a.await_ready());
      auto read = co_await read1a;
      BOOST_REQUIRE(read.has_error());
      BOOST_REQUIRE(read.error() == llfio::errc::timed_out);
    }
    // Await on the second, who should not be ready and should force a call to multiplexer()->run()
    {
      BOOST_CHECK(!read2a.await_ready());
      auto read = co_await read2a;
      BOOST_REQUIRE(read.has_error());
      BOOST_REQUIRE(read.error() == llfio::errc::timed_out);
    }
    llfio::pipe_handle writer = llfio::pipe_handle::pipe_open("llfio-pipe-handle-test", llfio::pipe_handle::caching::all, llfio::pipe_handle::flag::multiplexable).value();
    llfio::pipe_handle::const_buffer_type buffer3{(const llfio::byte *) "hello", 5};
    auto written = co_await writer.co_write({{buffer3}, 0});
    BOOST_REQUIRE(written.bytes_transferred() == 5);
    (co_await writer.co_barrier()).value();
    writer.close().value();
    buffer1 = {buffer, 64};
    auto read = co_await reader.co_read({{buffer1}, 0}, std::chrono::milliseconds(0));
    BOOST_REQUIRE(read.bytes_transferred() == 5);
    BOOST_CHECK(0 == memcmp(buffer, "hello", 5));
    reader.close().value();
  };
  auto a = coro();  // launch the coroutine
  while(!a.await_ready())
  {
    a.await_suspend({});  // pump it every time it suspends
  }
  a.await_resume();  // fetch its result
}
#endif

#if 0
      // This i/o is registered, pump completions until my coroutine resumes.
      // He will resume my coroutine if its deadline expires. We need to set
      // the result we would normally set if we returned normally, as we will
      // never return normally
      aw->_ret = {success()};
      bool found = true;
      while(found)
      {
        g.unlock();
        auto r = run();  // to infinity, though it may return
        g.lock();
        found = false;
        for(auto &i : it->second.io_outstanding)
        {
          if(i.co == co)
          {
            found = true;
            if(!r)
            {
              _remove_io(it, i);
              break;
            }
          }
        }
        if(!r)
        {
          return r.error();
        }
      }
      return success();
#endif

KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, blocking, "Tests that blocking llfio::pipe_handle works as expected", TestBlockingPipeHandle())
KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, nonblocking, "Tests that nonblocking llfio::pipe_handle works as expected", TestNonBlockingPipeHandle())
#ifdef OUTCOME_FOUND_COROUTINE_HEADER
KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, coroutined, "Tests that coroutined llfio::pipe_handle works as expected", TestCoroutinedPipeHandle())
#endif
