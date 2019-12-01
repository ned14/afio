/* A profile of an OS and filing system
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (5 commits)
File Created: Dec 2015


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

#include "../../directory_handle.hpp"
#include "../../file_handle.hpp"
#include "../../statfs.hpp"
#include "../../storage_profile.hpp"
#include "../../utils.hpp"

#include "quickcpplib/algorithm/small_prng.hpp"

#include <future>
#include <vector>
#ifndef NDEBUG
#include <fstream>
#include <iostream>
#endif

#define LLFIO_STORAGE_PROFILE_TIME_DIVIDER 10

// Work around buggy Windows scheduler
//#define LLFIO_STORAGE_PROFILE_PIN_THREADS

LLFIO_V2_NAMESPACE_BEGIN

namespace storage_profile
{

  /* YAML's syntax is amazingly powerful ... we can express a map
  of a map to a map using this syntax:

  ?
  direct: 0
  sync: 0
  :
  concurrency:
  atomicity:
  min_atomic_write: 1
  max_atomic_write: 1

  Some YAML parsers appear to accept this more terse form too:

  {direct: 0, sync: 0}:
  concurrency:
  atomicity:
  min_atomic_write: 1
  max_atomic_write: 1

  We don't do any of this as some YAML parsers are basically JSON parsers with
  some rules relaxed. We just use:

  direct=0 sync=0:
  concurrency:
  atomicity:
  min_atomic_write: 1
  max_atomic_write: 1
  */
  void storage_profile::write(std::ostream &out, const std::regex &which, size_t _indent, bool invert_match) const
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    std::vector<std::string> lastsection;
    auto print = [_indent, &out, &lastsection](auto &i) {
      size_t indent = _indent;
      if(i.value != default_value<decltype(i.value)>())
      {
        std::vector<std::string> thissection;
        const char *s, *e;
        for(s = i.name, e = i.name; *e != 0; e++)
        {
          if(*e == ':')
          {
            thissection.emplace_back(s, e - s);
            s = e + 1;
          }
        }
        std::string name(s, e - s);
        for(size_t n = 0; n < thissection.size(); n++)
        {
          indent += 4;
          if(n >= lastsection.size() || thissection[n] != lastsection[n])
          {
            out << std::string(indent - 4, ' ') << thissection[n] << ":\n";
          }
        }
        if(i.description)
        {
          std::string text(i.description);
          std::vector<std::string> lines;
          for(;;)
          {
            size_t idx = 78;
            if(idx < text.size())
            {
              while(text[idx] != ' ')
              {
                --idx;
              }
            }
            else
            {
              idx = text.size();
            }
            lines.push_back(text.substr(0, idx));
            if(idx < text.size())
            {
              text = text.substr(idx + 1);
            }
            else
            {
              break;
            }
          }
          for(auto &line : lines)
          {
            out << std::string(indent, ' ') << "# " << line << "\n";
          }
        }
        out << std::string(indent, ' ') << name << ": " << i.value << "\n";
        if(i.description && strlen(i.description) > 78)
        {
          out << "\n";
        }
        lastsection = std::move(thissection);
      }
    };
    for(const item_erased &i : *this)
    {
      bool matches = std::regex_match(i.name, which);
      if((matches && !invert_match) || (!matches && invert_match))
      {
        i.invoke(print);
      }
    }
  }

  namespace system
  {
    // System memory quantity, in use, max and min bandwidth
    outcome<void> mem(storage_profile &sp, file_handle &h) noexcept
    {
      static unsigned long long mem_quantity, mem_max_bandwidth, mem_min_bandwidth;
      static float mem_in_use;
      if(mem_quantity != 0u)
      {
        sp.mem_quantity.value = mem_quantity;
        sp.mem_in_use.value = mem_in_use;
        sp.mem_max_bandwidth.value = mem_max_bandwidth;
        sp.mem_min_bandwidth.value = mem_min_bandwidth;
      }
      else
      {
        try
        {
          size_t chunksize = 256 * 1024 * 1024;
#ifdef WIN32
          OUTCOME_TRYV(windows::_mem(sp, h));
#else
          OUTCOME_TRYV(posix::_mem(sp, h));
#endif

          if(sp.mem_quantity.value / 4 < chunksize)
          {
            chunksize = static_cast<size_t>(sp.mem_quantity.value / 4);
          }
          char *buffer = utils::page_allocator<char>().allocate(chunksize);
          auto unbuffer = LLFIO_V2_NAMESPACE::undoer([buffer, chunksize] { utils::page_allocator<char>().deallocate(buffer, chunksize); });
          // Make sure all memory is really allocated first
          memset(buffer, 1, chunksize);

          // Max bandwidth is sequential writes of min(25% of system memory or 256Mb)
          auto begin = std::chrono::high_resolution_clock::now();
          unsigned long long count;
          for(count = 0; std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - begin).count() < (10 / LLFIO_STORAGE_PROFILE_TIME_DIVIDER); count++)
          {
            memset(buffer, count & 0xff, chunksize);
          }
          sp.mem_max_bandwidth.value = static_cast<unsigned long long>(static_cast<double>(count) * chunksize / 10);

          // Min bandwidth is randomised 4Kb copies of the same
          QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng ctx(78);
          begin = std::chrono::high_resolution_clock::now();
          for(count = 0; std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - begin).count() < (10 / LLFIO_STORAGE_PROFILE_TIME_DIVIDER); count++)
          {
            for(size_t n = 0; n < chunksize; n += 4096)
            {
              auto offset = ctx() * 4096;
              offset = offset % chunksize;
              memset(buffer + offset, count & 0xff, 4096);
            }
          }
          sp.mem_min_bandwidth.value = static_cast<unsigned long long>(static_cast<double>(count) * chunksize / 10);
        }
        catch(...)
        {
          return std::current_exception();
        }
        mem_quantity = sp.mem_quantity.value;
        mem_in_use = sp.mem_in_use.value;
        mem_max_bandwidth = sp.mem_max_bandwidth.value;
        mem_min_bandwidth = sp.mem_min_bandwidth.value;
      }
      return success();
    }

    // High resolution clock granularity
    struct clock_info_t
    {
      unsigned granularity;  // in nanoseconds
      unsigned overhead;     // in nanoseconds
    };
    inline clock_info_t _clock_granularity_and_overhead()
    {
      static clock_info_t info;
      if(info.granularity == 0u)
      {
        auto count = static_cast<unsigned>(-1);
        for(size_t n = 0; n < 20; n++)
        {
          std::chrono::high_resolution_clock::time_point begin = std::chrono::high_resolution_clock::now(), end;
          do
          {
            end = std::chrono::high_resolution_clock::now();
          } while(begin == end);
          auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
          if(diff < count)
          {
            count = static_cast<unsigned>(diff);
          }
        }
        info.granularity = count;
        std::chrono::high_resolution_clock::time_point begin = std::chrono::high_resolution_clock::now();
        for(size_t n = 0; n < 1000000; n++)
        {
          (void) std::chrono::high_resolution_clock::now();
        }
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        info.overhead = static_cast<unsigned>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / 1000000);
      }
      return info;
    }
    outcome<void> clock_granularity(storage_profile &sp, file_handle & /*unused*/) noexcept
    {
      auto info = _clock_granularity_and_overhead();
      sp.clock_granularity.value = info.granularity;
      sp.clock_overhead.value = info.overhead;
      return success();
    }
    inline unsigned _sleep_wake_overhead()
    {
      static unsigned v;
      if(v == 0u)
      {
        unsigned count = static_cast<unsigned>(-1), period = 1000;  // 1us
        for(size_t n = 0; n < 20; n++)
        {
          std::chrono::high_resolution_clock::time_point begin, end;
          unsigned diff;
          do
          {
            begin = std::chrono::high_resolution_clock::now();
            std::this_thread::sleep_for(std::chrono::nanoseconds(period));
            end = std::chrono::high_resolution_clock::now();
            period *= 2;  // 2^20 = ~1ms
            diff = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
          } while(diff == 0);
          if(diff < count)
          {
            count = diff;
          }
        }
        v = count;
      }
      return v;
    }
    outcome<void> sleep_wake_overhead(storage_profile &sp, file_handle & /*unused*/) noexcept
    {
      sp.sleep_wake_overhead.value = _sleep_wake_overhead();
      return success();
    }
    inline unsigned _yield_overhead()
    {
      static unsigned v;
      if(v == 0u)
      {
        auto count = static_cast<unsigned>(-1);
        for(size_t n = 0; n < 20; n++)
        {
          std::chrono::high_resolution_clock::time_point begin, end;
          begin = std::chrono::high_resolution_clock::now();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          std::this_thread::yield();
          end = std::chrono::high_resolution_clock::now();
          unsigned diff = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
          if(diff < count)
          {
            count = diff / 10;
          }
        }
        v = count;
      }
      return v;
    }
    outcome<void> yield_overhead(storage_profile &sp, file_handle & /*unused*/) noexcept
    {
      sp.yield_overhead.value = _yield_overhead();
      return success();
    }
    outcome<void> drop_filesystem_cache_support(storage_profile &sp, file_handle & /*unused*/) noexcept
    {
      static bool v = !!utils::drop_filesystem_cache();
      sp.drop_filesystem_cache_support.value = static_cast<unsigned int>(v);
      return success();
    }
  }  // namespace system
  namespace storage
  {
    // Device name, size, min i/o size
    outcome<void> device(storage_profile &sp, file_handle &h) noexcept
    {
      try
      {
        statfs_t fsinfo;
        OUTCOME_TRYV(fsinfo.fill(h, statfs_t::want::iosize | statfs_t::want::mntfromname | statfs_t::want::fstypename));
        sp.device_min_io_size.value = static_cast<unsigned>(fsinfo.f_iosize);
#ifdef WIN32
        OUTCOME_TRYV(windows::_device(sp, h, fsinfo.f_mntfromname, fsinfo.f_fstypename));
#else
        OUTCOME_TRYV(posix::_device(sp, h, fsinfo.f_mntfromname, fsinfo.f_fstypename));
#endif
      }
      catch(...)
      {
        return std::current_exception();
      }
      return success();
    }
    // FS name, config, size, in use
    outcome<void> fs(storage_profile &sp, file_handle &h) noexcept
    {
      try
      {
        statfs_t fsinfo;
        OUTCOME_TRYV(fsinfo.fill(h));
        sp.fs_name.value = fsinfo.f_fstypename;
        sp.fs_config.value = "todo";
        sp.fs_size.value = fsinfo.f_blocks * fsinfo.f_bsize;
        sp.fs_in_use.value = static_cast<float>(fsinfo.f_blocks - fsinfo.f_bfree) / fsinfo.f_blocks;
      }
      catch(...)
      {
        return std::current_exception();
      }
      return success();
    }
  }  // namespace storage

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4459)  // off_t shadows global namespace
#endif
  namespace concurrency
  {
    outcome<void> atomic_rewrite_quantum(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.atomic_rewrite_quantum.value != static_cast<io_handle::extent_type>(-1))
      {
        return success();
      }
      try
      {
        using off_t = io_handle::extent_type;
        sp.max_aligned_atomic_rewrite.value = 1;
        sp.atomic_rewrite_quantum.value = static_cast<off_t>(-1);
        size_t size = srch.requires_aligned_io() ?
#ifdef _WIN32
                      4096
#else
                      512
#endif
                      :
                      64;
        for(; size <= 1 * 1024 * 1024 && size < sp.atomic_rewrite_quantum.value; size = size * 2)
        {
          // Create two concurrent writer threads and as many reader threads as additional CPU cores
          // The excessive unique_ptr works around a bug in libc++'s thread implementation
          std::vector<std::pair<std::unique_ptr<std::thread>, std::future<void>>> writers, readers;
          std::atomic<size_t> done(2);
          for(char no = '1'; no <= '2'; no++)
          {
            std::packaged_task<void()> task([size, &srch, no, &done] {
              auto _h(srch.clone());
              if(!_h)
              {
                throw std::runtime_error(std::string("concurrency::atomic_rewrite_quantum: "  // NOLINT
                                                     "Could not open work file due to ") +
                                         _h.error().message().c_str());
              }
              file_handle h(std::move(_h.value()));
              std::vector<byte, utils::page_allocator<byte>> buffer(size, to_byte(no));
              file_handle::const_buffer_type _reqs[1] = {{buffer.data(), size}};
              file_handle::io_request<file_handle::const_buffers_type> reqs(_reqs, 0);
              --done;
              while(done != 0u)
              {
                std::this_thread::yield();
              }
              while(done == 0u)
              {
                h.write(reqs).value();
              }
            });
            auto f(task.get_future());
            writers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
          }
          // Wait till the writers launch
          while(done != 0u)
          {
            std::this_thread::yield();
          }
          unsigned concurrency = std::thread::hardware_concurrency() - 2;
          if(concurrency < 4)
          {
            concurrency = 4;
          }
          std::atomic<io_handle::extent_type> atomic_rewrite_quantum(sp.atomic_rewrite_quantum.value);
          std::atomic<bool> failed(false);
          for(unsigned no = 0; no < concurrency; no++)
          {
            std::packaged_task<void()> task([size, &srch, &done, &atomic_rewrite_quantum, &failed] {
              auto _h(srch.clone());
              if(!_h)
              {
                throw std::runtime_error(std::string("concurrency::atomic_rewrite_quantum: "  // NOLINT
                                                     "Could not open work file due to ") +
                                         _h.error().message().c_str());
              }
              file_handle h(std::move(_h.value()));
              std::vector<byte, utils::page_allocator<byte>> buffer(size, to_byte(0)), tocmp(size, to_byte(0));
              file_handle::buffer_type _reqs[1] = {{buffer.data(), size}};
              file_handle::io_request<file_handle::buffers_type> reqs(_reqs, 0);
              while(done == 0u)
              {
                h.read(reqs).value();
                // memset(tocmp.data(), buffer.front(), size);
                // if (memcmp(buffer.data(), tocmp.data(), size))
                {
                  const size_t *data = reinterpret_cast<size_t *>(buffer.data()), *end = reinterpret_cast<size_t *>(buffer.data() + size);
                  for(const size_t *d = data; d < end; d++)
                  {
                    if(*d != *data)
                    {
                      failed = true;
                      off_t failedat = d - data;
                      if(failedat < atomic_rewrite_quantum)
                      {
#ifndef NDEBUG
                        std::cout << "  Torn rewrite at offset " << failedat << std::endl;
#endif
                        atomic_rewrite_quantum = failedat;
                      }
                      break;
                    }
                  }
                }
              }
            });
            auto f(task.get_future());
            readers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
          }

#ifndef NDEBUG
          std::cout << "direct=" << !srch.are_reads_from_cache() << " sync=" << srch.are_writes_durable() << " testing atomicity of rewrites of " << size << " bytes ..." << std::endl;
#endif
          auto begin = std::chrono::high_resolution_clock::now();
          while(!failed && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - begin).count() < (20 / LLFIO_STORAGE_PROFILE_TIME_DIVIDER))
          {
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
          done = 1u;
          for(auto &writer : writers)
          {
            writer.first->join();
          }
          for(auto &reader : readers)
          {
            reader.first->join();
          }
          for(auto &writer : writers)
          {
            writer.second.get();
          }
          for(auto &reader : readers)
          {
            reader.second.get();
          }
          sp.atomic_rewrite_quantum.value = atomic_rewrite_quantum;
          if(!failed)
          {
            if(size > sp.max_aligned_atomic_rewrite.value)
            {
              sp.max_aligned_atomic_rewrite.value = size;
            }
          }
          else
          {
            break;
          }
        }
        if(sp.atomic_rewrite_quantum.value > sp.max_aligned_atomic_rewrite.value)
        {
          sp.atomic_rewrite_quantum.value = sp.max_aligned_atomic_rewrite.value;
        }

        // If burst quantum exceeds rewrite quantum, make sure it does so at
        // offsets not at the front of the file
        if(sp.max_aligned_atomic_rewrite.value > sp.atomic_rewrite_quantum.value)
        {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4456)  // declaration hides previous local declaration
#endif
          auto size = static_cast<size_t>(sp.max_aligned_atomic_rewrite.value);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
          for(off_t offset = sp.max_aligned_atomic_rewrite.value; offset < sp.max_aligned_atomic_rewrite.value * 4; offset += sp.max_aligned_atomic_rewrite.value)
          {
            // Create two concurrent writer threads and as many reader threads as additional CPU cores
            // The excessive unique_ptr works around a bug in libc++'s thread implementation
            std::vector<std::pair<std::unique_ptr<std::thread>, std::future<void>>> writers, readers;
            std::atomic<size_t> done(2);
            for(char no = '1'; no <= '2'; no++)
            {
              std::packaged_task<void()> task([size, offset, &srch, no, &done] {
                auto _h(srch.clone());
                if(!_h)
                {
                  throw std::runtime_error(std::string("concurrency::atomic_rewrite_"  // NOLINT
                                                       "quantum: Could not open work file "
                                                       "due to ") +
                                           _h.error().message().c_str());
                }
                file_handle h(std::move(_h.value()));
                std::vector<byte, utils::page_allocator<byte>> buffer(size, to_byte(no));
                file_handle::const_buffer_type _reqs[1] = {{buffer.data(), size}};
                file_handle::io_request<file_handle::const_buffers_type> reqs(_reqs, offset);
                --done;
                while(done != 0u)
                {
                  std::this_thread::yield();
                }
                while(done == 0u)
                {
                  h.write(reqs).value();
                }
              });
              auto f(task.get_future());
              writers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
            }
            // Wait till the writers launch
            while(done != 0u)
            {
              std::this_thread::yield();
            }
            unsigned concurrency = std::thread::hardware_concurrency() - 2;
            if(concurrency < 4)
            {
              concurrency = 4;
            }
            std::atomic<io_handle::extent_type> max_aligned_atomic_rewrite(sp.max_aligned_atomic_rewrite.value);
            std::atomic<bool> failed(false);
            for(unsigned no = 0; no < concurrency; no++)
            {
              std::packaged_task<void()> task([size, offset, &srch, &done, &max_aligned_atomic_rewrite, &failed] {
                auto _h(srch.clone());
                if(!_h)
                {
                  throw std::runtime_error(std::string("concurrency::atomic_rewrite_"  // NOLINT
                                                       "quantum: Could not open work file "
                                                       "due to ") +
                                           _h.error().message().c_str());
                }
                file_handle h(std::move(_h.value()));
                std::vector<byte, utils::page_allocator<byte>> buffer(size, to_byte(0)), tocmp(size, to_byte(0));
                file_handle::buffer_type _reqs[1] = {{buffer.data(), size}};
                file_handle::io_request<file_handle::buffers_type> reqs(_reqs, offset);
                while(done == 0u)
                {
                  h.read(reqs).value();
                  // memset(tocmp.data(), buffer.front(), size);
                  // if (memcmp(buffer.data(), tocmp.data(), size))
                  {
                    const size_t *data = reinterpret_cast<size_t *>(buffer.data()), *end = reinterpret_cast<size_t *>(buffer.data() + size);
                    for(const size_t *d = data; d < end; d++)
                    {
                      if(*d != *data)
                      {
                        failed = true;
                        off_t failedat = (d - data);
                        if(failedat < max_aligned_atomic_rewrite)
                        {
#ifndef NDEBUG
                          std::cout << "  Torn rewrite at offset " << failedat << std::endl;
#endif
                          max_aligned_atomic_rewrite = failedat;
                        }
                        break;
                      }
                    }
                  }
                }
              });
              auto f(task.get_future());
              readers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
            }

#ifndef NDEBUG
            std::cout << "direct=" << !srch.are_reads_from_cache() << " sync=" << srch.are_writes_durable() << " testing atomicity of rewrites of " << size << " bytes to offset " << offset << " ..." << std::endl;
#endif
            auto begin = std::chrono::high_resolution_clock::now();
            while(!failed && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - begin).count() < (20 / LLFIO_STORAGE_PROFILE_TIME_DIVIDER))
            {
              std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            done = 1u;
            for(auto &writer : writers)
            {
              writer.first->join();
            }
            for(auto &reader : readers)
            {
              reader.first->join();
            }
            for(auto &writer : writers)
            {
              writer.second.get();
            }
            for(auto &reader : readers)
            {
              reader.second.get();
            }
            sp.max_aligned_atomic_rewrite.value = max_aligned_atomic_rewrite;
            if(failed)
            {
              return success();
            }
          }
        }
      }
      catch(...)
      {
        return std::current_exception();
      }
      return success();
    }

    outcome<void> atomic_rewrite_offset_boundary(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.atomic_rewrite_offset_boundary.value != static_cast<io_handle::extent_type>(-1))
      {
        return success();
      }
#ifdef _WIN32  // The 4Kb min i/o makes this test take too long
      if(srch.requires_aligned_io())
      {
        return success();
      }
#endif
      try
      {
        using off_t = io_handle::extent_type;
        auto size = static_cast<size_t>(sp.max_aligned_atomic_rewrite.value);
        auto maxsize = static_cast<size_t>(sp.max_aligned_atomic_rewrite.value);
        if(size > 1024)
        {
          size = 1024;
        }
        if(maxsize > 8192)
        {
          maxsize = 8192;
        }
        sp.atomic_rewrite_offset_boundary.value = static_cast<off_t>(-1);
        if(size > 1)
        {
          for(; size <= maxsize; size = size * 2)
          {
            for(off_t offset = 512; offset < size; offset += 512)
            {
              // Create two concurrent writer threads and as many reader threads as additional CPU cores
              // The excessive unique_ptr works around a bug in libc++'s thread implementation
              std::vector<std::pair<std::unique_ptr<std::thread>, std::future<void>>> writers, readers;
              std::atomic<size_t> done(2);
              for(char no = '1'; no <= '2'; no++)
              {
                std::packaged_task<void()> task([size, offset, &srch, no, &done] {
                  auto _h(srch.clone());
                  if(!_h)
                  {
                    throw std::runtime_error(std::string("concurrency::atomic_rewrite_"  // NOLINT
                                                         "offset_boundary: Could not open "
                                                         "work file due to ") +
                                             _h.error().message().c_str());
                  }
                  file_handle h(std::move(_h.value()));
                  std::vector<byte, utils::page_allocator<byte>> buffer(size, to_byte(no));
                  file_handle::const_buffer_type _reqs[1] = {{buffer.data(), size}};
                  file_handle::io_request<file_handle::const_buffers_type> reqs(_reqs, offset);
                  --done;
                  while(done != 0u)
                  {
                    std::this_thread::yield();
                  }
                  while(done == 0u)
                  {
                    h.write(reqs).value();
                  }
                });
                auto f(task.get_future());
                writers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
              }
              // Wait till the writers launch
              while(done != 0u)
              {
                std::this_thread::yield();
              }
              unsigned concurrency = std::thread::hardware_concurrency() - 2;
              if(concurrency < 4)
              {
                concurrency = 4;
              }
              std::atomic<io_handle::extent_type> atomic_rewrite_offset_boundary(sp.atomic_rewrite_offset_boundary.value);
              std::atomic<bool> failed(false);
              for(unsigned no = 0; no < concurrency; no++)
              {
                std::packaged_task<void()> task([size, offset, &srch, &done, &atomic_rewrite_offset_boundary, &failed] {
                  auto _h(srch.clone());
                  if(!_h)
                  {
                    throw std::runtime_error(std::string("concurrency::atomic_rewrite_"  // NOLINT
                                                         "offset_boundary: Could not open "
                                                         "work file due to ") +
                                             _h.error().message().c_str());
                  }
                  file_handle h(std::move(_h.value()));
                  std::vector<byte, utils::page_allocator<byte>> buffer(size, to_byte(0)), tocmp(size, to_byte(0));
                  file_handle::buffer_type _reqs[1] = {{buffer.data(), size}};
                  file_handle::io_request<file_handle::buffers_type> reqs(_reqs, offset);
                  while(done == 0u)
                  {
                    h.read(reqs).value();
                    // memset(tocmp.data(), buffer.front(), size);
                    // if (memcmp(buffer.data(), tocmp.data(), size))
                    {
                      const size_t *data = reinterpret_cast<size_t *>(buffer.data()), *end = reinterpret_cast<size_t *>(buffer.data() + size);
                      for(const size_t *d = data; d < end; d++)
                      {
                        if(*d != *data)
                        {
                          failed = true;
                          off_t failedat = (d - data) + offset;
                          if(failedat < atomic_rewrite_offset_boundary)
                          {
#ifndef NDEBUG
                            std::cout << "  Torn rewrite at offset " << failedat << std::endl;
#endif
                            atomic_rewrite_offset_boundary = failedat;
                          }
                          break;
                        }
                      }
                    }
                  }
                });
                auto f(task.get_future());
                readers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
              }

#ifndef NDEBUG
              std::cout << "direct=" << !srch.are_reads_from_cache() << " sync=" << srch.are_writes_durable() << " testing atomicity of rewrites of " << size << " bytes to offset " << offset << " ..." << std::endl;
#endif
              auto begin = std::chrono::high_resolution_clock::now();
              while(!failed && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - begin).count() < (20 / LLFIO_STORAGE_PROFILE_TIME_DIVIDER))
              {
                std::this_thread::sleep_for(std::chrono::seconds(1));
              }
              done = 1u;
              for(auto &writer : writers)
              {
                writer.first->join();
              }
              for(auto &reader : readers)
              {
                reader.first->join();
              }
              for(auto &writer : writers)
              {
                writer.second.get();
              }
              for(auto &reader : readers)
              {
                reader.second.get();
              }
              sp.atomic_rewrite_offset_boundary.value = atomic_rewrite_offset_boundary;
              if(failed)
              {
                return success();
              }
            }
          }
        }
      }
      catch(...)
      {
        return std::current_exception();
      }
      return success();
    }
  }  // namespace concurrency
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  namespace latency
  {
    struct stats
    {
      unsigned long long min{0}, mean{0}, max{0}, _50{0}, _95{0}, _99{0}, _99999{0};
    };
    inline outcome<stats> _latency_test(file_handle &srch, size_t noreaders, size_t nowriters, bool ownfiles)
    {
      static constexpr size_t memory_to_use = 128 * 1024 * 1024;  // 1Gb
      // static const unsigned clock_overhead = system::_clock_granularity_and_overhead().overhead;
      static const unsigned clock_granularity = system::_clock_granularity_and_overhead().granularity;
      try
      {
        std::vector<file_handle> _workfiles;
        _workfiles.reserve(noreaders + nowriters);
        std::vector<file_handle *> workfiles(noreaders + nowriters, &srch);
        path_handle base = srch.parent_path_handle().value();
        if(ownfiles)
        {
          std::vector<byte, utils::page_allocator<byte>> buffer(1024 * 1024 * 1024);
          for(size_t n = 0; n < noreaders + nowriters; n++)
          {
            auto fh = file_handle::file(base, std::to_string(n), file_handle::mode::write, file_handle::creation::open_existing, srch.kernel_caching(), srch.flags());
            if(!fh)
            {
              fh = file_handle::file(base, std::to_string(n), file_handle::mode::write, file_handle::creation::if_needed, srch.kernel_caching(), srch.flags() | file_handle::flag::unlink_on_first_close);
              fh.value().write(0, {{buffer.data(), buffer.size()}}).value();
            }
            _workfiles.push_back(std::move(fh.value()));
            workfiles[n] = &_workfiles.back();
          }
        }
        (void) utils::drop_filesystem_cache();

        std::vector<std::vector<unsigned long long>> results(noreaders + nowriters);
        // The excessive unique_ptr works around a bug in libc++'s thread implementation
        std::vector<std::pair<std::unique_ptr<std::thread>, std::future<void>>> writers, readers;
        std::atomic<size_t> done(noreaders + nowriters);
        for(auto &i : results)
        {
          i.resize(memory_to_use / results.size());
          memset(i.data(), 0, i.size() * sizeof(unsigned long long));  // prefault
          i.resize(0);
        }
        for(size_t no = 0; no < nowriters; no++)
        {
          std::packaged_task<void()> task([no, &done, &workfiles, &results] {
#ifdef LLFIO_STORAGE_PROFILE_PIN_THREADS
            SetThreadAffinityMask(GetCurrentThread(), 1ULL << (no * 2));
#endif
            file_handle &h = *workfiles[no];
            alignas(4096) byte buffer[4096];
            memset(buffer, static_cast<int>(no), 4096);
            file_handle::const_buffer_type _reqs[1] = {{buffer, 4096}};
            file_handle::io_request<file_handle::const_buffers_type> reqs(_reqs, 0);
            QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng rand(static_cast<uint32_t>(no));
            auto maxsize = h.maximum_extent().value();
            --done;
            while(done != 0u)
            {
              std::this_thread::yield();
            }
            while(done == 0u)
            {
              reqs.offset = (rand() % maxsize) & ~4095ULL;
              auto begin = std::chrono::high_resolution_clock::now();
              h.write(reqs).value();
              auto end = std::chrono::high_resolution_clock::now();
              auto ns = (std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
              if(ns == 0)
              {
                ns = clock_granularity / 2;
              }
              results[no].push_back(ns);
              if(results[no].size() == results[no].capacity())
              {
                return;
              }
            }
          });
          auto f(task.get_future());
          writers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
        }
        for(size_t no = nowriters; no < nowriters + noreaders; no++)
        {
          std::packaged_task<void()> task([no, &done, &workfiles, &results] {
#ifdef LLFIO_STORAGE_PROFILE_PIN_THREADS
            SetThreadAffinityMask(GetCurrentThread(), 1ULL << (no * 2));
#endif
            file_handle &h = *workfiles[no];
            alignas(4096) byte buffer[4096];
            memset(buffer, static_cast<int>(no), 4096);
            file_handle::buffer_type _reqs[1] = {{buffer, 4096}};
            file_handle::io_request<file_handle::buffers_type> reqs(_reqs, 0);
            QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng rand(static_cast<uint32_t>(no));
            auto maxsize = h.maximum_extent().value();
            --done;
            while(done != 0u)
            {
              std::this_thread::yield();
            }
            while(done == 0u)
            {
              reqs.offset = (rand() % maxsize) & ~4095ULL;
              auto begin = std::chrono::high_resolution_clock::now();
              h.read(reqs).value();
              auto end = std::chrono::high_resolution_clock::now();
              auto ns = (std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());  // / 16;
              if(ns == 0)
              {
                ns = clock_granularity / 2;
              }
              results[no].push_back(ns);
              if(results[no].size() == results[no].capacity())
              {
                return;
              }
            }
          });
          auto f(task.get_future());
          readers.emplace_back(std::make_unique<std::thread>(std::move(task)), std::move(f));
        }
        // Wait till the readers and writers launch
        while(done != 0u)
        {
          std::this_thread::yield();
        }
        auto begin = std::chrono::high_resolution_clock::now();
        while(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - begin).count() < (10))
        {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        done = 1u;
        for(auto &writer : writers)
        {
          writer.first->join();
        }
        for(auto &reader : readers)
        {
          reader.first->join();
        }
        for(auto &writer : writers)
        {
          writer.second.get();
        }
        for(auto &reader : readers)
        {
          reader.second.get();
        }

#if 0  // ndef NDEBUG
        {
          std::ofstream out("latencies.csv");
          for(const auto &r : results)
          {
            for(const auto &i : r)
            {
              out << i << "\n";
            }
          }
        }
#endif
        std::vector<unsigned long long> totalresults;
        unsigned long long sum = 0;
        stats s;
        s.min = static_cast<unsigned long long>(-1);
        for(auto &result : results)
        {
          for(const auto &i : result)
          {
            if(i < s.min)
            {
              s.min = i;
            }
            if(i > s.max)
            {
              s.max = i;
            }
            sum += i;
            totalresults.push_back(i);
          }
          result.clear();
          result.shrink_to_fit();
        }
#ifndef NDEBUG
        std::cout << "Total results = " << totalresults.size() << std::endl;
#endif
        s.mean = static_cast<unsigned long long>(static_cast<double>(sum) / totalresults.size());
        // Latency distributions are definitely not normally distributed, but here we have the
        // advantage of tons of sample points. So simply sort into order, and pluck out the values
        // at 99.999%, 99% and 95%. It'll be accurate enough.
        std::sort(totalresults.begin(), totalresults.end());
        s._50 = totalresults[static_cast<size_t>(0.5 * totalresults.size())];
        s._95 = totalresults[static_cast<size_t>(0.95 * totalresults.size())];
        s._99 = totalresults[static_cast<size_t>(0.99 * totalresults.size())];
        s._99999 = totalresults[static_cast<size_t>(0.99999 * totalresults.size())];
        return s;
      }
      catch(...)
      {
        return std::current_exception();
      }
    }
    outcome<void> read_qd1(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.read_qd1_mean.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      OUTCOME_TRY(s, _latency_test(srch, 1, 0, false));
      sp.read_qd1_min.value = s.min;
      sp.read_qd1_mean.value = s.mean;
      sp.read_qd1_max.value = s.max;
      sp.read_qd1_50.value = s._50;
      sp.read_qd1_95.value = s._95;
      sp.read_qd1_99.value = s._99;
      sp.read_qd1_99999.value = s._99999;
      return success();
    }
    outcome<void> write_qd1(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.write_qd1_mean.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      OUTCOME_TRY(s, _latency_test(srch, 0, 1, false));
      sp.write_qd1_min.value = s.min;
      sp.write_qd1_mean.value = s.mean;
      sp.write_qd1_max.value = s.max;
      sp.write_qd1_50.value = s._50;
      sp.write_qd1_95.value = s._95;
      sp.write_qd1_99.value = s._99;
      sp.write_qd1_99999.value = s._99999;
      return success();
    }
    outcome<void> read_qd16(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.read_qd16_mean.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      OUTCOME_TRY(s, _latency_test(srch, 16, 0, true));
      sp.read_qd16_min.value = s.min;
      sp.read_qd16_mean.value = s.mean;
      sp.read_qd16_max.value = s.max;
      sp.read_qd16_50.value = s._50;
      sp.read_qd16_95.value = s._95;
      sp.read_qd16_99.value = s._99;
      sp.read_qd16_99999.value = s._99999;
      return success();
    }
    outcome<void> write_qd16(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.write_qd16_mean.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      OUTCOME_TRY(s, _latency_test(srch, 0, 16, true));
      sp.write_qd16_min.value = s.min;
      sp.write_qd16_mean.value = s.mean;
      sp.write_qd16_max.value = s.max;
      sp.write_qd16_50.value = s._50;
      sp.write_qd16_95.value = s._95;
      sp.write_qd16_99.value = s._99;
      sp.write_qd16_99999.value = s._99999;
      return success();
    }
    outcome<void> readwrite_qd4(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.readwrite_qd4_mean.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      OUTCOME_TRY(s, _latency_test(srch, 3, 1, true));
      sp.readwrite_qd4_min.value = s.min;
      sp.readwrite_qd4_mean.value = s.mean;
      sp.readwrite_qd4_max.value = s.max;
      sp.readwrite_qd4_50.value = s._50;
      sp.readwrite_qd4_95.value = s._95;
      sp.readwrite_qd4_99.value = s._99;
      sp.readwrite_qd4_99999.value = s._99999;
      return success();
    }
    outcome<void> read_nothing(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.read_nothing.value != static_cast<unsigned>(-1))
      {
        return success();
      }
      volatile size_t errors = 0;
      auto begin = std::chrono::high_resolution_clock::now();
      for(size_t n = 0; n < 1000000; n++)
      {
        if(!srch.read(0, {{nullptr, 0}}))
        {
          ++errors;
        }
      }
      auto end = std::chrono::high_resolution_clock::now();
      auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
      sp.read_nothing.value = static_cast<unsigned>(diff / 1000000);
      return success();
    }
    outcome<void> write_nothing(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.write_nothing.value != static_cast<unsigned>(-1))
      {
        return success();
      }
      volatile size_t errors = 0;
      auto begin = std::chrono::high_resolution_clock::now();
      for(size_t n = 0; n < 1000000; n++)
      {
        if(!srch.write(0, {{nullptr, 0}}))
        {
          ++errors;
        }
      }
      auto end = std::chrono::high_resolution_clock::now();
      auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
      sp.write_nothing.value = static_cast<unsigned>(diff / 1000000);
      return success();
    }
  }  // namespace latency
  namespace response_time
  {
    struct stats
    {
      unsigned long long create{0}, enumerate{0}, open_read{0}, open_write{0}, destroy{0};
    };
    inline outcome<stats> _traversal_N(file_handle &srch, size_t no, size_t bytes, bool cold_cache, bool race_free, bool reduced) noexcept
    {
      stats s;
#ifdef LLFIO_STORAGE_PROFILE_PIN_THREADS
      SetThreadAffinityMask(GetCurrentThread(), 1ULL << (no * 2));
#endif
      try
      {
        directory_handle dirh(directory_handle::directory(srch.parent_path_handle().value(), "testdir", directory_handle::mode::write, directory_handle::creation::if_needed).value());
        auto flags = srch.flags();
        std::string filename;
        filename.reserve(16);
        std::chrono::high_resolution_clock::time_point begin, end;
        alignas(4096) byte buffer[4096];
        memset(buffer, 78, sizeof(buffer));
        if(!race_free)
        {
          flags |= handle::flag::disable_safety_unlinks | handle::flag::win_disable_unlink_emulation;
        }
        if(srch.requires_aligned_io())
        {
          bytes = utils::round_down_to_page_size(bytes, utils::page_size());
        }

        if(cold_cache)
        {
          OUTCOME_TRYV(utils::drop_filesystem_cache());
        }
        begin = std::chrono::high_resolution_clock::now();
        for(size_t n = 0; n < no; n++)
        {
          filename = std::to_string(n);
          file_handle fileh(file_handle::file(dirh, filename, file_handle::mode::write, file_handle::creation::if_needed, srch.kernel_caching(), flags).value());
          if(bytes > 0)
          {
            fileh.write(0, {{buffer, bytes}}).value();
          }
        }
        if(srch.kernel_caching() == file_handle::caching::reads || srch.kernel_caching() == file_handle::caching::none)
        {
          (void) utils::flush_modified_data();
        }
        end = std::chrono::high_resolution_clock::now();
        s.create = static_cast<unsigned long long>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / no);
        if(cold_cache)
        {
          (void) utils::drop_filesystem_cache();
        }

        std::vector<directory_entry> entries(no);
        begin = std::chrono::high_resolution_clock::now();
        directory_handle::buffers_type ei(dirh.read(directory_handle::buffers_type(entries)).value());
        assert(ei.done() == true);
        assert(ei.size() == no);
        end = std::chrono::high_resolution_clock::now();
        s.enumerate = static_cast<unsigned long long>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / no);
        if(cold_cache)
        {
          (void) utils::drop_filesystem_cache();
        }

        if(!reduced)
        {
          begin = std::chrono::high_resolution_clock::now();
          for(size_t n = 0; n < no; n++)
          {
            filename = std::to_string(n);
            file_handle fileh(file_handle::file(dirh, filename, file_handle::mode::read, file_handle::creation::open_existing, srch.kernel_caching(), flags).value());
          }
          // For atime updating
          if(srch.kernel_caching() == file_handle::caching::reads || srch.kernel_caching() == file_handle::caching::none)
          {
            (void) utils::flush_modified_data();
          }
          end = std::chrono::high_resolution_clock::now();
          s.open_read = static_cast<unsigned long long>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / no);
          if(cold_cache)
          {
            (void) utils::drop_filesystem_cache();
          }

          begin = std::chrono::high_resolution_clock::now();
          for(size_t n = 0; n < no; n++)
          {
            filename = std::to_string(n);
            file_handle fileh(file_handle::file(dirh, filename, file_handle::mode::write, file_handle::creation::open_existing, srch.kernel_caching(), flags).value());
          }
          // For atime updating
          if(srch.kernel_caching() == file_handle::caching::reads || srch.kernel_caching() == file_handle::caching::none)
          {
            (void) utils::flush_modified_data();
          }
          end = std::chrono::high_resolution_clock::now();
          s.open_write = static_cast<unsigned long long>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / no);
          if(cold_cache)
          {
            (void) utils::drop_filesystem_cache();
          }
        }

        begin = std::chrono::high_resolution_clock::now();
        for(size_t n = 0; n < no; n++)
        {
          filename = std::to_string(n);
          file_handle fileh(file_handle::file(dirh, filename, file_handle::mode::write, file_handle::creation::open_existing, srch.kernel_caching(), flags).value());
          fileh.unlink().value();
        }
        if(srch.kernel_caching() == file_handle::caching::reads || srch.kernel_caching() == file_handle::caching::none)
        {
          (void) utils::flush_modified_data();
        }
        end = std::chrono::high_resolution_clock::now();
        s.destroy = static_cast<unsigned long long>(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()) / no);

        dirh.unlink().value();
        return s;
      }
      catch(...)
      {
        return std::current_exception();
      }
    }
    outcome<void> traversal_warm_racefree_0b(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.create_file_warm_racefree_0b.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      size_t items = srch.are_writes_durable() ? 256 : 16384;
      OUTCOME_TRY(s, _traversal_N(srch, items, 0, false, true, false));
      sp.create_file_warm_racefree_0b.value = s.create;
      sp.enumerate_file_warm_racefree_0b.value = s.enumerate;
      sp.open_file_read_warm_racefree_0b.value = s.open_read;
      sp.open_file_write_warm_racefree_0b.value = s.open_write;
      sp.delete_file_warm_racefree_0b.value = s.destroy;
      return success();
    }
    outcome<void> traversal_warm_nonracefree_0b(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.create_file_warm_nonracefree_0b.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      size_t items = srch.are_writes_durable() ? 256 : 16384;
      OUTCOME_TRY(s, _traversal_N(srch, items, 0, false, false, false));
      sp.create_file_warm_nonracefree_0b.value = s.create;
      sp.enumerate_file_warm_nonracefree_0b.value = s.enumerate;
      sp.open_file_read_warm_nonracefree_0b.value = s.open_read;
      sp.open_file_write_warm_nonracefree_0b.value = s.open_write;
      sp.delete_file_warm_nonracefree_0b.value = s.destroy;
      return success();
    }
    outcome<void> traversal_cold_racefree_0b(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.create_file_cold_racefree_0b.value != static_cast<unsigned long long>(-1))
      {
        return success();
      }
      size_t items = srch.are_writes_durable() ? 256 : 16384;
      OUTCOME_TRY(s, _traversal_N(srch, items, 0, true, true, false));
      sp.create_file_cold_racefree_0b.value = s.create;
      sp.enumerate_file_cold_racefree_0b.value = s.enumerate;
      sp.open_file_read_cold_racefree_0b.value = s.open_read;
      sp.open_file_write_cold_racefree_0b.value = s.open_write;
      sp.delete_file_cold_racefree_0b.value = s.destroy;
      return success();
    }
    /*
    outcome<void> traversal_warm_nonracefree_1M(storage_profile &sp, file_handle &srch) noexcept
    {
      if(sp.create_1M_files.value != (unsigned) -1)
        return success();
      if(srch.kernel_caching() != file_handle::caching::all)
      {
        return errc::invalid_argument;
      }
      OUTCOME_TRY(s, _traversal_N(srch, 1000000, 0, false, false, true));
      sp.create_1M_files.value = s.create;
      sp.enumerate_1M_files.value = s.enumerate;
      sp.delete_1M_files.value = s.destroy;
      return success();
    }
    */
  }  // namespace response_time
}  // namespace storage_profile
LLFIO_V2_NAMESPACE_END

#ifdef WIN32
#include "windows/storage_profile.ipp"
#else
#include "posix/storage_profile.ipp"
#endif
