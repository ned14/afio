/* A handle to a pipe
(C) 2015-2019 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
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

#ifndef LLFIO_PIPE_HANDLE_H
#define LLFIO_PIPE_HANDLE_H

#include "io_handle.hpp"

//! \file pipe_handle.hpp Provides `pipe_handle`

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

/*! \class pipe_handle
\brief A handle to a named or anonymous pipe.

The only fully portable use of this class is to *create* a named pipe with
read-only privileges (`pipe_create()`), and then to *open* an existing named pipe with
append-only privileges (`pipe_open()`). This ordering is important - it
works irrespective of whether the pipe is multiplexable or not.

This class doesn't prevent you opening fully duplex pipes
(i.e. `mode::write`) if your system supports them, but semantics in this
situation are implementation defined. Linux and Windows support fully
duplex pipes, and the Windows implementation matches the Linux bespoke
semantics.

When you create a named pipe, `flag::unlink_on_first_close` is
always forced on. This is due to portability reasons -
on some platforms (e.g. Windows), named pipes always get deleted when
the last handle to them is closed in the system, so the closest
matching semantic is for the creating handle to unlink its creation on
first close on all platforms. If you don't want this, release the native
handle before closing the handle instance, and take over its management.

If `flag::multiplexable` is specified which causes the handle to
be created as `native_handle_type::disposition::nonblocking`, opening
pipes for reads no longer blocks in the constructor. However it will then
block in `read()`, unless its deadline is zero. Opening pipes for write
in nonblocking mode will now fail if there is no reader present on the
other side of the pipe.

\warning On POSIX neither `creation::only_if_not_exist` nor
`creation::always_new` is atomic due to lack of kernel API support.

# Windows only

On Microsoft Windows, all pipes (including anonymous) are created within
the `\Device\NamedPipe\` region within the NT kernel namespace, which is the
ONLY place where pipes can exist on Windows (i.e. you cannot place them in
the filing system like on POSIX).

Because pipes can only exist in a single, global namespace shared amongst
all applications, and this is the same whether for Win32 or the NT kernel,
`pipe_handle` does not bother implementing the `\!!\` extension which forces
use of the NT kernel API. All named pipes always operate out of the NT kernel
namespace. `.parent_path_handle()` always returns
`path_discovery::temporary_named_pipes_directory()` on Windows.

So long as you use `path_discovery::temporary_named_pipes_directory()`
as your base directory, you can write quite portable code between POSIX
and Windows.
*/
class LLFIO_DECL pipe_handle : public io_handle, public fs_handle
{
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC const handle &_get_handle() const noexcept final { return *this; }

  void _set_is_connected(bool v) noexcept { this->_spare1 = v; }
  bool _is_connected() const noexcept { return this->_spare1 != 0; }

public:
  using path_type = io_handle::path_type;
  using extent_type = io_handle::extent_type;
  using size_type = io_handle::size_type;
  using mode = io_handle::mode;
  using creation = io_handle::creation;
  using caching = io_handle::caching;
  using flag = io_handle::flag;
  using buffer_type = io_handle::buffer_type;
  using const_buffer_type = io_handle::const_buffer_type;
  using buffers_type = io_handle::buffers_type;
  using const_buffers_type = io_handle::const_buffers_type;
  template <class T> using io_request = io_handle::io_request<T>;
  template <class T> using io_result = io_handle::io_result<T>;
  using dev_t = fs_handle::dev_t;
  using ino_t = fs_handle::ino_t;
  using path_view_type = fs_handle::path_view_type;

public:
  //! Default constructor
  constexpr pipe_handle() {}  // NOLINT
  //! Construct a handle from a supplied native handle
  constexpr pipe_handle(native_handle_type h, dev_t devid, ino_t inode, caching caching = caching::none, flag flags = flag::none, io_context *ctx = nullptr)
      : io_handle(std::move(h), caching, flags, ctx)
      , fs_handle(devid, inode)
  {
  }
  //! No copy construction (use clone())
  pipe_handle(const pipe_handle &) = delete;
  //! No copy assignment
  pipe_handle &operator=(const pipe_handle &) = delete;
  //! Implicit move construction of `pipe_handle` permitted
  constexpr pipe_handle(pipe_handle &&o) noexcept
      : io_handle(std::move(o))
      , fs_handle(std::move(o))
  {
  }
  //! Explicit conversion from handle and io_handle permitted
  explicit constexpr pipe_handle(handle &&o, dev_t devid, ino_t inode) noexcept
      : io_handle(std::move(o))
      , fs_handle(devid, inode)
  {
  }
  //! Move assignment of `pipe_handle` permitted
  pipe_handle &operator=(pipe_handle &&o) noexcept
  {
    this->~pipe_handle();
    new(this) pipe_handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  LLFIO_MAKE_FREE_FUNCTION
  void swap(pipe_handle &o) noexcept
  {
    pipe_handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  /*! Create a pipe handle opening access to a named pipe
  \param path The path relative to base to open.
  \param _mode How to open the pipe.
  \param _creation How to create the pipe.
  \param _caching How to ask the kernel to cache the pipe.
  \param flags Any additional custom behaviours.
  \param base Handle to a base location on the filing system.
  Defaults to `path_discovery::temporary_named_pipes_directory()`.

  \errors Any of the values POSIX `open()`, `mkfifo()`, `NtCreateFile()` or `NtCreateNamedPipeFile()` can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<pipe_handle> pipe(path_view_type path, mode _mode, creation _creation, caching _caching = caching::all, flag flags = flag::none, const path_handle &base = path_discovery::temporary_named_pipes_directory()) noexcept;
  /*! Convenience overload for `pipe()` creating a new named pipe if
  needed, and with read-only privileges. Unless `flag::multiplexable`
  is specified, this will block until the other end connects.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static inline result<pipe_handle> pipe_create(path_view_type path, caching _caching = caching::all, flag flags = flag::none, const path_handle &base = path_discovery::temporary_named_pipes_directory()) noexcept { return pipe(path, mode::read, creation::if_needed, _caching, flags, base); }
  /*! Convenience overload for `pipe()` opening an existing named pipe
  with write-only privileges. This will fail if no reader is waiting
  on the other end of the pipe.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static inline result<pipe_handle> pipe_open(path_view_type path, caching _caching = caching::all, flag flags = flag::none, const path_handle &base = path_discovery::temporary_named_pipes_directory()) noexcept { return pipe(path, mode::append, creation::open_existing, _caching, flags, base); }

  /*! Create a pipe handle creating a randomly named pipe on a path.
  The pipe is opened exclusively with `creation::only_if_not_exist` so it
  will never collide with nor overwrite any existing pipe.

  \errors Any of the values POSIX `open()`, `mkfifo()`, `NtCreateFile()` or `NtCreateNamedPipeFile()` can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static inline result<pipe_handle> random_pipe(mode _mode = mode::read, caching _caching = caching::all, flag flags = flag::none, const path_handle &dirpath = path_discovery::temporary_named_pipes_directory()) noexcept
  {
    try
    {
      for(;;)
      {
        auto randomname = utils::random_string(32);
        randomname.append(".random");
        result<pipe_handle> ret = pipe(randomname, _mode, creation::only_if_not_exist, _caching, flags, dirpath);
        if(ret || (!ret && ret.error() != errc::file_exists))
        {
          return ret;
        }
      }
    }
    catch(...)
    {
      return error_from_exception();
    }
  }
  /*! \em Securely create two ends of an anonymous pipe handle. The first
  handle returned is the read end; the second is the write end.

  Unlike Windows' `CreatePipe()`, this function can create non-blocking
  anonymous pipes. These are truly anonymous, not just randomly named.

  \errors Any of the values POSIX `pipe()` or `NtCreateNamedPipeFile()` can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::pair<pipe_handle, pipe_handle>> anonymous_pipe(caching _caching = caching::all, flag flags = flag::none) noexcept;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~pipe_handle() override
  {
    if(_v)
    {
      (void) pipe_handle::close();
    }
  }
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept override
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(_flags & flag::unlink_on_first_close)
    {
      auto ret = unlink();
      if(!ret)
      {
        // File may have already been deleted, if so ignore
        if(ret.error() != errc::no_such_file_or_directory)
        {
          return std::move(ret).error();
        }
      }
    }
#ifndef NDEBUG
    if(_v)
    {
      // Tell handle::close() that we have correctly executed
      _v.behaviour |= native_handle_type::disposition::_child_close_executed;
    }
#endif
    return io_handle::close();
  }

  using io_handle::read;
  using io_handle::write;
#ifdef _WIN32
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<path_handle> parent_path_handle(deadline /*unused*/ = std::chrono::seconds(30)) const noexcept override
  {
    // Pipes parent handle is always the NT kernel namespace for pipes
    return path_discovery::temporary_named_pipes_directory().clone();
  }
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<buffers_type> read(io_request<buffers_type> reqs, deadline d = deadline()) noexcept override;
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> write(io_request<const_buffers_type> reqs, deadline d = deadline()) noexcept override;
#endif
  //! Convenience initialiser list based overload for `read()`
  LLFIO_MAKE_FREE_FUNCTION
  io_result<size_type> read(extent_type offset, std::initializer_list<buffer_type> lst, deadline d = deadline()) noexcept
  {
    buffer_type *_reqs = reinterpret_cast<buffer_type *>(alloca(sizeof(buffer_type) * lst.size()));
    memcpy(_reqs, lst.begin(), sizeof(buffer_type) * lst.size());
    io_request<buffers_type> reqs(buffers_type(_reqs, lst.size()), offset);
    auto ret = read(reqs, d);
    if(ret)
    {
      return ret.bytes_transferred();
    }
    return std::move(ret).error();
  }

  LLFIO_DEADLINE_TRY_FOR_UNTIL(read)
};

//! \brief Constructor for `pipe_handle`
template <> struct construct<pipe_handle>
{
  pipe_handle::path_view_type _path;
  pipe_handle::mode _mode = pipe_handle::mode::read;
  pipe_handle::creation _creation = pipe_handle::creation::if_needed;
  pipe_handle::caching _caching = pipe_handle::caching::all;
  pipe_handle::flag flags = pipe_handle::flag::none;
  const path_handle &base = path_discovery::temporary_named_pipes_directory();
  result<pipe_handle> operator()() const noexcept { return pipe_handle::pipe(_path, _mode, _creation, _caching, flags, base); }
};

// BEGIN make_free_functions.py
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#ifdef _WIN32
#include "detail/impl/windows/pipe_handle.ipp"
#else
#include "detail/impl/posix/pipe_handle.ipp"
#endif
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#endif
