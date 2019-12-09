/* A handle to a file
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
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

#ifndef LLFIO_FILE_HANDLE_H
#define LLFIO_FILE_HANDLE_H

#include "lockable_io_handle.hpp"
#include "path_discovery.hpp"
#include "utils.hpp"

//! \file file_handle.hpp Provides file_handle

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

/*! \class file_handle
\brief A handle to a regular file or device, kept data layout compatible with
async_file_handle.

<table>
<tr><th></th><th>Cost of opening</th><th>Cost of i/o</th><th>Concurrency and Atomicity</th><th>Other remarks</th></tr>
<tr><td>`file_handle`</td><td>Least</td><td>Syscall</td><td>POSIX guarantees (usually)</td><td>Least gotcha</td></tr>
<tr><td>`async_file_handle`</td><td>More</td><td>Most (syscall + malloc/free + reactor)</td><td>POSIX guarantees (usually)</td><td>Makes no sense to use with cached i/o as it's a very expensive way to call `memcpy()`</td></tr>
<tr><td>`mapped_file_handle`</td><td>Most</td><td>Least</td><td>None</td><td>Cannot be used with uncached i/o</td></tr>
</table>

*/
class LLFIO_DECL file_handle : public lockable_io_handle, public fs_handle
{
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC const handle &_get_handle() const noexcept final { return *this; }

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
  constexpr file_handle() {}  // NOLINT
  //! Construct a handle from a supplied native handle
  constexpr file_handle(native_handle_type h, dev_t devid, ino_t inode, caching caching = caching::none, flag flags = flag::none)
      : lockable_io_handle(std::move(h), caching, flags)
      , fs_handle(devid, inode)
  {
  }
  //! No copy construction (use clone())
  file_handle(const file_handle &) = delete;
  //! No copy assignment
  file_handle &operator=(const file_handle &) = delete;
  //! Implicit move construction of file_handle permitted
  constexpr file_handle(file_handle &&o) noexcept
      : lockable_io_handle(std::move(o))
      , fs_handle(std::move(o))
  {
  }
  //! Explicit conversion from handle and io_handle permitted
  explicit constexpr file_handle(handle &&o, dev_t devid, ino_t inode) noexcept
      : lockable_io_handle(std::move(o))
      , fs_handle(devid, inode)
  {
  }
  //! Move assignment of file_handle permitted
  file_handle &operator=(file_handle &&o) noexcept
  {
    this->~file_handle();
    new(this) file_handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  LLFIO_MAKE_FREE_FUNCTION
  void swap(file_handle &o) noexcept
  {
    file_handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  /*! Create a file handle opening access to a file on path
  \param base Handle to a base location on the filing system. Pass `{}` to indicate that path will be absolute.
  \param path The path relative to base to open.
  \param _mode How to open the file.
  \param _creation How to create the file.
  \param _caching How to ask the kernel to cache the file.
  \param flags Any additional custom behaviours.

  \errors Any of the values POSIX open() or CreateFile() can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<file_handle> file(const path_handle &base, path_view_type path, mode _mode = mode::read, creation _creation = creation::open_existing, caching _caching = caching::all, flag flags = flag::none) noexcept;
  /*! Create a file handle creating a randomly named file on a path.
  The file is opened exclusively with `creation::only_if_not_exist` so it
  will never collide with nor overwrite any existing file. Note also
  that caching defaults to temporary which hints to the OS to only
  flush changes to physical storage as lately as possible.

  \errors Any of the values POSIX open() or CreateFile() can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static inline result<file_handle> random_file(const path_handle &dirpath, mode _mode = mode::write, caching _caching = caching::temporary, flag flags = flag::none) noexcept
  {
    try
    {
      for(;;)
      {
        auto randomname = utils::random_string(32);
        randomname.append(".random");
        result<file_handle> ret = file(dirpath, randomname, _mode, creation::only_if_not_exist, _caching, flags);
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
  /*! Create a file handle creating the named file on some path which
  the OS declares to be suitable for temporary files. Most OSs are
  very lazy about flushing changes made to these temporary files.
  Note the default flags are to have the newly created file deleted
  on first handle close.
  Note also that an empty name is equivalent to calling
  `random_file(path_discovery::storage_backed_temporary_files_directory())` and the creation
  parameter is ignored.

  \note If the temporary file you are creating is not going to have its
  path sent to another process for usage, this is the WRONG function
  to use. Use `temp_inode()` instead, it is far more secure.

  \errors Any of the values POSIX open() or CreateFile() can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static inline result<file_handle> temp_file(path_view_type name = path_view_type(), mode _mode = mode::write, creation _creation = creation::if_needed, caching _caching = caching::temporary, flag flags = flag::unlink_on_first_close) noexcept
  {
    auto &tempdirh = path_discovery::storage_backed_temporary_files_directory();
    return name.empty() ? random_file(tempdirh, _mode, _caching, flags) : file(tempdirh, name, _mode, _creation, _caching, flags);
  }
  /*! \em Securely create a file handle creating a temporary anonymous inode in
  the filesystem referred to by \em dirpath. The inode created has
  no name nor accessible path on the filing system and ceases to
  exist as soon as the last handle is closed, making it ideal for use as
  a temporary file where other processes do not need to have access
  to its contents via some path on the filing system (a classic use case
  is for backing shared memory maps).

  \errors Any of the values POSIX open() or CreateFile() can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  static LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<file_handle> temp_inode(const path_handle &dirh = path_discovery::storage_backed_temporary_files_directory(), mode _mode = mode::write, flag flags = flag::none) noexcept;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC ~file_handle() override
  {
    if(_v)
    {
      (void) file_handle::close();
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

  /*! Clone this handle (copy constructor is disabled to avoid accidental copying),
  optionally race free reopening the handle with different access or caching.

  Microsoft Windows provides a syscall for cloning an existing handle but with new
  access. On POSIX, if not changing the mode, we change caching via `fcntl()`, if
  changing the mode we must loop calling `current_path()`,
  trying to open the path returned and making sure it is the same inode.

  \errors Any of the values POSIX dup() or DuplicateHandle() can return.
  \mallocs On POSIX if changing the mode, we must loop calling `current_path()` and
  trying to open the path returned. Thus many allocations may occur.
  */
  result<file_handle> clone(mode mode_ = mode::unchanged, caching caching_ = caching::unchanged, deadline d = std::chrono::seconds(30)) const noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(clone)

  /*! Return the current maximum permitted extent of the file.

  \errors Any of the values POSIX fstat() or GetFileInformationByHandleEx() can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<extent_type> maximum_extent() const noexcept;

  /*! Resize the current maximum permitted extent of the file to the given extent, avoiding any
  new allocation of physical storage where supported. Note that on extents based filing systems
  this will succeed even if there is insufficient free space on the storage medium.

  \return The bytes actually truncated to.
  \param newsize The bytes to truncate the file to.
  \errors Any of the values POSIX ftruncate() or SetFileInformationByHandle() can return.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<extent_type> truncate(extent_type newsize) noexcept;

  /*! \brief Returns a list of currently valid extents for this open file. WARNING: racy!
  \return A vector of pairs of extent offset + extent length representing the valid extents
  in this file. Filing systems which do not support extents return a single extent matching
  the length of the file rather than returning an error.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<std::vector<std::pair<extent_type, extent_type>>> extents() const noexcept;

  /*! \brief Efficiently zero, and possibly deallocate, data on storage.

  On most major operating systems and with recent filing systems which are "extents based", one can
  deallocate the physical storage of a file, causing the space deallocated to appear all bits zero.
  This call attempts to deallocate whole pages (usually 4Kb) entirely, and memset's any excess to all
  bits zero. This call works on most Linux filing systems with a recent kernel, Microsoft Windows
  with NTFS, and FreeBSD with ZFS. On other systems it simply writes zeros.

  \return The bytes zeroed.
  \param offset The offset to start zeroing from.
  \param bytes The number of bytes to zero.
  \param d An optional deadline by which the i/o must complete, else it is cancelled.
  Note function may return significantly after this deadline if the i/o takes long to cancel.
  \errors Any of the values POSIX write() can return, `errc::timed_out`, `errc::operation_canceled`.
  `errc::not_supported` may be returned if deadline i/o is not possible with this particular
  handle configuration (e.g. writing to regular files on POSIX or writing to a non-overlapped
  HANDLE on Windows).
  \mallocs The default synchronous implementation in file_handle performs no memory allocation.
  The asynchronous implementation in async_file_handle may perform one calloc and one free.
  */
  LLFIO_MAKE_FREE_FUNCTION
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<extent_type> zero(extent_type offset, extent_type bytes, deadline d = deadline()) noexcept;

  LLFIO_DEADLINE_TRY_FOR_UNTIL(zero)
};

//! \brief Constructor for `file_handle`
template <> struct construct<file_handle>
{
  const path_handle &base;
  file_handle::path_view_type _path;
  file_handle::mode _mode = file_handle::mode::read;
  file_handle::creation _creation = file_handle::creation::open_existing;
  file_handle::caching _caching = file_handle::caching::all;
  file_handle::flag flags = file_handle::flag::none;
  result<file_handle> operator()() const noexcept { return file_handle::file(base, _path, _mode, _creation, _caching, flags); }
};

// BEGIN make_free_functions.py
//! Swap with another instance
inline void swap(file_handle &self, file_handle &o) noexcept
{
  return self.swap(std::forward<decltype(o)>(o));
}
/*! Create a file handle opening access to a file on path
\param base Handle to a base location on the filing system. Pass `{}` to indicate that path will be absolute.
\param path The path relative to base to open.
\param _mode How to open the file.
\param _creation How to create the file.
\param _caching How to ask the kernel to cache the file.
\param flags Any additional custom behaviours.

\errors Any of the values POSIX open() or CreateFile() can return.
*/
inline result<file_handle> file(const path_handle &base, file_handle::path_view_type path, file_handle::mode _mode = file_handle::mode::read, file_handle::creation _creation = file_handle::creation::open_existing, file_handle::caching _caching = file_handle::caching::all,
                                file_handle::flag flags = file_handle::flag::none) noexcept
{
  return file_handle::file(std::forward<decltype(base)>(base), std::forward<decltype(path)>(path), std::forward<decltype(_mode)>(_mode), std::forward<decltype(_creation)>(_creation), std::forward<decltype(_caching)>(_caching), std::forward<decltype(flags)>(flags));
}
/*! Create a file handle creating a randomly named file on a path.
The file is opened exclusively with `creation::only_if_not_exist` so it
will never collide with nor overwrite any existing file. Note also
that caching defaults to temporary which hints to the OS to only
flush changes to physical storage as lately as possible.

\errors Any of the values POSIX open() or CreateFile() can return.
*/
inline result<file_handle> random_file(const path_handle &dirpath, file_handle::mode _mode = file_handle::mode::write, file_handle::caching _caching = file_handle::caching::temporary, file_handle::flag flags = file_handle::flag::none) noexcept
{
  return file_handle::random_file(std::forward<decltype(dirpath)>(dirpath), std::forward<decltype(_mode)>(_mode), std::forward<decltype(_caching)>(_caching), std::forward<decltype(flags)>(flags));
}
/*! Create a file handle creating the named file on some path which
the OS declares to be suitable for temporary files. Most OSs are
very lazy about flushing changes made to these temporary files.
Note the default flags are to have the newly created file deleted
on first handle close.
Note also that an empty name is equivalent to calling
`random_file(path_discovery::storage_backed_temporary_files_directory())` and the creation
parameter is ignored.

\note If the temporary file you are creating is not going to have its
path sent to another process for usage, this is the WRONG function
to use. Use `temp_inode()` instead, it is far more secure.

\errors Any of the values POSIX open() or CreateFile() can return.
*/
inline result<file_handle> temp_file(file_handle::path_view_type name = file_handle::path_view_type(), file_handle::mode _mode = file_handle::mode::write, file_handle::creation _creation = file_handle::creation::if_needed, file_handle::caching _caching = file_handle::caching::temporary,
                                     file_handle::flag flags = file_handle::flag::unlink_on_first_close) noexcept
{
  return file_handle::temp_file(std::forward<decltype(name)>(name), std::forward<decltype(_mode)>(_mode), std::forward<decltype(_creation)>(_creation), std::forward<decltype(_caching)>(_caching), std::forward<decltype(flags)>(flags));
}
/*! \em Securely create a file handle creating a temporary anonymous inode in
the filesystem referred to by \em dirpath. The inode created has
no name nor accessible path on the filing system and ceases to
exist as soon as the last handle is closed, making it ideal for use as
a temporary file where other processes do not need to have access
to its contents via some path on the filing system (a classic use case
is for backing shared memory maps).

\errors Any of the values POSIX open() or CreateFile() can return.
*/
inline result<file_handle> temp_inode(const path_handle &dirh = path_discovery::storage_backed_temporary_files_directory(), file_handle::mode _mode = file_handle::mode::write, file_handle::flag flags = file_handle::flag::none) noexcept
{
  return file_handle::temp_inode(std::forward<decltype(dirh)>(dirh), std::forward<decltype(_mode)>(_mode), std::forward<decltype(flags)>(flags));
}
//! \overload
inline file_handle::io_result<file_handle::size_type> read(file_handle &self, file_handle::extent_type offset, std::initializer_list<file_handle::buffer_type> lst, deadline d = deadline()) noexcept
{
  return self.read(std::forward<decltype(offset)>(offset), std::forward<decltype(lst)>(lst), std::forward<decltype(d)>(d));
}
/*! Return the current maximum permitted extent of the file.

\errors Any of the values POSIX fstat() or GetFileInformationByHandleEx() can return.
*/
inline result<file_handle::extent_type> maximum_extent(const file_handle &self) noexcept
{
  return self.maximum_extent();
}
/*! Resize the current maximum permitted extent of the file to the given extent, avoiding any
new allocation of physical storage where supported. Note that on extents based filing systems
this will succeed even if there is insufficient free space on the storage medium.

\return The bytes actually truncated to.
\param self The object whose member function to call.
\param newsize The bytes to truncate the file to.
\errors Any of the values POSIX ftruncate() or SetFileInformationByHandle() can return.
*/
inline result<file_handle::extent_type> truncate(file_handle &self, file_handle::extent_type newsize) noexcept
{
  return self.truncate(std::forward<decltype(newsize)>(newsize));
}
/*! \brief Returns a list of currently valid extents for this open file. WARNING: racy!
*/
inline result<std::vector<std::pair<file_handle::extent_type, file_handle::extent_type>>> extents(const file_handle &self) noexcept
{
  return self.extents();
}
/*! \brief Efficiently zero, and possibly deallocate, data on storage.

On most major operating systems and with recent filing systems which are "extents based", one can
deallocate the physical storage of a file, causing the space deallocated to appear all bits zero.
This call attempts to deallocate whole pages (usually 4Kb) entirely, and memset's any excess to all
bits zero. This call works on most Linux filing systems with a recent kernel, Microsoft Windows
with NTFS, and FreeBSD with ZFS. On other systems it simply writes zeros.

\return The bytes zeroed.
\param self The object whose member function to call.
\param offset The offset to start zeroing from.
\param bytes The number of bytes to zero.
\param d An optional deadline by which the i/o must complete, else it is cancelled.
Note function may return significantly after this deadline if the i/o takes long to cancel.
\errors Any of the values POSIX write() can return, `errc::timed_out`, `errc::operation_canceled`. `errc::not_supported` may be
returned if deadline i/o is not possible with this particular handle configuration (e.g.
writing to regular files on POSIX or writing to a non-overlapped HANDLE on Windows).
\mallocs The default synchronous implementation in file_handle performs no memory allocation.
The asynchronous implementation in async_file_handle may perform one calloc and one free.
*/
inline result<file_handle::extent_type> zero(file_handle &self, file_handle::extent_type offset, file_handle::extent_type bytes, deadline d = deadline()) noexcept
{
  return self.zero(std::forward<decltype(offset)>(offset), std::forward<decltype(bytes)>(bytes), std::forward<decltype(d)>(d));
}
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#ifdef _WIN32
#include "detail/impl/windows/file_handle.ipp"
#else
#include "detail/impl/posix/file_handle.ipp"
#endif
#undef LLFIO_INCLUDED_BY_HEADER
#endif

// Needs to be here as path discovery uses file_handle
#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#include "detail/impl/path_discovery.ipp"
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
