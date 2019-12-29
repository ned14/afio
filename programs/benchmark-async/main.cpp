/* Test the performance of async i/o
(C) 2020 Niall Douglas <http://www.nedproductions.biz/> (6 commits)
File Created: Jan 2020


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
static constexpr size_t BENCHMARK_DURATION = 10;

static constexpr size_t THREADS = 2;

#include "../../include/llfio/llfio.hpp"

#include "quickcpplib/algorithm/small_prng.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#if __has_include("asio/include/asio.hpp")
#define ENABLE_ASIO 1
#include "asio/include/asio.hpp"
#endif

namespace llfio = LLFIO_V2_NAMESPACE;

struct test_results
{
  double creation{0}, ops{0}, cancel{0} , destruction{0};
};

template <class C> test_results do_benchmark(size_t handles)
{
  auto create1 = std::chrono::high_resolution_clock::now();
  auto ios = C(handles);
  auto create2 = std::chrono::high_resolution_clock::now();
  std::vector<std::thread> threads;
  std::atomic<size_t> done(THREADS + 1);
  for(unsigned n = 0; n < THREADS; n++)
  {
    threads.push_back(std::thread(
    [&](unsigned seed) {
      QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng rand(seed);
      --done;
      while(done > 0)
      {
        std::this_thread::yield();
      }
      while(!done)
      {
        unsigned v = rand() & (unsigned) (handles - 1);
        ios.write(v);
      }
      ++done;
    },
    n));
  }
  while(done != 1)
  {
    std::this_thread::yield();
  }
  done = 0;
  auto ops1 = std::chrono::high_resolution_clock::now(), ops2 = ops1;
  do
  {
    for(size_t n = 0; n < 1024; n++)
    {
      ios.read();
    }
    ops2 = std::chrono::high_resolution_clock::now();
    if(!done && std::chrono::duration_cast<std::chrono::seconds>(ops2 - ops1).count() >= BENCHMARK_DURATION)
    {
      done = 1;
    }
  } while(done < THREADS + 1);
  auto cancel1 = std::chrono::high_resolution_clock::now();
  ios.cancel();
  auto cancel2 = std::chrono::high_resolution_clock::now();
  auto destroy1 = std::chrono::high_resolution_clock::now();
  size_t ops = ios.destroy();
  auto destroy2 = std::chrono::high_resolution_clock::now();
  for(auto &i : threads)
  {
    i.join();
  }
  test_results ret;
  ret.creation = 1000000000.0 * handles / std::chrono::duration_cast<std::chrono::nanoseconds>(create2 - create1).count();
  ret.ops = 1000000000.0 * ops / std::chrono::duration_cast<std::chrono::nanoseconds>(ops2 - ops1).count();
  ret.cancel = 1000000000.0 * handles / std::chrono::duration_cast<std::chrono::nanoseconds>(cancel2 - cancel1).count();
  ret.destruction = 1000000000.0 * handles / std::chrono::duration_cast<std::chrono::nanoseconds>(destroy2 - destroy1).count();
  return ret;
}

template <class C> void benchmark(llfio::path_view csv, size_t max_handles)
{
  auto res = do_benchmark<C>(1);
  std::cout << "creation = " << res.creation << "\n     ops = " << res.ops
          << "\n  cancel = " << res.cancel << "\n destroy = " << res.destruction << std::endl;
}

template <class HandleType, size_t ReaderThreads> struct benchmark_llfio
{
  using mode = typename HandleType::mode;
  using creation = typename HandleType::creation;
  using caching = typename HandleType::caching;
  using flag = typename HandleType::flag;
  using buffer_type = typename HandleType::buffer_type;
  using const_buffer_type = typename HandleType::const_buffer_type;
  using buffers_type = typename HandleType::buffers_type;
  using const_buffers_type = typename HandleType::const_buffers_type;
  template <class T> using io_result = typename HandleType::template io_result<T>;

  struct read_state;
  struct receiver_type
  {
    read_state *state;
    inline void set_value(io_result<buffers_type> &&res);
    void set_done() {}
  };
  using state_type = llfio::io_operation_connection<llfio::async_read, receiver_type>;

  struct read_state
  {
    llfio::byte raw_buffer[1];
    buffer_type buffer;
    HandleType read_handle;
    llfio::optional<state_type> op_state;
    size_t ops{0};

    explicit read_state(HandleType &&h)
        : read_handle(std::move(h))
    {
    }
    void begin_io()
    {
      buffer = {raw_buffer, 1};
      op_state.emplace(connect(llfio::async_read(read_handle, {{buffer}, 0}), receiver_type{this}));
      op_state->start();
    }
  };

  std::unique_ptr<llfio::io_multiplexer> multiplexer;
  std::vector<HandleType> write_handles;
  std::vector<read_state> read_states;

  explicit benchmark_llfio(size_t count)
  {
    multiplexer = llfio::io_multiplexer::best_available(ReaderThreads).value();
    read_states.reserve(count);
    write_handles.reserve(count);
    // Construct read and write sides of the pipes
    llfio::filesystem::path::value_type name[64] = {'l', 'l', 'f', 'i', 'o', '_'};
    for(size_t n = 0; n < count; n++)
    {
      name[QUICKCPPLIB_NAMESPACE::algorithm::string::to_hex_string(name + 6, 58, (const char *) &n, sizeof(n))] = 0;
      read_states.emplace_back(llfio::construct<HandleType>{name, mode::read, creation::if_needed, caching::all, flag::multiplexable}().value());
      read_states.back().read_handle.set_multiplexer(multiplexer.get()).value();
      write_handles.push_back(llfio::construct<HandleType>{name, mode::write, creation::open_existing, caching::all, flag::multiplexable}().value());
    }
    // Begin the read i/o for all pipes
    for(auto &s : read_states)
    {
      s.begin_io();
    }
  }
  void read()
  {
    // Complete any i/o which has finished. The receiver will
    // post restart of i/o on their handle to the multiplexer.
    multiplexer->complete_io().value();

    // Restart i/o for the stuff which completed.
    multiplexer->invoke_posted_items().value();
  }
  void write(unsigned which)
  {
    llfio::byte c = llfio::to_byte(78);
    const_buffer_type b(&c, 1);
    write_handles[which].write({{b}, 0}).value();
  }
  void cancel() {
    do
    {
      for(auto &i : read_states)
      {
        i.op_state.reset();
      }
      multiplexer->complete_io().value();
    } while(multiplexer->invoke_posted_items().value() > 0);
  }
  size_t destroy()
  {
    write_handles.clear();
    size_t ret = 0;
    for(auto &i : read_states)
    {
      ret += i.ops;
    }
    read_states.clear();
    multiplexer.reset();
    return ret;
  }
};
template <class HandleType, size_t ReaderThreads> inline void benchmark_llfio<HandleType, ReaderThreads>::receiver_type::set_value(io_result<buffers_type> &&res)
{
  ++state->ops;
  if(!res)
  {
    std::cerr << res.error().message() << std::endl;
    abort();
  }
  // If the i/o completed immediately, we would recurse to infinity, so defer until later (see read())
  state->read_handle.multiplexer()->post([state = this->state]() { state->begin_io(); }).value();
}


int main(void)
{
  benchmark<benchmark_llfio<llfio::pipe_handle, 1>>("foo.csv", 1);
  return 0;
}