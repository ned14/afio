/* A handle to a directory
(C) 2017 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
File Created: Aug 2017


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

#include "../../../directory_handle.hpp"
#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

result<directory_handle> directory_handle::directory(const path_handle &base, path_view_type path, mode _mode, creation _creation, caching _caching, flag flags) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  if(flags & flag::unlink_on_first_close)
  {
    return errc::invalid_argument;
  }
  result<directory_handle> ret(directory_handle(native_handle_type(), 0, 0, _caching, flags));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::directory;
  DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  // Trying to truncate a directory returns EISDIR rather than some internal Win32 error code uncomparable to errc
  if(_creation == creation::truncate_existing)
  {
    return errc::is_a_directory;
  }
  OUTCOME_TRY(access, access_mask_from_handle_mode(nativeh, _mode, flags));
  OUTCOME_TRY(attribs, attributes_from_handle_caching_and_flags(nativeh, _caching, flags));
  nativeh.behaviour &= ~native_handle_type::disposition::seekable;  // not seekable
  /* It is super important that we remove the DELETE permission for directories as otherwise relative renames
  will always fail due to an unfortunate design choice by Microsoft. This breaks renaming by open handle,
  see relink() for the hack workaround. It also breaks creation::always_new, which ought to be implemented
  by opening the handle with DELETE, then reopening the file without DELETE, which I haven't bothered
  doing below (pull requests are welcome!).
  */
  access &= ~DELETE;
  bool need_to_set_case_sensitive = false;
  if(base.is_valid() || path.is_ntpath())
  {
    DWORD creatdisp = 0x00000001 /*FILE_OPEN*/;
    switch(_creation)
    {
    case creation::open_existing:
      break;
    case creation::only_if_not_exist:
      creatdisp = 0x00000002 /*FILE_CREATE*/;
      break;
    case creation::if_needed:
      creatdisp = 0x00000003 /*FILE_OPEN_IF*/;
      break;
    case creation::truncate_existing:
      creatdisp = 0x00000004 /*FILE_OVERWRITE*/;
      break;
    case creation::always_new:
      // creatdisp = 0x00000000 /*FILE_SUPERSEDE*/;
      // Superseding won't work without DELETE permission, so ...
      creatdisp = 0x00000002 /*FILE_CREATE*/;
      break;
    }

    attribs &= 0x00ffffff;  // the real attributes only, not the win32 flags
    OUTCOME_TRY(ntflags, ntflags_from_handle_caching_and_flags(nativeh, _caching, flags));
    ntflags |= 0x01 /*FILE_DIRECTORY_FILE*/;  // required to open a directory
    IO_STATUS_BLOCK isb = make_iostatus();

    path_view::c_str<> zpath(path, true);
    UNICODE_STRING _path{};
    _path.Buffer = const_cast<wchar_t *>(zpath.buffer);
    _path.MaximumLength = (_path.Length = static_cast<USHORT>(zpath.length * sizeof(wchar_t))) + sizeof(wchar_t);
    if(zpath.length >= 4 && _path.Buffer[0] == '\\' && _path.Buffer[1] == '!' && _path.Buffer[2] == '!' && _path.Buffer[3] == '\\')
    {
      _path.Buffer += 3;
      _path.Length -= 3 * sizeof(wchar_t);
      _path.MaximumLength -= 3 * sizeof(wchar_t);
    }

    OBJECT_ATTRIBUTES oa{};
    memset(&oa, 0, sizeof(oa));
    oa.Length = sizeof(OBJECT_ATTRIBUTES);
    oa.ObjectName = &_path;
    oa.RootDirectory = base.is_valid() ? base.native_handle().h : nullptr;
    oa.Attributes = 0;  // 0x40 /*OBJ_CASE_INSENSITIVE*/;
    // if(!!(flags & file_flags::int_opening_link))
    //  oa.Attributes|=0x100/*OBJ_OPENLINK*/;

    LARGE_INTEGER AllocationSize{};
    memset(&AllocationSize, 0, sizeof(AllocationSize));
    NTSTATUS ntstat = NtCreateFile(&nativeh.h, access, &oa, &isb, &AllocationSize, attribs, fileshare, creatdisp, ntflags, nullptr, 0);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(nativeh.h, isb, deadline());
    }
    if(ntstat < 0)
    {
      if(creation::always_new == _creation && (NTSTATUS) 0xc0000035 /*STATUS_OBJECT_NAME_COLLISION*/ == ntstat)
      {
        return errc::directory_not_empty;
      }
      return ntkernel_error(ntstat);
    }
    switch(_creation)
    {
    case creation::open_existing:
      need_to_set_case_sensitive = false;
      break;
    case creation::only_if_not_exist:
      need_to_set_case_sensitive = true;
      break;
    case creation::if_needed:
      need_to_set_case_sensitive = (isb.Information == 2 /*FILE_CREATED*/);
      break;
    case creation::truncate_existing:
    case creation::always_new:
      need_to_set_case_sensitive = true;
      break;
    }
  }
  else
  {
    DWORD creation = OPEN_EXISTING;
    switch(_creation)
    {
    case creation::open_existing:
      break;
    case creation::only_if_not_exist:
      creation = CREATE_NEW;
      break;
    case creation::if_needed:
      creation = OPEN_ALWAYS;
      break;
    case creation::truncate_existing:
      creation = TRUNCATE_EXISTING;
      break;
    case creation::always_new:
      // creation = CREATE_ALWAYS;
      // Superseding won't work without DELETE permission, so ...
      creation = CREATE_NEW;
      break;
    }
    attribs |= FILE_FLAG_BACKUP_SEMANTICS;  // required to open a directory
    path_view::c_str<> zpath(path, false);
    if(INVALID_HANDLE_VALUE == (nativeh.h = CreateFileW_(zpath.buffer, access, fileshare, nullptr, creation, attribs, nullptr, true)))  // NOLINT
    {
      DWORD errcode = GetLastError();
      if(creation::always_new == _creation && 0xb7 /*ERROR_ALREADY_EXISTS*/ == errcode)
      {
        return errc::directory_not_empty;
      }
      // assert(false);
      return win32_error(errcode);
    }
    switch(_creation)
    {
    case creation::open_existing:
      need_to_set_case_sensitive = false;
      break;
    case creation::only_if_not_exist:
      need_to_set_case_sensitive = true;
      break;
    case creation::if_needed:
      need_to_set_case_sensitive = (GetLastError() != ERROR_ALREADY_EXISTS);  // FIXME: doesn't appear to detect when new directory not created
      break;
    case creation::truncate_existing:
    case creation::always_new:
      need_to_set_case_sensitive = true;
      break;
    }
  }
  if(_mode == mode::write && need_to_set_case_sensitive && (flags & flag::win_create_case_sensitive_directory))
  {
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_CASE_SENSITIVE_INFORMATION fcsi;
    fcsi.Flags = 1 /*FILE_CS_FLAG_CASE_SENSITIVE_DIR*/;
    NTSTATUS ntstat = NtSetInformationFile(nativeh.h, &isb, &fcsi, sizeof(fcsi), FileCaseSensitiveInformation);
    if(ntstat < 0)
    {
      auto r = ntkernel_error(ntstat);
      LLFIO_LOG_WARN(&ret, "Failed to set directory to case sensitive");
      (void) r;  // throw away
    }
  }
  return ret;
}

result<directory_handle> directory_handle::clone(mode mode_, caching caching_, deadline /* unused */) const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  result<directory_handle> ret(directory_handle(native_handle_type(), _devid, _inode, _caching, _flags));
  OUTCOME_TRY(do_clone_handle(ret.value()._v, _v, mode_, caching_, _flags, true));
  return ret;
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<path_handle> directory_handle::clone_to_path_handle() const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  result<path_handle> ret(path_handle(native_handle_type(), _caching, _flags));
  ret.value()._v.behaviour = _v.behaviour;
  if(DuplicateHandle(GetCurrentProcess(), _v.h, GetCurrentProcess(), &ret.value()._v.h, 0, 0, DUPLICATE_SAME_ACCESS) == 0)
  {
    return win32_error();
  }
  return ret;
}

namespace detail
{
  inline result<file_handle> duplicate_handle_with_delete_privs(directory_handle *o) noexcept
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    native_handle_type nativeh = o->native_handle();
    DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    OBJECT_ATTRIBUTES oa{};
    memset(&oa, 0, sizeof(oa));
    oa.Length = sizeof(OBJECT_ATTRIBUTES);
    // It is entirely undocumented that this is how you clone a file handle with new privs
    UNICODE_STRING _path{};
    memset(&_path, 0, sizeof(_path));
    oa.ObjectName = &_path;
    oa.RootDirectory = o->native_handle().h;
    IO_STATUS_BLOCK isb = make_iostatus();
    NTSTATUS ntstat = NtOpenFile(&nativeh.h, GENERIC_READ | SYNCHRONIZE | DELETE, &oa, &isb, fileshare, 0x01 /*FILE_DIRECTORY_FILE*/ | 0x20 /*FILE_SYNCHRONOUS_IO_NONALERT*/);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(nativeh.h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }
    // Return as a file handle so the direct relink and unlink are used
    return file_handle(nativeh, 0, 0, file_handle::caching::all);
  }
}  // namespace detail

result<void> directory_handle::relink(const path_handle &base, directory_handle::path_view_type newpath, bool atomic_replace, deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  /* We can never hold DELETE permission on an open handle to a directory as otherwise
  race free renames into that directory will fail, so we are forced to duplicate the
  handle with DELETE privs temporarily in order to issue the rename
  */
  OUTCOME_TRY(h, detail::duplicate_handle_with_delete_privs(this));
  return h.relink(base, newpath, atomic_replace, d);
}

result<void> directory_handle::unlink(deadline d) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  /* We can never hold DELETE permission on an open handle to a directory as otherwise
  race free renames into that directory will fail, so we are forced to duplicate the
  handle with DELETE privs temporarily in order to issue the unlink
  */
  OUTCOME_TRY(h, detail::duplicate_handle_with_delete_privs(this));
  return h.unlink(d);
}

result<directory_handle::buffers_type> directory_handle::read(io_request<buffers_type> req) const noexcept
{
  static constexpr stat_t::want default_stat_contents = stat_t::want::ino | stat_t::want::type | stat_t::want::atim | stat_t::want::mtim | stat_t::want::ctim | stat_t::want::size | stat_t::want::allocated | stat_t::want::birthtim | stat_t::want::sparse | stat_t::want::compressed | stat_t::want::reparse_point;
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  if(req.buffers.empty())
  {
    return std::move(req.buffers);
  }
  UNICODE_STRING _glob{};
  memset(&_glob, 0, sizeof(_glob));
  path_view_type::c_str<> zglob(req.glob, true);
  if(!req.glob.empty())
  {
    _glob.Buffer = const_cast<wchar_t *>(zglob.buffer);
    _glob.Length = (USHORT)(zglob.length * sizeof(wchar_t));
    _glob.MaximumLength = _glob.Length + sizeof(wchar_t);
  }
  if(!req.buffers._kernel_buffer && req.kernelbuffer.empty())
  {
    // Let's assume the average leafname will be 64 characters long.
    size_t toallocate = (sizeof(FILE_ID_FULL_DIR_INFORMATION) + 64 * sizeof(wchar_t)) * req.buffers.size();
    auto *mem = new(std::nothrow) char[toallocate];
    if(mem == nullptr)
    {
      return errc::not_enough_memory;
    }
    req.buffers._kernel_buffer = std::unique_ptr<char[]>(mem);
    req.buffers._kernel_buffer_size = toallocate;
  }
  FILE_ID_FULL_DIR_INFORMATION *buffer;
  ULONG bytes;
  bool done = false;
  do
  {
    buffer = req.kernelbuffer.empty() ? reinterpret_cast<FILE_ID_FULL_DIR_INFORMATION *>(req.buffers._kernel_buffer.get()) : reinterpret_cast<FILE_ID_FULL_DIR_INFORMATION *>(req.kernelbuffer.data());
    bytes = req.kernelbuffer.empty() ? static_cast<ULONG>(req.buffers._kernel_buffer_size) : static_cast<ULONG>(req.kernelbuffer.size());
    IO_STATUS_BLOCK isb = make_iostatus();
    NTSTATUS ntstat = NtQueryDirectoryFile(_v.h, nullptr, nullptr, nullptr, &isb, buffer, bytes, FileIdFullDirectoryInformation, FALSE, req.glob.empty() ? nullptr : &_glob, TRUE);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(_v.h, isb, deadline());
    }
    if(req.kernelbuffer.empty() && STATUS_BUFFER_OVERFLOW == ntstat)
    {
      req.buffers._kernel_buffer.reset();
      size_t toallocate = req.buffers._kernel_buffer_size * 2;
      auto *mem = new(std::nothrow) char[toallocate];
      if(mem == nullptr)
      {
        return errc::not_enough_memory;
      }
      req.buffers._kernel_buffer = std::unique_ptr<char[]>(mem);
      req.buffers._kernel_buffer_size = toallocate;
    }
    else
    {
      if(ntstat < 0)
      {
        return ntkernel_error(ntstat);
      }
      done = true;
    }
  } while(!done);
  size_t n = 0;
  done = false;
  for(FILE_ID_FULL_DIR_INFORMATION *ffdi = buffer; !done; ffdi = reinterpret_cast<FILE_ID_FULL_DIR_INFORMATION *>(reinterpret_cast<uintptr_t>(ffdi) + ffdi->NextEntryOffset))
  {
    size_t length = ffdi->FileNameLength / sizeof(wchar_t);
    done = (ffdi->NextEntryOffset == 0);
    if(length <= 2 && '.' == ffdi->FileName[0])
    {
      if(1 == length || '.' == ffdi->FileName[1])
      {
        continue;
      }
    }
    directory_entry &item = req.buffers[n];
    // Try to zero terminate leafnames where possible for later efficiency
    if(reinterpret_cast<uintptr_t>(ffdi->FileName + length) + sizeof(wchar_t) <= reinterpret_cast<uintptr_t>(ffdi) + ffdi->NextEntryOffset)
    {
      ffdi->FileName[length] = 0;
      item.leafname = path_view_type(ffdi->FileName, length, true);
    }
    else
    {
      item.leafname = path_view_type(ffdi->FileName, length, false);
    }
    if(req.filtering == filter::fastdeleted && item.leafname.is_llfio_deleted())
    {
      continue;
    }
    item.stat = stat_t(nullptr);
    item.stat.st_ino = ffdi->FileId.QuadPart;
    item.stat.st_type = to_st_type(ffdi->FileAttributes, ffdi->ReparsePointTag);
    item.stat.st_atim = to_timepoint(ffdi->LastAccessTime);
    item.stat.st_mtim = to_timepoint(ffdi->LastWriteTime);
    item.stat.st_ctim = to_timepoint(ffdi->ChangeTime);
    item.stat.st_size = ffdi->EndOfFile.QuadPart;
    item.stat.st_allocated = ffdi->AllocationSize.QuadPart;
    item.stat.st_birthtim = to_timepoint(ffdi->CreationTime);
    item.stat.st_sparse = static_cast<unsigned int>((ffdi->FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0u);
    item.stat.st_compressed = static_cast<unsigned int>((ffdi->FileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0u);
    item.stat.st_reparse_point = static_cast<unsigned int>((ffdi->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u);
    n++;
    if(!done && n >= req.buffers.size())
    {
      // Fill is incomplete
      req.buffers._metadata = default_stat_contents;
      req.buffers._done = false;
      return std::move(req.buffers);
    }
  }
  // Fill is complete
  req.buffers._resize(n);
  req.buffers._metadata = default_stat_contents;
  req.buffers._done = true;
  return std::move(req.buffers);
}

LLFIO_V2_NAMESPACE_END
