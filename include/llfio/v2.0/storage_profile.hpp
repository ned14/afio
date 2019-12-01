/* A profile of an OS and filing system
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (7 commits)
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

#ifndef LLFIO_STORAGE_PROFILE_H
#define LLFIO_STORAGE_PROFILE_H

#include "io_handle.hpp"

#if LLFIO_EXPERIMENTAL_STATUS_CODE
#include "outcome/experimental/status_outcome.hpp"
LLFIO_V2_NAMESPACE_EXPORT_BEGIN
template <class T> using outcome = OUTCOME_V2_NAMESPACE::experimental::status_outcome<T, file_io_error>;
LLFIO_V2_NAMESPACE_END
#else
#include "outcome/outcome.hpp"
LLFIO_V2_NAMESPACE_EXPORT_BEGIN
template <class T> using outcome = OUTCOME_V2_NAMESPACE::outcome<T, error_info>;
LLFIO_V2_NAMESPACE_END
#endif

#include <regex>
#include <utility>
//! \file storage_profile.hpp Provides storage_profile

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

namespace storage_profile
{
  //! Types potentially storable in a storage profile
  enum class storage_types
  {
    unknown,
    extent_type,
    unsigned_int,
    unsigned_long_long,
    float_,
    string
  };
  struct storage_profile;

  //! Returns the enum matching type T
  template <class T> constexpr storage_types map_to_storage_type()
  {
    static_assert(0 == sizeof(T), "Unsupported storage_type");
    return storage_types::unknown;
  }
  //! Specialise for a different default value for T
  template <class T> constexpr T default_value() { return T{}; }

  template <> constexpr storage_types map_to_storage_type<io_handle::extent_type>() { return storage_types::extent_type; }
  template <> constexpr io_handle::extent_type default_value<io_handle::extent_type>() { return static_cast<io_handle::extent_type>(-1); }
  template <> constexpr storage_types map_to_storage_type<unsigned int>() { return storage_types::unsigned_int; }
  template <> constexpr unsigned int default_value<unsigned int>() { return static_cast<unsigned int>(-1); }
  //  template<> constexpr storage_types map_to_storage_type<unsigned long long>() { return storage_types::unsigned_long_long; }
  template <> constexpr storage_types map_to_storage_type<float>() { return storage_types::float_; }
  template <> constexpr storage_types map_to_storage_type<std::string>() { return storage_types::string; }

  //! Common base class for items
  struct item_base
  {
    static constexpr size_t item_size = 128;
    //! The type of handle used for testing
    using handle_type = file_handle;

    const char *name;         //!< The name of the item in colon delimited category format
    const char *description;  //!< Some description of the item
    storage_types type;       //!< The type of the value
  protected:
    constexpr item_base(const char *_name, const char *_desc, storage_types _type)
        : name(_name)
        , description(_desc)
        , type(_type)
    {
    }
  };
  //! A tag-value item in the storage profile where T is the type of value stored.
  template <class T> struct item : public item_base
  {
    static constexpr size_t item_size = item_base::item_size;
    using handle_type = item_base::handle_type;
    using callable = outcome<void> (*)(storage_profile &sp, handle_type &h);

    callable impl;
    T value;  //!< The storage of the item
    char _padding[item_size - sizeof(item_base) - sizeof(callable) - sizeof(T)];
    constexpr item(const char *_name, callable c, const char *_desc = nullptr, T _value = default_value<T>())
        : item_base(_name, _desc, map_to_storage_type<T>())
        , impl(c)
        , value(std::move(_value))
        , _padding{0}
    {
      static_assert(sizeof(*this) == item_size, "");
    }
    //! Clear this item, returning value to default
    void clear() { value = default_value<T>(); }
    //! Set this item if its value is default
    outcome<void> operator()(storage_profile &sp, handle_type &h) const
    {
      if(value != default_value<T>())
      {
        return success();
      }
      return impl(sp, h);
    }
  };
  //! A type erased tag-value item
  struct item_erased : public item_base
  {
    static constexpr size_t item_size = item_base::item_size;
    using handle_type = item_base::handle_type;
    char _padding[item_size - sizeof(item_base)];

    item_erased() = delete;
    ~item_erased() = delete;
    item_erased(const item_erased &) = delete;
    item_erased(item_erased &&) = delete;
    item_erased &operator=(const item_erased &) = delete;
    item_erased &operator=(item_erased &&) = delete;
    //! Call the callable with the unerased type
    template <class U> auto invoke(U &&f) const
    {
      switch(type)
      {
      case storage_types::extent_type:
        return f(*reinterpret_cast<const item<io_handle::extent_type> *>(static_cast<const item_base *>(this)));
      case storage_types::unsigned_int:
        return f(*reinterpret_cast<const item<unsigned int> *>(static_cast<const item_base *>(this)));
      case storage_types::unsigned_long_long:
        return f(*reinterpret_cast<const item<unsigned long long> *>(static_cast<const item_base *>(this)));
      case storage_types::float_:
        return f(*reinterpret_cast<const item<float> *>(static_cast<const item_base *>(this)));
      case storage_types::string:
        return f(*reinterpret_cast<const item<std::string> *>(static_cast<const item_base *>(this)));
      case storage_types::unknown:
        break;
      }
      throw std::invalid_argument("No type set in item");  // NOLINT
    }
    //! Set this item if its value is default
    outcome<void> operator()(storage_profile &sp, handle_type &h) const
    {
      return invoke([&sp, &h](auto &item) { return item(sp, h); });
    }
  };

  namespace system
  {
    // OS name, version
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> os(storage_profile &sp, file_handle &h) noexcept;
    // CPU name, architecture, physical cores
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> cpu(storage_profile &sp, file_handle &h) noexcept;
    // System memory quantity, in use, max and min bandwidth
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> mem(storage_profile &sp, file_handle &h) noexcept;
#ifdef _WIN32
    namespace windows
    {
#else
    namespace posix
    {
#endif
      LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> _mem(storage_profile &sp, file_handle &h) noexcept;
    }
    // High resolution clock granularity
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> clock_granularity(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> yield_overhead(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> sleep_wake_overhead(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> drop_filesystem_cache_support(storage_profile &sp, file_handle & /*unused*/) noexcept;
  }  // namespace system
  namespace storage
  {
    // Device name, size, min i/o size
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> device(storage_profile &sp, file_handle &h) noexcept;
    // FS name, config, size, in use
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> fs(storage_profile &sp, file_handle &h) noexcept;
#ifdef _WIN32
    namespace windows
    {
#else
    namespace posix
    {
#endif
      LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> _device(storage_profile &sp, file_handle &h, const std::string &_mntfromname, const std::string &fstypename) noexcept;
    }
  }  // namespace storage
  namespace concurrency
  {
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> atomic_rewrite_quantum(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> atomic_rewrite_offset_boundary(storage_profile &sp, file_handle &srch) noexcept;
  }
  namespace latency
  {
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> read_nothing(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> write_nothing(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> read_qd1(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> write_qd1(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> read_qd16(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> write_qd16(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> readwrite_qd4(storage_profile &sp, file_handle &srch) noexcept;
  }
  namespace response_time
  {
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_racefree_0b(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_racefree_1b(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_racefree_4k(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_nonracefree_0b(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_nonracefree_1b(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_nonracefree_4k(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_warm_nonracefree_1M(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_cold_racefree_0b(storage_profile &sp, file_handle &srch) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_cold_racefree_1b(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_cold_racefree_4k(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_cold_nonracefree_0b(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_cold_nonracefree_1b(storage_profile &sp, file_handle &h) noexcept;
    LLFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> traversal_cold_nonracefree_4k(storage_profile &sp, file_handle &h) noexcept;
  }  // namespace response_time

  //! A (possibly incomplet) profile of storage
  struct LLFIO_DECL storage_profile
  {
    //! The size type
    using size_type = size_t;

  private:
    size_type _size{0};

  public:
    storage_profile() = default;

    //! Value type
    using value_type = item_erased &;
    //! Reference type
    using reference = item_erased &;
    //! Const reference type
    using const_reference = const item_erased &;
    //! Iterator type
    using iterator = item_erased *;
    //! Const iterator type
    using const_iterator = const item_erased *;
    //! The type of handle used for testing
    using handle_type = item_base::handle_type;

    //! True if this storage profile is empty
    bool empty() const noexcept { return _size == 0; }
    //! Items in this storage profile
    size_type size() const noexcept { return _size; }
    //! Potential items in this storage profile
    size_type max_size() const noexcept { return (sizeof(*this) - sizeof(_size)) / item_base::item_size; }
    //! Returns an iterator to the first item
    iterator begin() noexcept { return reinterpret_cast<item_erased *>(static_cast<item_base *>(&os_name)); }
    //! Returns an iterator to the last item
    iterator end() noexcept { return begin() + max_size(); }
    //! Returns an iterator to the first item
    const_iterator begin() const noexcept { return reinterpret_cast<const item_erased *>(static_cast<const item_base *>(&os_name)); }
    //! Returns an iterator to the last item
    const_iterator end() const noexcept { return begin() + max_size(); }

    //! Read the matching items in the storage profile from in as YAML
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC void read(std::istream &in, std::regex which = std::regex(".*"));
    //! Write the matching items from storage profile as YAML to out with the given indentation
    LLFIO_HEADERS_ONLY_MEMFUNC_SPEC void write(std::ostream &out, const std::regex &which = std::regex(".*"), size_t _indent = 0, bool invert_match = false) const;

    // System characteristics
    item<std::string> os_name = {"system:os:name", &system::os};                     // e.g. Microsoft Windows NT
    item<std::string> os_ver = {"system:os:ver", &system::os};                       // e.g. 10.0.10240
    item<std::string> cpu_name = {"system:cpu:name", &system::cpu};                  // e.g. Intel Haswell
    item<std::string> cpu_architecture = {"system:cpu:architecture", &system::cpu};  // e.g. x64
    item<unsigned> cpu_physical_cores = {"system:cpu:physical_cores", &system::cpu};
    item<unsigned long long> mem_max_bandwidth = {"system:mem:max_bandwidth", system::mem, "Main memory bandwidth when accessed sequentially (1 CPU core)"};
    item<unsigned long long> mem_min_bandwidth = {"system:mem:min_bandwidth", system::mem, "Main memory bandwidth when 4Kb pages are accessed randomly (1 CPU core)"};
    item<unsigned long long> mem_quantity = {"system:mem:quantity", &system::mem};
    item<float> mem_in_use = {"system:mem:in_use", &system::mem};  // not including caches etc.
    item<unsigned> clock_granularity = {"system:timer:ns_per_tick", &system::clock_granularity};
    item<unsigned> clock_overhead = {"system:timer:ns_overhead", &system::clock_granularity};
    item<unsigned> yield_overhead = {"system:scheduler:ns_yield", &system::yield_overhead, "Nanoseconds to context switch a thread"};
    item<unsigned> sleep_wake_overhead = {"system:scheduler:ns_sleep_wake", &system::sleep_wake_overhead, "Nanoseconds to sleep and wake a thread"};
    item<unsigned> drop_filesystem_cache_support = {"system:drop_filesystem_cache_support", &system::drop_filesystem_cache_support};

    // Controller characteristics
    item<std::string> controller_type = {"storage:controller:kind", &storage::device};  // e.g. SATA
    item<unsigned> controller_max_transfer = {"storage:controller:max_transfer", storage::device, "The maximum number of bytes the disk controller can transfer at once"};
    item<unsigned> controller_max_buffers = {"storage:controller:max_buffers", storage::device, "The maximum number of scatter-gather buffers the disk controller can handle"};

    // Storage characteristics
    item<std::string> device_name = {"storage:device:name", &storage::device};             // e.g. WDC WD30EFRX-68EUZN0
    item<unsigned> device_min_io_size = {"storage:device:min_io_size", &storage::device};  // e.g. 4096
    item<io_handle::extent_type> device_size = {"storage:device:size", &storage::device};

    // Filing system characteristics
    item<std::string> fs_name = {"storage:fs:name", &storage::fs};
    item<std::string> fs_config = {"storage:fs:config", &storage::fs};  // POSIX mount options, ZFS pool properties etc
                                                                        //        item<std::string> fs_ffeatures = { "storage:fs:features" };  // Standardised features???
    item<io_handle::extent_type> fs_size = {"storage:fs:size", &storage::fs};
    item<float> fs_in_use = {"storage:fs:in_use", &storage::fs};

    // Test results on this filing system, storage and system
    item<io_handle::extent_type> atomic_rewrite_quantum = {"concurrency:atomic_rewrite_quantum", concurrency::atomic_rewrite_quantum, "The i/o modify quantum guaranteed to be atomically visible to readers irrespective of rewrite quantity"};
    item<io_handle::extent_type> max_aligned_atomic_rewrite = {"concurrency:max_aligned_atomic_rewrite", concurrency::atomic_rewrite_quantum,
                                                                "The maximum single aligned i/o modify quantity atomically visible to readers (can be [potentially unreliably] much larger than atomic_rewrite_quantum). "
                                                                "A very common value on modern hardware with direct i/o thanks to PCIe DMA is 4096, don't trust values higher than this because of potentially discontiguous memory page mapping."};
    item<io_handle::extent_type> atomic_rewrite_offset_boundary = {"concurrency:atomic_rewrite_offset_boundary", concurrency::atomic_rewrite_offset_boundary, "The multiple of offset in a file where update atomicity breaks, so if you wrote 4096 bytes at a 512 offset and "
                                                                                                                                                               "this value was 4096, your write would tear at 3584 because all writes would tear on a 4096 offset multiple. "
                                                                                                                                                               "Linux has a famously broken kernel i/o design which causes this value to be a page multiple, except on "
                                                                                                                                                               "filing systems which take special measures to work around it. Windows NT appears to lose all atomicity as soon as "
                                                                                                                                                               "an i/o straddles a 4096 file offset multiple and DMA suddenly goes into many 64 byte cache lines :(, so if "
                                                                                                                                                               "this value is less than max_aligned_atomic_rewrite and some multiple of the CPU cache line size then this is "
                                                                                                                                                               "what has happened."};
    item<unsigned> read_nothing = {"latency:read:nothing", latency::read_nothing, "The nanoseconds to read zero bytes"};

    item<unsigned long long> read_qd1_min = {"latency:read:qd1:min", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (min)"};
    item<unsigned long long> read_qd1_mean = {"latency:read:qd1:mean", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (arithmetic mean)"};
    item<unsigned long long> read_qd1_max = {"latency:read:qd1:max", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (max)"};
    item<unsigned long long> read_qd1_50 = {"latency:read:qd1:50%", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (50% of the time)"};
    item<unsigned long long> read_qd1_95 = {"latency:read:qd1:95%", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (95% of the time)"};
    item<unsigned long long> read_qd1_99 = {"latency:read:qd1:99%", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (99% of the time)"};
    item<unsigned long long> read_qd1_99999 = {"latency:read:qd1:99.999%", latency::read_qd1, "The nanoseconds to read 4Kb at a queue depth of 1 (99.999% of the time)"};

    item<unsigned long long> read_qd16_min = {"latency:read:qd16:min", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (min)"};
    item<unsigned long long> read_qd16_mean = {"latency:read:qd16:mean", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (arithmetic mean)"};
    item<unsigned long long> read_qd16_max = {"latency:read:qd16:max", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (max)"};
    item<unsigned long long> read_qd16_50 = {"latency:read:qd16:50%", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (50% of the time)"};
    item<unsigned long long> read_qd16_95 = {"latency:read:qd16:95%", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (95% of the time)"};
    item<unsigned long long> read_qd16_99 = {"latency:read:qd16:99%", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (99% of the time)"};
    item<unsigned long long> read_qd16_99999 = {"latency:read:qd16:99.999%", latency::read_qd16, "The nanoseconds to read 4Kb at a queue depth of 16 (99.999% of the time)"};

    item<unsigned> write_nothing = {"latency:write:nothing", latency::write_nothing, "The nanoseconds to write zero bytes"};

    item<unsigned long long> write_qd1_min = {"latency:write:qd1:min", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (min)"};
    item<unsigned long long> write_qd1_mean = {"latency:write:qd1:mean", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (arithmetic mean)"};
    item<unsigned long long> write_qd1_max = {"latency:write:qd1:max", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (max)"};
    item<unsigned long long> write_qd1_50 = {"latency:write:qd1:50%", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (50% of the time)"};
    item<unsigned long long> write_qd1_95 = {"latency:write:qd1:95%", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (95% of the time)"};
    item<unsigned long long> write_qd1_99 = {"latency:write:qd1:99%", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (99% of the time)"};
    item<unsigned long long> write_qd1_99999 = {"latency:write:qd1:99.999%", latency::write_qd1, "The nanoseconds to write 4Kb at a queue depth of 1 (99.999% of the time)"};

    item<unsigned long long> write_qd16_min = {"latency:write:qd16:min", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (min)"};
    item<unsigned long long> write_qd16_mean = {"latency:write:qd16:mean", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (arithmetic mean)"};
    item<unsigned long long> write_qd16_max = {"latency:write:qd16:max", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (max)"};
    item<unsigned long long> write_qd16_50 = {"latency:write:qd16:50%", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (50% of the time)"};
    item<unsigned long long> write_qd16_95 = {"latency:write:qd16:95%", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (95% of the time)"};
    item<unsigned long long> write_qd16_99 = {"latency:write:qd16:99%", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (99% of the time)"};
    item<unsigned long long> write_qd16_99999 = {"latency:write:qd16:99.999%", latency::write_qd16, "The nanoseconds to write 4Kb at a queue depth of 16 (99.999% of the time)"};

    item<unsigned long long> readwrite_qd4_min = {"latency:readwrite:qd4:min", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (min)"};
    item<unsigned long long> readwrite_qd4_mean = {"latency:readwrite:qd4:mean", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (arithmetic mean)"};
    item<unsigned long long> readwrite_qd4_max = {"latency:readwrite:qd4:max", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (max)"};
    item<unsigned long long> readwrite_qd4_50 = {"latency:readwrite:qd4:50%", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (50% of the time)"};
    item<unsigned long long> readwrite_qd4_95 = {"latency:readwrite:qd4:95%", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (95% of the time)"};
    item<unsigned long long> readwrite_qd4_99 = {"latency:readwrite:qd4:99%", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (99% of the time)"};
    item<unsigned long long> readwrite_qd4_99999 = {"latency:readwrite:qd4:99.999%", latency::readwrite_qd4, "The nanoseconds to 75% read 25% write 4Kb at a total queue depth of 4 (99.999% of the time)"};

    item<unsigned long long> create_file_warm_racefree_0b = {"response_time:race_free:warm_cache:create_file:0b", response_time::traversal_warm_racefree_0b, "The average nanoseconds to create a 0 byte file (warm cache, race free)"};
    item<unsigned long long> enumerate_file_warm_racefree_0b = {"response_time:race_free:warm_cache:enumerate_file:0b", response_time::traversal_warm_racefree_0b, "The average nanoseconds to enumerate a 0 byte file (warm cache, race free)"};
    item<unsigned long long> open_file_read_warm_racefree_0b = {"response_time:race_free:warm_cache:open_file_read:0b", response_time::traversal_warm_racefree_0b, "The average nanoseconds to open a 0 byte file for reading (warm cache, race free)"};
    item<unsigned long long> open_file_write_warm_racefree_0b = {"response_time:race_free:warm_cache:open_file_write:0b", response_time::traversal_warm_racefree_0b, "The average nanoseconds to open a 0 byte file for writing (warm cache, race free)"};
    item<unsigned long long> delete_file_warm_racefree_0b = {"response_time:race_free:warm_cache:delete_file:0b", response_time::traversal_warm_racefree_0b, "The average nanoseconds to delete a 0 byte file (warm cache, race free)"};

    item<unsigned long long> create_file_warm_nonracefree_0b = {"response_time:non_race_free:warm_cache:create_file:0b", response_time::traversal_warm_nonracefree_0b, "The average nanoseconds to create a 0 byte file (warm cache, non race free)"};
    item<unsigned long long> enumerate_file_warm_nonracefree_0b = {"response_time:non_race_free:warm_cache:enumerate_file:0b", response_time::traversal_warm_nonracefree_0b, "The average nanoseconds to enumerate a 0 byte file (warm cache, non race free)"};
    item<unsigned long long> open_file_read_warm_nonracefree_0b = {"response_time:non_race_free:warm_cache:open_file_read:0b", response_time::traversal_warm_nonracefree_0b, "The average nanoseconds to open a 0 byte file for reading (warm cache, non race free)"};
    item<unsigned long long> open_file_write_warm_nonracefree_0b = {"response_time:non_race_free:warm_cache:open_file_write:0b", response_time::traversal_warm_nonracefree_0b, "The average nanoseconds to open a 0 byte file for writing (warm cache, non race free)"};
    item<unsigned long long> delete_file_warm_nonracefree_0b = {"response_time:non_race_free:warm_cache:delete_file:0b", response_time::traversal_warm_nonracefree_0b, "The average nanoseconds to delete a 0 byte file (warm cache, non race free)"};

    item<unsigned long long> create_file_cold_racefree_0b = {"response_time:race_free:cold_cache:create_file:0b", response_time::traversal_cold_racefree_0b, "The average nanoseconds to create a 0 byte file (cold cache, race free)"};
    item<unsigned long long> enumerate_file_cold_racefree_0b = {"response_time:race_free:cold_cache:enumerate_file:0b", response_time::traversal_cold_racefree_0b, "The average nanoseconds to enumerate a 0 byte file (cold cache, race free)"};
    item<unsigned long long> open_file_read_cold_racefree_0b = {"response_time:race_free:cold_cache:open_file_read:0b", response_time::traversal_cold_racefree_0b, "The average nanoseconds to open a 0 byte file for reading (cold cache, race free)"};
    item<unsigned long long> open_file_write_cold_racefree_0b = {"response_time:race_free:cold_cache:open_file_write:0b", response_time::traversal_cold_racefree_0b, "The average nanoseconds to open a 0 byte file for writing (cold cache, race free)"};
    item<unsigned long long> delete_file_cold_racefree_0b = {"response_time:race_free:cold_cache:delete_file:0b", response_time::traversal_cold_racefree_0b, "The average nanoseconds to delete a 0 byte file (cold cache, race free)"};

    /*
    item<unsigned> create_1M_files = {"response_time:create_1M_files_single_dir", response_time::traversal_warm_nonracefree_1M, "The milliseconds to create 1M empty files in a single directory"};
    item<unsigned> enumerate_1M_files = {"response_time:enumerate_1M_files_single_dir", response_time::traversal_warm_nonracefree_1M, "The milliseconds to enumerate 1M empty files in a single directory"};
    item<unsigned> delete_1M_files = {"response_time:delete_1M_files_single_dir", response_time::traversal_warm_nonracefree_1M, "The milliseconds to delete 1M files in a single directory"};
    */
  };
}  // namespace storage_profile

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#include "detail/impl/storage_profile.ipp"
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
