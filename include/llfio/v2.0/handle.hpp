/* A handle to something
(C) 2015-2019 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
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

#ifndef LLFIO_HANDLE_H
#define LLFIO_HANDLE_H

#include "deadline.h"
#include "native_handle_type.hpp"
#include "status_code.hpp"

#include <algorithm>  // for std::count
#include <cassert>

//! \file handle.hpp Provides handle

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

class fs_handle;
class io_multiplexer;

namespace detail
{
  struct io_operation_connection;
}

#pragma pack(push, 4)

/*! \class handle
\brief A native_handle_type which is managed by the lifetime of this object instance.
*/
class LLFIO_DECL handle
{
  friend class fs_handle;
  friend inline std::ostream &operator<<(std::ostream &s, const handle &v);

public:
  //! The path type used by this handle
  using path_type = filesystem::path;
  //! The file extent type used by this handle
  using extent_type = unsigned long long;
  //! The memory extent type used by this handle
  using size_type = size_t;

  //! The behaviour of the handle: does it read, read and write, or atomic append?
  enum class mode : unsigned char  // bit 0 set means writable
  {
    unchanged = 0,
    none = 2,        //!< No ability to read or write anything, but can synchronise (SYNCHRONIZE or 0)
    attr_read = 4,   //!< Ability to read attributes (FILE_READ_ATTRIBUTES|SYNCHRONIZE or O_RDONLY)
    attr_write = 5,  //!< Ability to read and write attributes (FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES|SYNCHRONIZE or O_RDONLY)
    read = 6,        //!< Ability to read (READ_CONTROL|FILE_READ_DATA|FILE_READ_ATTRIBUTES|FILE_READ_EA|SYNCHRONISE or O_RDONLY)
    write = 7,       //!< Ability to read and write (READ_CONTROL|FILE_READ_DATA|FILE_READ_ATTRIBUTES|FILE_READ_EA|FILE_WRITE_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|FILE_APPEND_DATA|SYNCHRONISE or O_RDWR)
    append = 9       //!< All mainstream OSs and CIFS guarantee this is atomic with respect to all other appenders (FILE_APPEND_DATA|SYNCHRONISE or O_APPEND)
                     // NOTE: IF UPDATING THIS UPDATE THE std::ostream PRINTER BELOW!!!
  };
  //! On opening, do we also create a new file or truncate an existing one?
  enum class creation : unsigned char
  {
    open_existing = 0,  //!< Filesystem entry must already exist
    only_if_not_exist,  //!< Filesystem entry must NOT exist, and is atomically created by the success of this operation
    if_needed,          //!< If filesystem entry exists that is used, else one is created
    truncate_existing,  //!< Filesystem entry must already exist. It is atomically truncated on open, leaving creation date and unique identifier unmodified.
    always_new          //!< If filesystem entry exists, it is atomically replaced with a new inode, else a new entry is created.
                        // NOTE: IF UPDATING THIS UPDATE THE std::ostream PRINTER BELOW!!!
  };
  //! What i/o on the handle may complete immediately due to kernel caching
  enum class caching : unsigned char  // bit 0 set means safety barriers enabled
  {
    unchanged = 0,
    none = 1,                //!< No caching whatsoever, all reads and writes come from storage (i.e. <tt>O_DIRECT|O_SYNC</tt>). Align all i/o to 4Kb boundaries for this to work. <tt>disable_safety_barriers</tt> can be used here.
    only_metadata = 2,       //!< Cache reads and writes of metadata but avoid caching data (<tt>O_DIRECT</tt>), thus i/o here does not affect other cached data for other handles. Align all i/o to 4Kb boundaries for this to work.
    reads = 3,               //!< Cache reads only. Writes of data and metadata do not complete until reaching storage (<tt>O_SYNC</tt>). <tt>disable_safety_barriers</tt> can be used here.
    reads_and_metadata = 5,  //!< Cache reads and writes of metadata, but writes of data do not complete until reaching storage (<tt>O_DSYNC</tt>). <tt>disable_safety_barriers</tt> can be used here.
    all = 6,                 //!< Cache reads and writes of data and metadata so they complete immediately, sending writes to storage at some point when the kernel decides (this is the default file system caching on a system).
    safety_barriers = 7,     //!< Cache reads and writes of data and metadata so they complete immediately, but issue safety barriers at certain points. See documentation for <tt>disable_safety_barriers</tt>.
    temporary = 8            //!< Cache reads and writes of data and metadata so they complete immediately, only sending any updates to storage on last handle close in the system or if memory becomes tight as this file is expected to be temporary (Windows and FreeBSD only).
                             // NOTE: IF UPDATING THIS UPDATE THE std::ostream PRINTER BELOW!!!
  };
  //! Bitwise flags which can be specified
  QUICKCPPLIB_BITFIELD_BEGIN(flag){
  none = 0,  //!< No flags
  /*! Unlinks the file on handle close. On POSIX, this simply unlinks whatever is pointed
  to by `path()` upon the call of `close()` if and only if the inode matches. On Windows,
  if you are on Windows 10 1709 or later, exactly the same thing occurs. If on previous
  editions of Windows, the file entry does not disappears but becomes unavailable for
  anyone else to open with an `errc::resource_unavailable_try_again` error return. Because this is confusing, unless the
  `win_disable_unlink_emulation` flag is also specified, this POSIX behaviour is
  somewhat emulated by LLFIO on older Windows by renaming the file to a random name on `close()`
  causing it to appear to have been unlinked immediately.
  */
  unlink_on_first_close = 1U << 0U,

  /*! Some kernel caching modes have unhelpfully inconsistent behaviours
  in getting your data onto storage, so by default unless this flag is
  specified LLFIO adds extra fsyncs to the following operations for the
  caching modes specified below:
  * truncation of file length either explicitly or during file open.
  * closing of the handle either explicitly or in the destructor.

  Additionally on Linux only to prevent loss of file metadata:
  * On the parent directory whenever a file might have been created.
  * On the parent directory on file close.

  This only occurs for these kernel caching modes:
  * caching::none
  * caching::reads
  * caching::reads_and_metadata
  * caching::safety_barriers
  */
  disable_safety_barriers = 1U << 2U,
  /*! `file_handle::unlink()` could accidentally delete the wrong file if someone has
  renamed the open file handle since the time it was opened. To prevent this occuring,
  where the OS doesn't provide race free unlink-by-open-handle we compare the inode of
  the path we are about to unlink with that of the open handle before unlinking.
  \warning This does not prevent races where in between the time of checking the inode
  and executing the unlink a third party changes the item about to be unlinked. Only
  operating systems with a true race-free unlink syscall are race free.
  */
  disable_safety_unlinks = 1U << 3U,
  /*! Ask the OS to disable prefetching of data. This can improve random
  i/o performance.
  */
  disable_prefetching = 1U << 4U,
  /*! Ask the OS to maximise prefetching of data, possibly prefetching the entire file
  into kernel cache. This can improve sequential i/o performance.
  */
  maximum_prefetching = 1U << 5U,

  win_disable_unlink_emulation = 1U << 24U,  //!< See the documentation for `unlink_on_first_close`
  /*! Microsoft Windows NTFS, having been created in the late 1980s, did not originally
  implement extents-based storage and thus could only represent sparse files via
  efficient compression of intermediate zeros. With NTFS v3.0 (Microsoft Windows 2000),
  a proper extents-based on-storage representation was added, thus allowing only 64Kb
  extent chunks written to be stored irrespective of whatever the maximum file extent
  was set to.

  For various historical reasons, extents-based storage is disabled by default in newly
  created files on NTFS, unlike in almost every other major filing system. You have to
  explicitly "opt in" to extents-based storage.

  As extents-based storage is nearly cost free on NTFS, LLFIO by default opts in to
  extents-based storage for any empty file it creates. If you don't want this, you
  can specify this flag to prevent that happening.
  */
  win_disable_sparse_file_creation = 1U << 25U,
  /*! Filesystems tend to be embarrassingly parallel for operations performed to different
  inodes. Where LLFIO performs i/o to multiple inodes at a time, it will use OpenMP or
  the Parallelism or Concurrency standard library extensions to usually complete the
  operation in constant rather than linear time. If you don't want this default, you can
  disable default using this flag.
  */
  disable_parallelism = 1U << 26U,
  /*! Microsoft Windows NTFS has the option, when creating a directory, to set whether
  leafname lookup will be case sensitive. This is the only way of getting exact POSIX
  semantics on Windows without resorting to editing the system registry, however it also
  affects all code doing lookups within that directory, so we must default it to off.
  */
  win_create_case_sensitive_directory = 1U << 27U,

  /*! Create the handle in a way where i/o upon it can be multiplexed with other i/o
  on the same initiating thread of execution i.e. you can perform more than one read
  concurrently, without using threads. The blocking operations `.read()` and `.write()`
  may have to use a less efficient, but cancellable, blocking implementation for handles created
  in this way. On Microsoft Windows, this creates handles with `OVERLAPPED` semantics.
  On POSIX, this creates handles with nonblocking semantics for non-file handles such
  as pipes and sockets, however for file, directory and symlink handles it does not set
  nonblocking, as it is non-portable.
  */
  multiplexable = 1U << 28U,

  // NOTE: IF UPDATING THIS UPDATE THE std::ostream PRINTER BELOW!!!

  byte_lock_insanity = 1U << 29U,  //!< Using insane POSIX byte range locks
  anonymous_inode = 1U << 30U      //!< This is an inode created with no representation on the filing system
  } QUICKCPPLIB_BITFIELD_END(flag);

protected:
  // vptr takes 4 or 8 bytes
  io_multiplexer *_ctx{nullptr};  // 8 or 16 bytes
  native_handle_type _v;          // 16 or 28 bytes
  caching _caching{caching::none};
  union {
    uint8_t _is_initializer{0};
    struct
    {
      uint8_t _is_connected : 1;  // used by pipe_handle on Windows to store connectedness
#ifdef _WIN32
      uint8_t _multiplexer_is_apc : 1;
      uint8_t _multiplexer_is_iocp : 1;
#endif
    };
  };
  uint16_t _spare2{0};      // 20 or 32 bytes
  flag _flags{flag::none};  // 24 or 36 bytes

public:
  //! Default constructor
  constexpr handle() {}  // NOLINT
  //! Construct a handle from a supplied native handle
  explicit constexpr handle(native_handle_type h, caching caching = caching::none, flag flags = flag::none, io_multiplexer *ctx = nullptr) noexcept
      : _ctx(ctx)
      , _v(std::move(h))
      , _caching(caching)
      , _flags(flags)
  {
  }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~handle();
  //! No copy construction (use clone())
  handle(const handle &) = delete;
  //! No copy assignment
  handle &operator=(const handle &o) = delete;
  //! Move the handle.
  constexpr handle(handle &&o) noexcept
      : _ctx(o._ctx)
      , _v(std::move(o._v))
      , _caching(o._caching)
      , _is_initializer(o._is_initializer)
      , _spare2(o._spare2)
      , _flags(o._flags)
  {
    o._ctx = nullptr;
    o._v = native_handle_type();
    o._caching = caching::none;
    o._flags = flag::none;
  }
  //! Move assignment of handle
  handle &operator=(handle &&o) noexcept
  {
    this->~handle();
    new(this) handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  LLFIO_MAKE_FREE_FUNCTION
  void swap(handle &o) noexcept
  {
    handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  /*! \brief The i/o context this handle will use to multiplex i/o. If this returns null,
  then this handle has not been registered with an i/o context yet.
  */
  io_multiplexer *multiplexer() const noexcept { return this->_ctx; }

  /*! Returns the current path of the open handle as said by the operating system. Note
  that you are NOT guaranteed that any path refreshed bears any resemblance to the original,
  some operating systems will return some different path which still reaches the same inode
  via some other route e.g. hardlinks, dereferenced symbolic links, etc. Windows and
  Linux correctly track changes to the specific path the handle was opened with,
  not getting confused by other hard links. MacOS nearly gets it right, but under some
  circumstances e.g. renaming may switch to a different hard link's path which is almost
  certainly a bug.

  If LLFIO was not able to determine the current path for this open handle e.g. the inode
  has been unlinked, it returns an empty path. Be aware that FreeBSD can return an empty
  (deleted) path for file inodes no longer cached by the kernel path cache, LLFIO cannot
  detect the difference. FreeBSD will also return any path leading to the inode if it is
  hard linked. FreeBSD does implement path retrieval for directory inodes
  correctly however, and see `algorithm::cached_parent_handle_adapter<T>` for a handle
  adapter which makes use of that.

  On Linux if `/proc` is not mounted, this call fails with an error. All APIs in LLFIO
  which require the use of `current_path()` can be told to not use it e.g. `flag::disable_safety_unlinks`.
  It is up to you to detect if `current_path()` is not working, and to change how you
  call LLFIO appropriately.

  \warning This call is expensive, it always asks the kernel for the current path, and no
  checking is done to ensure what the kernel returns is accurate or even sensible.
  Be aware that despite these precautions, paths are unstable and **can change randomly at
  any moment**. Most code written to use absolute file systems paths is **racy**, so don't
  do it, use `path_handle` to fix a base location on the file system and work from that anchor
  instead!

  \mallocs At least one malloc for the `path_type`, likely several more.
  \sa `algorithm::cached_parent_handle_adapter<T>` which overrides this with an
  implementation based on retrieving the current path of a cached handle to the parent
  directory. On platforms with instability or failure to retrieve the correct current path
  for regular files, the cached parent handle adapter works around the problem by
  taking advantage of directory inodes not having the same instability problems on any
  platform.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<path_type> current_path() const noexcept;
  //! Immediately close the native handle type managed by this handle
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept;
  /*! Clone this handle (copy constructor is disabled to avoid accidental copying)

  \errors Any of the values POSIX dup() or DuplicateHandle() can return.
  */
  LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<handle> clone() const noexcept;
  //! Release the native handle type managed by this handle
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC native_handle_type release() noexcept
  {
    native_handle_type ret(std::move(_v));
    return ret;
  }

  //! True if the handle is valid (and usually open)
  bool is_valid() const noexcept { return _v.is_valid(); }

  //! True if the handle is readable
  bool is_readable() const noexcept { return _v.is_readable(); }
  //! True if the handle is writable
  bool is_writable() const noexcept { return _v.is_writable(); }
  //! True if the handle is append only
  bool is_append_only() const noexcept { return _v.is_append_only(); }
  /*! \brief EXTENSION: Changes whether this handle is append only or not.

  \warning On Windows this is implemented as a bit of a hack to make it fast like on POSIX,
  so make sure you open the handle for read/write originally. Note unlike on POSIX the
  append_only disposition will be the only one toggled, seekable and readable will remain
  turned on.

  \errors Whatever POSIX fcntl() returns. On Windows nothing is changed on the handle.
  \mallocs No memory allocation.
  */
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> set_append_only(bool enable) noexcept;

  //! True if multiplexable
  bool is_multiplexable() const noexcept { return !!(_flags & flag::multiplexable); }
  //! True if nonblocking
  bool is_nonblocking() const noexcept { return _v.is_nonblocking(); }
  //! True if seekable
  bool is_seekable() const noexcept { return _v.is_seekable(); }
  //! True if requires aligned i/o
  bool requires_aligned_io() const noexcept { return _v.requires_aligned_io(); }

  //! True if a regular file or device
  bool is_regular() const noexcept { return _v.is_regular(); }
  //! True if a directory
  bool is_directory() const noexcept { return _v.is_directory(); }
  //! True if a symlink
  bool is_symlink() const noexcept { return _v.is_symlink(); }
  //! True if a pipe
  bool is_pipe() const noexcept { return _v.is_pipe(); }
  //! True if a socket
  bool is_socket() const noexcept { return _v.is_socket(); }
  //! True if a multiplexer like BSD kqueues, Linux epoll or Windows IOCP
  bool is_multiplexer() const noexcept { return _v.is_multiplexer(); }
  //! True if a process
  bool is_process() const noexcept { return _v.is_process(); }
  //! True if a memory section
  bool is_section() const noexcept { return _v.is_section(); }

  //! Kernel cache strategy used by this handle
  caching kernel_caching() const noexcept { return _caching; }
  //! True if the handle uses the kernel page cache for reads
  bool are_reads_from_cache() const noexcept { return _caching != caching::none && _caching != caching::only_metadata; }
  //! True if writes are safely on storage on completion
  bool are_writes_durable() const noexcept { return _caching == caching::none || _caching == caching::reads || _caching == caching::reads_and_metadata; }
  //! True if issuing safety fsyncs is on
  bool are_safety_barriers_issued() const noexcept { return !(_flags & flag::disable_safety_barriers) && !((static_cast<unsigned>(_caching) & 1U) == 0U); }

  //! The flags this handle was opened with
  flag flags() const noexcept { return _flags; }
  //! The native handle used by this handle
  native_handle_type native_handle() const noexcept { return _v; }

public:
  enum class _async_op
  {
    unknown,
    read,
    write,
    barrier
  };
#ifdef _WIN32
  // Used async_io_sender to figure out which implementation to use
  bool _is_multiplexer_apc() const noexcept { return _multiplexer_is_apc; }
  bool _is_multiplexer_iocp() const noexcept { return _multiplexer_is_iocp; }

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _apc_begin(_async_op /*unused*/, detail::io_operation_connection * /*unused*/, void * /*unused*/) noexcept { abort(); }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _apc_cancel(detail::io_operation_connection * /*unused*/, void * /*unused*/) noexcept { abort(); }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _iocp_begin(_async_op /*unused*/, detail::io_operation_connection * /*unused*/, void * /*unused*/) noexcept { abort(); }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC void _iocp_cancel(detail::io_operation_connection * /*unused*/, void * /*unused*/) noexcept { abort(); }
#endif
};
static_assert((sizeof(void *) == 4 && sizeof(handle) == 24) || (sizeof(void *) == 8 && sizeof(handle) == 36), "handle is not 24 or 36 bytes in size!");

#pragma pack(pop)

inline std::ostream &operator<<(std::ostream &s, const handle &v)
{
  if(v.is_valid())
  {
    auto _currentpath = v.current_path();
    std::string currentpath = !_currentpath ? std::string(_currentpath.error().message().c_str()) : _currentpath.value().string();
    return s << "llfio::handle(" << v._v._init << ", " << currentpath << ")";
  }
  return s << "llfio::handle(closed)";
}
inline std::ostream &operator<<(std::ostream &s, const handle::mode &v)
{
  static constexpr const char *values[] = {"unchanged", nullptr, "none", nullptr, "attr_read", "attr_write", "read", "write", nullptr, "append"};
  if(static_cast<size_t>(v) >= sizeof(values) / sizeof(values[0]) || (values[static_cast<size_t>(v)] == nullptr))  // NOLINT
  {
    return s << "llfio::handle::mode::<unknown>";
  }
  return s << "llfio::handle::mode::" << values[static_cast<size_t>(v)];  // NOLINT
}
inline std::ostream &operator<<(std::ostream &s, const handle::creation &v)
{
  static constexpr const char *values[] = {"open_existing", "only_if_not_exist", "if_needed", "truncate_existing", "always_new"};
  if(static_cast<size_t>(v) >= sizeof(values) / sizeof(values[0]) || (values[static_cast<size_t>(v)] == nullptr))  // NOLINT
  {
    return s << "llfio::handle::creation::<unknown>";
  }
  return s << "llfio::handle::creation::" << values[static_cast<size_t>(v)];  // NOLINT
}
inline std::ostream &operator<<(std::ostream &s, const handle::caching &v)
{
  static constexpr const char *values[] = {"unchanged", "none", "only_metadata", "reads", "all", "reads_and_metadata", "temporary", "safety_fsyncs"};
  if(static_cast<size_t>(v) >= sizeof(values) / sizeof(values[0]) || (values[static_cast<size_t>(v)] == nullptr))  // NOLINT
  {
    return s << "llfio::handle::caching::<unknown>";
  }
  return s << "llfio::handle::caching::" << values[static_cast<size_t>(v)];  // NOLINT
}
inline std::ostream &operator<<(std::ostream &s, const handle::flag &v)
{
  std::string temp;
  if(!!(v & handle::flag::unlink_on_first_close))
  {
    temp.append("unlink_on_first_close|");
  }
  if(!!(v & handle::flag::disable_safety_barriers))
  {
    temp.append("disable_safety_barriers|");
  }
  if(!!(v & handle::flag::disable_prefetching))
  {
    temp.append("disable_prefetching|");
  }
  if(!!(v & handle::flag::maximum_prefetching))
  {
    temp.append("maximum_prefetching|");
  }
  if(!!(v & handle::flag::win_disable_unlink_emulation))
  {
    temp.append("win_disable_unlink_emulation|");
  }
  if(!!(v & handle::flag::win_disable_sparse_file_creation))
  {
    temp.append("win_disable_sparse_file_creation|");
  }
  if(!!(v & handle::flag::disable_parallelism))
  {
    temp.append("disable_parallelism|");
  }
  if(!!(v & handle::flag::win_create_case_sensitive_directory))
  {
    temp.append("win_create_case_sensitive_directory|");
  }
  if(!!(v & handle::flag::multiplexable))
  {
    temp.append("multiplexable|");
  }
  if(!!(v & handle::flag::byte_lock_insanity))
  {
    temp.append("byte_lock_insanity|");
  }
  if(!!(v & handle::flag::anonymous_inode))
  {
    temp.append("anonymous_inode|");
  }
  if(!temp.empty())
  {
    temp.resize(temp.size() - 1);
    if(std::count(temp.cbegin(), temp.cend(), '|') > 0)
    {
      temp = "(" + temp + ")";
    }
  }
  else
  {
    temp = "none";
  }
  return s << "llfio::handle::flag::" << temp;
}

/*! \brief Metaprogramming shim for constructing any `handle` subclass.

Each `handle` implementation provides one or more static member functions used to construct it.
Each of these has a descriptive, unique name so it can be used as a free function which is
convenient and intuitive for human programmers.

This design pattern is however inconvenient for generic code which needs a single way
of constructing some arbitrary unknown `handle` implementation. This shim function
provides that.
*/
template <class T> struct construct
{
  result<T> operator()() const noexcept { static_assert(!std::is_same<T, T>::value, "construct<T>() was not specialised for the type T supplied"); }
};

#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
namespace detail
{
  template <class Dest, class Src> inline void fill_failure_info(Dest &dest, const Src &src)
  {
    (void) src;
    auto &tls = detail::tls_errored_results();
    if(!tls.reentering_self)
    {
      handle *currenth = tls.current_handle;
      native_handle_type nativeh;
      if(currenth != nullptr)
      {
        nativeh = currenth->native_handle();
        // This may fail, if it does it will construct an error_info thus reentering ourselves. Prevent that.
        tls.reentering_self = true;
        auto currentpath_ = currenth->current_path();
        tls.reentering_self = false;
        if(currentpath_)
        {
          auto currentpath = currentpath_.value().string();
          dest._thread_id = tls.this_thread_id;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)  // the function may be unsafe
#endif
          strncpy(tls.next(dest._tls_path_id1), QUICKCPPLIB_NAMESPACE::ringbuffer_log::last190(currentpath), 190);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
          dest._tls_path_id2 = dest._tls_path_id1 - 17;  // guaranteed invalid
        }
      }
#if LLFIO_LOGGING_LEVEL >= 2
      if(log().log_level() >= log_level::error)
      {
        dest._log_id = log().emplace_back(log_level::error, src.message().c_str(), static_cast<uint32_t>(nativeh._init), tls.this_thread_id);
      }
#endif
    }
  }
}  // namespace detail
#endif

#if LLFIO_EXPERIMENTAL_STATUS_CODE

#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO

//! Helper for constructing an error code from an errc
inline file_io_error generic_error(errc c)
{
  SYSTEM_ERROR2_NAMESPACE::status_code<file_io_error_domain<SYSTEM_ERROR2_NAMESPACE::generic_code::domain_type>> sc(c);
  if(sc.failure())
  {
    detail::fill_failure_info(sc.value(), sc);
  }
  return sc;
}
#ifndef _WIN32
//! Helper for constructing an error code from a POSIX errno
inline file_io_error posix_error(int c)
{
  SYSTEM_ERROR2_NAMESPACE::status_code<file_io_error_domain<SYSTEM_ERROR2_NAMESPACE::posix_code::domain_type>> sc(c);
  if(sc.failure())
  {
    detail::fill_failure_info(sc.value(), sc);
  }
  return sc;
}
#else
//! Helper for constructing an error code from a DWORD
inline file_io_error win32_error(SYSTEM_ERROR2_NAMESPACE::win32::DWORD c)
{
  SYSTEM_ERROR2_NAMESPACE::status_code<file_io_error_domain<SYSTEM_ERROR2_NAMESPACE::win32_code::domain_type>> sc(c);
  if(sc.failure())
  {
    detail::fill_failure_info(sc.value(), sc);
  }
  return sc;
}
//! Helper for constructing an error code from a NTSTATUS
inline file_io_error ntkernel_error(SYSTEM_ERROR2_NAMESPACE::win32::NTSTATUS c)
{
  SYSTEM_ERROR2_NAMESPACE::status_code<file_io_error_domain<SYSTEM_ERROR2_NAMESPACE::nt_code::domain_type>> sc(c);
  if(sc.failure())
  {
    detail::fill_failure_info(sc.value(), sc);
  }
  return sc;
}
#endif

#endif

#else  // LLFIO_EXPERIMENTAL_STATUS_CODE

// failure_info is defined in config.hpp, this is its constructor which needs
// to be defined here so that we have handle's definition available
inline error_info::error_info(std::error_code _ec)
    : ec(_ec)
{
// Here is a VERY useful place to breakpoint!
#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
  if(ec)
  {
    detail::fill_failure_info(*this, this->ec);
  }
#endif
}
#endif  // LLFIO_EXPERIMENTAL_STATUS_CODE

// Define how we log handles and subclasses thereof
namespace detail
{
  template <class T> void log_inst_to_info(const handle *inst, const char *buffer)
  {
    (void) inst;
    (void) buffer;
    LLFIO_LOG_INFO(inst->native_handle()._init, buffer);
  }
}  // namespace detail

// BEGIN make_free_functions.py
//! Swap with another instance
inline void swap(handle &self, handle &o) noexcept
{
  return self.swap(std::forward<decltype(o)>(o));
}
//! Immediately close the native handle type managed by this handle
inline result<void> close(handle &self) noexcept
{
  return self.close();
}
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#ifdef _WIN32
#include "detail/impl/windows/handle.ipp"
#else
#include "detail/impl/posix/handle.ipp"
#endif
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
