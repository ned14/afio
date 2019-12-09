/* A handle to something
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (8 commits)
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

#include "../../../file_handle.hpp"

#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

result<file_handle> file_handle::file(const path_handle &base, file_handle::path_view_type path, file_handle::mode _mode, file_handle::creation _creation, file_handle::caching _caching, file_handle::flag flags) noexcept
{
  result<file_handle> ret(file_handle(native_handle_type(), 0, 0, _caching, flags));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::file;
  OUTCOME_TRY(attribs, attribs_from_handle_mode_caching_and_flags(nativeh, _mode, _creation, _caching, flags));
  attribs &= ~O_NONBLOCK;
  nativeh.behaviour &= ~native_handle_type::disposition::nonblocking;
  path_view::c_str<> zpath(path);
  if(base.is_valid())
  {
    nativeh.fd = ::openat(base.native_handle().fd, zpath.buffer, attribs, 0x1b0 /*660*/);
  }
  else
  {
    nativeh.fd = ::open(zpath.buffer, attribs, 0x1b0 /*660*/);
  }
  if(-1 == nativeh.fd)
  {
    if((mode::write == _mode || mode::append == _mode) && creation::always_new == _creation && EEXIST == errno)
    {
      // Take a path handle to the directory containing the file
      auto path_parent = path.parent_path();
      path_handle dirh;
      if(base.is_valid() && path_parent.empty())
      {
        OUTCOME_TRY(dh, base.clone());
        dirh = std::move(dh);
      }
      else if(!path_parent.empty())
      {
        OUTCOME_TRY(dh, path_handle::path(base, path_parent));
        dirh = std::move(dh);
      }
      // Create a randomly named file, and rename it over
      OUTCOME_TRY(rfh, random_file(dirh, _mode, _caching, flags));
      auto r = rfh.relink(dirh, path.filename());
      if(r)
      {
        return std::move(rfh);
      }
      // If failed to rename, remove
      (void) rfh.unlink();
      return std::move(r).error();
    }
    return posix_error();
  }
  if((flags & flag::disable_prefetching) || (flags & flag::maximum_prefetching))
  {
#ifdef POSIX_FADV_SEQUENTIAL
    int advice = (flags & flag::disable_prefetching) ? POSIX_FADV_RANDOM : (POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);
    if(-1 == ::posix_fadvise(nativeh.fd, 0, 0, advice))
    {
      return posix_error();
    }
#elif __APPLE__
    int advice = (flags & flag::disable_prefetching) ? 0 : 1;
    if(-1 == ::fcntl(nativeh.fd, F_RDAHEAD, advice))
    {
      return posix_error();
    }
#endif
  }
  if(_creation == creation::truncate_existing && ret.value().are_safety_barriers_issued())
  {
    fsync(nativeh.fd);
  }
  return ret;
}

result<file_handle> file_handle::temp_inode(const path_handle &dirh, mode _mode, flag flags) noexcept
{
  caching _caching = caching::temporary;
  // No need to check inode before unlink
  flags |= flag::unlink_on_first_close | flag::disable_safety_unlinks;
  result<file_handle> ret(file_handle(native_handle_type(), 0, 0, _caching, flags));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::file;
  // Open file exclusively to prevent collision
  OUTCOME_TRY(attribs, attribs_from_handle_mode_caching_and_flags(nativeh, _mode, creation::only_if_not_exist, _caching, flags));
  attribs &= ~O_NONBLOCK;
  nativeh.behaviour &= ~native_handle_type::disposition::nonblocking;
#ifdef O_TMPFILE
  // Linux has a special flag just for this use case
  attribs |= O_TMPFILE;
  attribs &= ~O_EXCL;  // allow relinking later
  nativeh.fd = ::openat(dirh.native_handle().fd, "", attribs, 0600);
  if(-1 != nativeh.fd)
  {
    ret.value()._flags |= flag::anonymous_inode;
    ret.value()._flags &= ~flag::unlink_on_first_close;  // It's already unlinked
    return ret;
  }
  // If it failed, assume this kernel or FS doesn't support O_TMPFILE
  attribs &= ~O_TMPFILE;
  attribs |= O_EXCL;
#endif
  std::string random;
  for(;;)
  {
    try
    {
      random = utils::random_string(32) + ".tmp";
    }
    catch(...)
    {
      return error_from_exception();
    }
    nativeh.fd = ::openat(dirh.native_handle().fd, random.c_str(), attribs, 0600);  // user read/write perms only
    if(-1 == nativeh.fd)
    {
      int errcode = errno;
      if(EEXIST == errcode)
      {
        continue;
      }
      return posix_error(errcode);
    }
    // Immediately unlink after creation
    if(-1 == ::unlinkat(dirh.native_handle().fd, random.c_str(), 0))
    {
      return posix_error();
    }
    ret.value()._flags &= ~flag::unlink_on_first_close;
    return ret;
  }
}

result<file_handle> file_handle::clone(mode mode_, caching caching_, deadline d) const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  // Fast path
  if(mode_ == mode::unchanged)
  {
    result<file_handle> ret(file_handle(native_handle_type(), _devid, _inode, caching_, _flags));
    ret.value()._v.behaviour = _v.behaviour;
    ret.value()._v.fd = ::fcntl(_v.fd, F_DUPFD_CLOEXEC);
    if(-1 == ret.value()._v.fd)
    {
      return posix_error();
    }
    if(caching_ == caching::unchanged)
    {
      return ret;
    }

    int attribs = fcntl(ret.value()._v.fd, F_GETFL);
    if(-1 != attribs)
    {
      attribs &= ~(O_SYNC
#ifdef O_DIRECT
                   | O_DIRECT
#endif
#ifdef O_DSYNC
                   | O_DSYNC
#endif
      );
      switch(caching_)
      {
      case caching::unchanged:
        break;
      case caching::none:
        attribs |= O_SYNC
#ifdef O_DIRECT
                   | O_DIRECT
#endif
        ;
        if(-1 == fcntl(ret.value()._v.fd, F_SETFL, attribs))
        {
          return posix_error();
        }
        ret.value()._v.behaviour |= native_handle_type::disposition::aligned_io;
        break;
      case caching::only_metadata:
#ifdef O_DIRECT
        attribs |= O_DIRECT;
#endif
        if(-1 == fcntl(ret.value()._v.fd, F_SETFL, attribs))
        {
          return posix_error();
        }
        ret.value()._v.behaviour |= native_handle_type::disposition::aligned_io;
        break;
      case caching::reads:
        attribs |= O_SYNC;
        if(-1 == fcntl(ret.value()._v.fd, F_SETFL, attribs))
        {
          return posix_error();
        }
        ret.value()._v.behaviour &= ~native_handle_type::disposition::aligned_io;
        break;
      case caching::reads_and_metadata:
#ifdef O_DSYNC
        attribs |= O_DSYNC;
#else
        attribs |= O_SYNC;
#endif
        if(-1 == fcntl(ret.value()._v.fd, F_SETFL, attribs))
        {
          return posix_error();
        }
        ret.value()._v.behaviour &= ~native_handle_type::disposition::aligned_io;
        break;
      case caching::all:
      case caching::safety_barriers:
      case caching::temporary:
        if(-1 == fcntl(ret.value()._v.fd, F_SETFL, attribs))
        {
          return posix_error();
        }
        ret.value()._v.behaviour &= ~native_handle_type::disposition::aligned_io;
        break;
      }
      return ret;
    }
  }
  // Slow path
  std::chrono::steady_clock::time_point began_steady;
  std::chrono::system_clock::time_point end_utc;
  if(d)
  {
    if(d.steady)
    {
      began_steady = std::chrono::steady_clock::now();
    }
    else
    {
      end_utc = d.to_time_point();
    }
  }
  for(;;)
  {
    // Get the current path of myself
    OUTCOME_TRY(currentpath, current_path());
    // Open myself
    auto fh = file({}, currentpath, mode_, creation::open_existing, caching_, _flags);
    if(fh)
    {
      if(fh.value().unique_id() == unique_id())
      {
        return fh;
      }
    }
    else
    {
      if(fh.error() != errc::no_such_file_or_directory)
      {
        return std::move(fh).error();
      }
    }
    // Check timeout
    if(d)
    {
      if(d.steady)
      {
        if(std::chrono::steady_clock::now() >= (began_steady + std::chrono::nanoseconds(d.nsecs)))
        {
          return errc::timed_out;
        }
      }
      else
      {
        if(std::chrono::system_clock::now() >= end_utc)
        {
          return errc::timed_out;
        }
      }
    }
  }
}

result<file_handle::extent_type> file_handle::maximum_extent() const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  struct stat s
  {
  };
  memset(&s, 0, sizeof(s));
  if(-1 == ::fstat(_v.fd, &s))
  {
    return posix_error();
  }
  return s.st_size;
}

result<file_handle::extent_type> file_handle::truncate(file_handle::extent_type newsize) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  if(ftruncate(_v.fd, newsize) < 0)
  {
    return posix_error();
  }
  if(are_safety_barriers_issued())
  {
    fsync(_v.fd);
  }
  return newsize;
}

result<std::vector<std::pair<file_handle::extent_type, file_handle::extent_type>>> file_handle::extents() const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  try
  {
    std::vector<std::pair<file_handle::extent_type, file_handle::extent_type>> out;
    out.reserve(64);
    extent_type start = 0, end = 0;
    for(;;)
    {
#ifdef __linux__
#ifndef SEEK_DATA
      errno = EINVAL;
      break;
#else
      start = lseek64(_v.fd, end, SEEK_DATA);
      if(static_cast<extent_type>(-1) == start)
      {
        break;
      }
      end = lseek64(_v.fd, start, SEEK_HOLE);
      if(static_cast<extent_type>(-1) == end)
      {
        break;
      }
#endif
#elif defined(__APPLE__)
      // Can't find any support for extent enumeration in OS X
      errno = EINVAL;
      break;
#elif defined(__FreeBSD__)
      start = lseek(_v.fd, end, SEEK_DATA);
      if((extent_type) -1 == start)
        break;
      end = lseek(_v.fd, start, SEEK_HOLE);
      if((extent_type) -1 == end)
        break;
#else
#error Unknown system
#endif
      // Data region may have been concurrently deleted
      if(end > start)
      {
        out.emplace_back(start, end - start);
      }
    }
    if(ENXIO != errno)
    {
      if(EINVAL == errno)
      {
        // If it failed with no output, probably this filing system doesn't support extents
        if(out.empty())
        {
          OUTCOME_TRY(size, file_handle::maximum_extent());
          out.emplace_back(0, size);
          return out;
        }
      }
      else
      {
        return posix_error();
      }
    }
#if 0
  // A problem with SEEK_DATA and SEEK_HOLE is that they are racy under concurrent extents changing
  // Coalesce sequences of contiguous data e.g. 0, 64; 64, 64; 128, 64 ...
  std::vector<std::pair<extent_type, extent_type>> outfixed; outfixed.reserve(out.size());
  outfixed.push_back(out.front());
  for (size_t n = 1; n<out.size(); n++)
  {
    if (outfixed.back().first + outfixed.back().second == out[n].first)
      outfixed.back().second += out[n].second;
    else
      outfixed.push_back(out[n]);
  }
  return outfixed;
#endif
    return out;
  }
  catch(...)
  {
    return error_from_exception();
  }
}

result<file_handle::extent_type> file_handle::zero(file_handle::extent_type offset, file_handle::extent_type bytes, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
#if defined(__linux__)
  if(-1 == fallocate(_v.fd, 0x02 /*FALLOC_FL_PUNCH_HOLE*/ | 0x01 /*FALLOC_FL_KEEP_SIZE*/, offset, bytes))
  {
    // The filing system may not support trim
    if(EOPNOTSUPP != errno)
    {
      return posix_error();
    }
  }
#endif
  // Fall back onto a write of zeros
  if(bytes < utils::page_size())
  {
    auto *buffer = static_cast<byte *>(alloca((size_type) bytes));
    memset(buffer, 0, (size_type) bytes);
    OUTCOME_TRY(written, write(offset, {{buffer, (size_type) bytes}}, d));
    return written;
  }
  try
  {
    extent_type ret = 0;
    auto blocksize = utils::file_buffer_default_size();
    byte *buffer = utils::page_allocator<byte>().allocate(blocksize);
    auto unbufferh = undoer([buffer, blocksize] { utils::page_allocator<byte>().deallocate(buffer, blocksize); });
    (void) unbufferh;
    while(bytes > 0)
    {
      auto towrite = (bytes < blocksize) ? (size_t) bytes : blocksize;
      OUTCOME_TRY(written, write(offset, {{buffer, towrite}}, d));
      offset += written;
      bytes -= written;
      ret += written;
    }
    return ret;
  }
  catch(...)
  {
    return error_from_exception();
  }
}

LLFIO_V2_NAMESPACE_END
