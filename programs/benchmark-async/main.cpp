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
static constexpr int BENCHMARK_DURATION = 10;

static constexpr size_t THREADS = 2;

/* ASIO IOCP:

Benchmarking struct benchmark_asio_pipe<1> with 1 handles ...
   creates 14367.8 reads 199862 cancels 53475.9 destroys 57142.9

Benchmarking struct benchmark_asio_pipe<1> with 1 handles ...
   creates 15873 reads 205075 cancels 57142.9 destroys 63291.1

Benchmarking struct benchmark_asio_pipe<1> with 1 handles ...
   creates 14513.8 reads 201082 cancels 58823.5 destroys 58823.5


LLFIO IOCP:

Benchmarking class llfio_v2_98e974b4::pipe_handle with 1 handles ...
   creates 22421.5 reads 200027 cancels 303030 destroys 50761.4

Benchmarking class llfio_v2_98e974b4::pipe_handle with 1 handles ...
   creates 20876.8 reads 199098 cancels 3.33333e+06 destroys 40816.3

Benchmarking class llfio_v2_98e974b4::pipe_handle with 1 handles ...
   creates 25252.5 reads 194343 cancels 285714 destroys 42553.2


LLFIO Alertable:

Benchmarking class llfio_v2_98e974b4::pipe_handle with 1 handles ...
   creates 23640.7 reads 111594 cancels 434783 destroys 9794.32

Benchmarking class llfio_v2_98e974b4::pipe_handle with 1 handles ...
   creates 24937.7 reads 111980 cancels 454545 destroys 10493.2
*/

#include "../../include/llfio/llfio.hpp"

#include "quickcpplib/algorithm/small_prng.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <typeinfo>
#include <vector>

#if __has_include("asio/include/asio.hpp")
#define ENABLE_ASIO 1
#if defined(__clang__) && defined(_MSC_VER)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-include"
#endif
#include "asio/include/asio.hpp"
#if defined(__clang__) && defined(_MSC_VER)
#pragma clang diagnostic pop
#endif
#endif

namespace llfio = LLFIO_V2_NAMESPACE;

struct test_results
{
  int handles{0};
  double creation{0}, ops{0}, cancel{0}, destruction{0};
};

template <class C, class... Args> test_results do_benchmark(int handles, Args &&... args)
{
  const bool doing_warm_up = (handles < 0);
  handles = abs(handles);
  auto create1 = std::chrono::high_resolution_clock::now();
  auto ios = C(handles, std::forward<Args>(args)...);
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
    if(!done && std::chrono::duration_cast<std::chrono::seconds>(ops2 - ops1).count() >= (doing_warm_up ? 3U : BENCHMARK_DURATION))
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

template <class C, class... Args> void benchmark(llfio::path_view csv, size_t max_handles, Args &&... args)
{
  std::vector<test_results> results;
  for(int n = 1; n <= max_handles; n <<= 2)
  {
    std::cout << "Benchmarking " << C::description() << " with " << n << " handles ..." << std::endl;
    auto res = do_benchmark<C>(n, std::forward<Args>(args)...);
    res.handles = n;
    results.push_back(res);
    std::cout << "   creates " << res.creation << " reads " << res.ops << " cancels " << res.cancel << " destroys " << res.destruction << std::endl;
  }
  std::ofstream of(csv.path());
  of << "Handles";
  for(auto &i : results)
  {
    of << "," << i.handles;
  }
  of << "\nCreates";
  for(auto &i : results)
  {
    of << "," << i.creation;
  }
  of << "\nReads";
  for(auto &i : results)
  {
    of << "," << i.ops;
  }
  of << "\nCancels";
  for(auto &i : results)
  {
    of << "," << i.cancel;
  }
  of << "\nDestroys";
  for(auto &i : results)
  {
    of << "," << i.destruction;
  }
  of << "\n";
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
    inline void set_done();
  };
  using state_type = llfio::io_operation_connection<llfio::async_read, receiver_type>;

  struct read_state
  {
    llfio::byte raw_buffer[1];
    buffer_type buffer;
    HandleType read_handle;
    size_t ops{0}, idx{0};
    llfio::optional<state_type> op_states[4];

    explicit read_state(HandleType &&h)
        : read_handle(std::move(h))
    {
    }
    void begin_io()
    {
      buffer = {raw_buffer, 1};
      if(!op_states[idx])
      {
        op_states[idx].emplace(connect(llfio::async_read(read_handle, {{buffer}, 0}), receiver_type{this}));
      }
      else
      {
        op_states[idx]->reset();
      }
      op_states[idx]->start();
      if(++idx == sizeof(op_states) / sizeof(op_states[0]))
      {
        idx = 0;
      }
    }
  };

  std::unique_ptr<llfio::io_multiplexer> multiplexer;
  std::vector<HandleType> write_handles;
  std::vector<read_state> read_states;

  static const char *description() noexcept
  {
#ifdef _MSC_VER
    return typeid(HandleType).name();
#else
    static const char *v = [] {
      size_t length = 0;
      int status = 0;
      return abi::__cxa_demangle(typeid(HandleType).name(), nullptr, &length, &status);
    }();
#endif
  }

  explicit benchmark_llfio(size_t count, std::unique_ptr<llfio::io_multiplexer> (*make_multiplexer)())
  {
    multiplexer = make_multiplexer();
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
    // Complete any i/o which has finished.
    if(0 == multiplexer->complete_io().value())
    {
      int a = 1;
      // abort();
      // std::this_thread::yield();
    }
  }
  void write(unsigned which)
  {
    llfio::byte c = llfio::to_byte(78);
    const_buffer_type b(&c, 1);
    write_handles[which].write({{b}, 0}).value();
  }
  void cancel()
  {
    for(auto &i : read_states)
    {
      i.op_states[0].reset();
      i.op_states[1].reset();
    }
  }
  size_t destroy()
  {
    size_t ret = 0;
    for(auto &i : read_states)
    {
      ret += i.ops;
    }
    read_states.clear();
    write_handles.clear();
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
  // Restart the i/o for this handle
  state->begin_io();
}
template <class HandleType, size_t ReaderThreads> inline void benchmark_llfio<HandleType, ReaderThreads>::receiver_type::set_done() {
}

#if ENABLE_ASIO
template <size_t ReaderThreads> struct benchmark_asio_pipe
{
#ifdef _WIN32
  using handle_type = asio::windows::stream_handle;
#else
  using handle_type = asio::posix::stream_descriptor;
#endif

  struct read_state
  {
    char raw_buffer[1];
    handle_type read_handle;
    size_t ops{0};

    explicit read_state(handle_type &&h)
        : read_handle(std::move(h))
    {
    }
    void begin_io()
    {
      read_handle.async_read_some(asio::buffer(raw_buffer, 1), [this](const auto & /*unused*/, const auto & /*unused*/) {
        ++ops;
        begin_io();
      });
    }
  };

  llfio::optional<asio::io_context> multiplexer;
  std::vector<handle_type> write_handles;
  std::vector<read_state> read_states;

  static const char *description() noexcept
  {
#ifdef _MSC_VER
    return typeid(benchmark_asio_pipe).name();
#else
    static const char *v = [] {
      size_t length = 0;
      int status = 0;
      return abi::__cxa_demangle(typeid(benchmark_asio_pipe).name(), nullptr, &length, &status);
    }();
#endif
  }

  explicit benchmark_asio_pipe(size_t count)
  {
    multiplexer.emplace((int) ReaderThreads);
    read_states.reserve(count);
    write_handles.reserve(count);
    // Construct read and write sides of the pipes
    llfio::filesystem::path::value_type name[64] = {'l', 'l', 'f', 'i', 'o', '_'};
    for(size_t n = 0; n < count; n++)
    {
      using mode = typename llfio::pipe_handle::mode;
      using creation = typename llfio::pipe_handle::creation;
      using caching = typename llfio::pipe_handle::caching;
      using flag = typename llfio::pipe_handle::flag;

      name[QUICKCPPLIB_NAMESPACE::algorithm::string::to_hex_string(name + 6, 58, (const char *) &n, sizeof(n))] = 0;
      auto read_handle = llfio::construct<llfio::pipe_handle>{name, mode::read, creation::if_needed, caching::all, flag::multiplexable}().value();
      auto write_handle = llfio::construct<llfio::pipe_handle>{name, mode::write, creation::open_existing, caching::all, flag::multiplexable}().value();
#ifdef _WIN32
      read_states.emplace_back(handle_type(*multiplexer, read_handle.release().h));
      write_handles.emplace_back(handle_type(*multiplexer, write_handle.release().h));
#else
      read_states.emplace_back(handle_type(*multiplexer, read_handle.release().fd));
      write_handles.emplace_back(handle_type(*multiplexer, write_handle.release().fd));
#endif
    }
    // Begin the read i/o for all pipes
    for(auto &s : read_states)
    {
      s.begin_io();
    }
  }
  void read()
  {
    // io_context::poll() does not return so long as there is completed i/o,
    // which isn't what I want. I also don't want it to block if there is
    // no pending i/o. The closest match is poll_one() I think. Internally,
    // ASIO's poll() is implemented using poll_one() in any case.
    multiplexer->poll_one();
  }
  void write(unsigned which)
  {
    char c = 78;
    write_handles[which].write_some(asio::buffer(&c, 1));
  }
  void cancel()
  {
    for(auto &i : read_states)
    {
      i.read_handle.cancel();
    }
    multiplexer->poll();  // does not return until no completions are pending
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
#endif

int main(void)
{
  using make_multiplexer_type = std::unique_ptr<llfio::io_multiplexer> (*)();
  static const make_multiplexer_type make_multiplexers[] = {
#ifdef _WIN32
  // +[]() -> std::unique_ptr<llfio::io_multiplexer> { return llfio::io_multiplexer::win_alertable().value(); }  //,
  +[]() -> std::unique_ptr<llfio::io_multiplexer> { return llfio::io_multiplexer::win_iocp(1).value(); }
#endif
  };
  std::cout << "Warming up ..." << std::endl;
  do_benchmark<benchmark_llfio<llfio::pipe_handle, 1>>(-1, make_multiplexers[0]);

  benchmark<benchmark_llfio<llfio::pipe_handle, 1>>("llfio-pipe-handle-1-readers.csv", 1, make_multiplexers[0]);
#if ENABLE_ASIO
  benchmark<benchmark_asio_pipe<1>>("asio-pipe-handle-1-readers.csv", 1);
#endif
  return 0;
}