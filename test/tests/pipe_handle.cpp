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
#include <unordered_set>

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
  {  // no writer, so non-blocking read should time out
    auto read = reader.read(0, {{buffer, 64}}, std::chrono::milliseconds(0));
    BOOST_REQUIRE(read.has_error());
    BOOST_REQUIRE(read.error() == llfio::errc::timed_out);
  }
  {  // no writer, so blocking read should time out
    auto read = reader.read(0, {{buffer, 64}}, std::chrono::seconds(1));
    BOOST_REQUIRE(read.has_error());
    BOOST_REQUIRE(read.error() == llfio::errc::timed_out);
  }
  llfio::pipe_handle writer = llfio::pipe_handle::pipe_open("llfio-pipe-handle-test", llfio::pipe_handle::caching::all, llfio::pipe_handle::flag::multiplexable).value();
  auto written = writer.write(0, {{(const llfio::byte *) "hello", 5}}).value();
  BOOST_REQUIRE(written == 5);
  // writer.barrier().value();  // would block until pipe drained by reader
  // writer.close().value();  // would cause all further reads to fail due to pipe broken
  auto read = reader.read(0, {{buffer, 64}}, std::chrono::milliseconds(0));
  BOOST_REQUIRE(read.value() == 5);
  BOOST_CHECK(0 == memcmp(buffer, "hello", 5));
  writer.barrier().value();  // must not block nor fail
  writer.close().value();
  reader.close().value();
}

static inline void TestMultiplexedPipeHandle()
{
  static constexpr size_t MAX_PIPES = 64;
  namespace llfio = LLFIO_V2_NAMESPACE;
  std::vector<llfio::pipe_handle> read_pipes, write_pipes;
  std::vector<size_t> received_for(MAX_PIPES);
  struct checking_receiver
  {
    std::vector<size_t> &received_for;
    union {
      llfio::byte _buffer[sizeof(size_t)];
      size_t _index;
    };
    llfio::pipe_handle::buffer_type buffer;

    checking_receiver(std::vector<size_t> &r)
        : received_for(r)
        , buffer(_buffer, sizeof(_buffer))
    {
      memset(_buffer, 0, sizeof(_buffer));
    }
    void set_value(llfio::pipe_handle::io_result<llfio::pipe_handle::buffers_type> res)
    {
      BOOST_CHECK(res.has_value());
      if(res)
      {
        BOOST_REQUIRE(res.value().size() == 1);
        BOOST_CHECK(res.value()[0].data() == _buffer);
        BOOST_CHECK(res.value()[0].size() == sizeof(size_t));
        BOOST_REQUIRE(_index < MAX_PIPES);
        received_for[_index]++;
      }
    }
    void set_done()
    {
      std::cout << "Cancelled!" << std::endl;
      BOOST_CHECK(false);
    }
  };
  std::vector<llfio::io_operation_connection<llfio::async_read, checking_receiver>> async_reads;
  auto multiplexer = llfio::this_thread::multiplexer();
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    auto ret = llfio::pipe_handle::anonymous_pipe(llfio::pipe_handle::caching::reads, llfio::pipe_handle::flag::multiplexable).value();
    ret.first.set_multiplexer(multiplexer).value();
    async_reads.push_back(llfio::connect(llfio::async_read(ret.first), checking_receiver(received_for)));
    read_pipes.push_back(std::move(ret.first));
    write_pipes.push_back(std::move(ret.second));
  }
  auto writerthread = std::async([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for(size_t n = MAX_PIPES - 1; n < MAX_PIPES; n--)
    {
      auto r = write_pipes[n].write(0, {{(llfio::byte *) &n, sizeof(n)}});
      if(!r)
      {
        abort();
      }
    }
  });
  // Start the connected states. They cannot move in memory until complete
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    async_reads[n].receiver().buffer = {async_reads[n].receiver()._buffer, sizeof(async_reads[n].receiver()._buffer)};
    async_reads[n].sender().request() = {{async_reads[n].receiver().buffer}, 0};
    async_reads[n].start();
  }
  // Wait for all reads to complete
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    // Block until this i/o completes
    while(async_reads[n].poll() == llfio::io_state_status::scheduled)
    {
      llfio::this_thread::multiplexer()->run().value();
    }
  }
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    BOOST_CHECK(received_for[n] == 1);
  }
  writerthread.get();
}

#if LLFIO_ENABLE_COROUTINES
static inline void TestCoroutinedPipeHandle()
{
  static constexpr size_t MAX_PIPES = 70;
  namespace llfio = LLFIO_V2_NAMESPACE;
  struct io_visitor
  {
    using container_type = std::unordered_set<llfio::io_awaitable<llfio::async_read, io_visitor> *>;
    container_type &c;
    void await_suspend(container_type::value_type i) { c.insert(i); }
    void await_resume(container_type::value_type i) { c.erase(i); }
  };
  io_visitor::container_type io_pending;
  struct coroutine
  {
    llfio::pipe_handle read_pipe, write_pipe;
    size_t received_for{0};

    explicit coroutine(llfio::pipe_handle &&r, llfio::pipe_handle &&w)
        : read_pipe(std::move(r))
        , write_pipe(std::move(w))
    {
    }
    llfio::eager<llfio::result<void>> operator()(io_visitor::container_type &io_pending)
    {
      union {
        llfio::byte _buffer[sizeof(size_t)];
        size_t _index;
      };
      llfio::pipe_handle::buffer_type buffer;
      for(;;)
      {
        buffer = {_buffer, sizeof(_buffer)};
        // This will never return if the coroutine gets cancelled
        auto r = co_await read_pipe.co_read(io_visitor{io_pending}, {{buffer}, 0});
        if(!r)
        {
          co_return r.error();
        }
        BOOST_CHECK(r.value().size() == 1);
        BOOST_CHECK(r.value()[0].size() == sizeof(_buffer));
        ++received_for;
      }
    }
  };
  std::vector<coroutine> coroutines;
  auto multiplexer = llfio::this_thread::multiplexer();
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    auto ret = llfio::pipe_handle::anonymous_pipe(llfio::pipe_handle::caching::reads, llfio::pipe_handle::flag::multiplexable).value();
    ret.first.set_multiplexer(multiplexer).value();
    coroutines.push_back(coroutine(std::move(ret.first), std::move(ret.second)));
  }
  // Start the coroutines, all of whom will begin a read and then suspend
  std::vector<llfio::optional<llfio::eager<llfio::result<void>>>> states(MAX_PIPES);
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    states[n].emplace(coroutines[n](io_pending));
  }
  // Write to all the pipes, then pump coroutine resumption until all completions done
  for(size_t i = 0; i < 10; i++)
  {
    for(size_t n = MAX_PIPES - 1; n < MAX_PIPES; n--)
    {
      coroutines[n].write_pipe.write(0, {{(llfio::byte *) &i, sizeof(i)}}).value();
    }
    // Take a copy of all pending i/o
    std::vector<io_visitor::container_type::value_type> copy(io_pending.begin(), io_pending.end());
    for(;;)
    {
      // Manually check if an i/o completion is ready, avoiding any syscalls
      bool need_to_poll = true;
      for(auto it = copy.begin(); it != copy.end();)
      {
        if((*it)->await_ready())
        {
          need_to_poll = false;
          it = copy.erase(it);
          std::cout << "Completed an i/o without syscall" << std::endl;
        }
        else
          ++it;
      }
      if(need_to_poll)
      {
        // Have the kernel tell me when an i/o completion is ready
        auto r = multiplexer->poll();
        BOOST_CHECK(r.value() != 0);
        if(r.value() < 0)
        {
          for(size_t n = 0; n < MAX_PIPES; n++)
          {
            BOOST_CHECK(coroutines[n].received_for == i + 1);
          }
          break;
        }
      }
    }
  }
  // Rethrow any failures
  for(size_t n = 0; n < MAX_PIPES; n++)
  {
    if(states[n]->await_ready())
    {
      states[n]->await_resume().value();
    }
  }
  // Destruction of coroutines when they are suspended must work.
  // This will cancel any pending i/o and immediately exit the
  // coroutines
  states.clear();
  // Now clear all the coroutines
  coroutines.clear();
}
#endif

KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, blocking, "Tests that blocking llfio::pipe_handle works as expected", TestBlockingPipeHandle())
KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, nonblocking, "Tests that nonblocking llfio::pipe_handle works as expected", TestNonBlockingPipeHandle())
KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, multiplexed, "Tests that multiplexed llfio::pipe_handle works as expected", TestMultiplexedPipeHandle())
#if LLFIO_ENABLE_COROUTINES
KERNELTEST_TEST_KERNEL(integration, llfio, pipe_handle, coroutined, "Tests that coroutined llfio::pipe_handle works as expected", TestCoroutinedPipeHandle())
#endif
