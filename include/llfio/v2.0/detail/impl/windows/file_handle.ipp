/* A handle to a file
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (16 commits)
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
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  result<file_handle> ret(file_handle(native_handle_type(), 0, 0, _caching, flags));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::file;
  DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  OUTCOME_TRY(access, access_mask_from_handle_mode(nativeh, _mode, flags));
  OUTCOME_TRY(attribs, attributes_from_handle_caching_and_flags(nativeh, _caching, flags));
  bool need_to_set_sparse = false;
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
      creatdisp = 0x00000000 /*FILE_SUPERSEDE*/;
      break;
    }

    attribs &= 0x00ffffff;  // the real attributes only, not the win32 flags
    OUTCOME_TRY(ntflags, ntflags_from_handle_caching_and_flags(nativeh, _caching, flags));
    ntflags |= 0x040 /*FILE_NON_DIRECTORY_FILE*/;  // do not open a directory
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
      return ntkernel_error(ntstat);
    }
    switch(_creation)
    {
    case creation::open_existing:
      need_to_set_sparse = false;
      break;
    case creation::only_if_not_exist:
      need_to_set_sparse = true;
      break;
    case creation::if_needed:
      need_to_set_sparse = (isb.Information == 2 /*FILE_CREATED*/);
      break;
    case creation::truncate_existing:
    case creation::always_new:
      need_to_set_sparse = true;
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
      creation = CREATE_ALWAYS;
      break;
    }
    path_view::c_str<> zpath(path, false);
    if(INVALID_HANDLE_VALUE == (nativeh.h = CreateFileW_(zpath.buffer, access, fileshare, nullptr, creation, attribs, nullptr)))  // NOLINT
    {
      DWORD errcode = GetLastError();
      // assert(false);
      return win32_error(errcode);
    }
    switch(_creation)
    {
    case creation::open_existing:
      need_to_set_sparse = false;
      break;
    case creation::only_if_not_exist:
      need_to_set_sparse = true;
      break;
    case creation::if_needed:
      need_to_set_sparse = (GetLastError() != ERROR_ALREADY_EXISTS);  // new inode created
      break;
    case creation::truncate_existing:
    case creation::always_new:
      need_to_set_sparse = true;
      break;
    }
  }
  if(need_to_set_sparse && !(flags & flag::win_disable_sparse_file_creation))
  {
    DWORD bytesout = 0;
    FILE_SET_SPARSE_BUFFER fssb = {1u};
    if(DeviceIoControl(nativeh.h, FSCTL_SET_SPARSE, &fssb, sizeof(fssb), nullptr, 0, &bytesout, nullptr) == 0)
    {
#if LLFIO_LOGGING_LEVEL >= 3
      DWORD errcode = GetLastError();
      LLFIO_LOG_WARN(&ret, "Failed to set file to sparse");
      result<void> r = win32_error(errcode);
      (void) r;  // throw away
#endif
    }
  }
  if(_creation == creation::truncate_existing && ret.value().are_safety_barriers_issued())
  {
    FlushFileBuffers(nativeh.h);
  }
  return ret;
}

result<file_handle> file_handle::temp_inode(const path_handle &dirh, mode _mode, flag flags) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  caching _caching = caching::temporary;
  // No need to rename to random on unlink or check inode before unlink
  flags |= flag::disable_safety_unlinks | flag::win_disable_unlink_emulation;
  result<file_handle> ret(file_handle(native_handle_type(), 0, 0, _caching, flags));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::file;
  DWORD fileshare = /* no read nor write access for others */ FILE_SHARE_DELETE;
  OUTCOME_TRY(access, access_mask_from_handle_mode(nativeh, _mode, flags));
  OUTCOME_TRY(attribs, attributes_from_handle_caching_and_flags(nativeh, _caching, flags));
  DWORD creatdisp = 0x00000002 /*FILE_CREATE*/;

  attribs &= 0x00ffffff;  // the real attributes only, not the win32 flags
  OUTCOME_TRY(ntflags, ntflags_from_handle_caching_and_flags(nativeh, _caching, flags));
  ntflags |= 0x040 /*FILE_NON_DIRECTORY_FILE*/;  // do not open a directory
  UNICODE_STRING _path{};
  _path.MaximumLength = (_path.Length = static_cast<USHORT>(68 * sizeof(wchar_t))) + sizeof(wchar_t);

  OBJECT_ATTRIBUTES oa{};
  memset(&oa, 0, sizeof(oa));
  oa.Length = sizeof(OBJECT_ATTRIBUTES);
  oa.ObjectName = &_path;
  oa.RootDirectory = dirh.native_handle().h;

  LARGE_INTEGER AllocationSize{};
  memset(&AllocationSize, 0, sizeof(AllocationSize));
  std::string _random;
  std::wstring random(68, 0);
  for(;;)
  {
    try
    {
      _random = utils::random_string(32) + ".tmp";
      for(size_t n = 0; n < _random.size(); n++)
      {
        random[n] = _random[n];
      }
    }
    catch(...)
    {
      return error_from_exception();
    }
    _path.Buffer = const_cast<wchar_t *>(random.c_str());
    {
      IO_STATUS_BLOCK isb = make_iostatus();
      NTSTATUS ntstat = NtCreateFile(&nativeh.h, access, &oa, &isb, &AllocationSize, attribs, fileshare, creatdisp, ntflags, nullptr, 0);
      if(STATUS_PENDING == ntstat)
      {
        ntstat = ntwait(nativeh.h, isb, deadline());
      }
      if(ntstat < 0 && ntstat != static_cast<NTSTATUS>(0xC0000035L) /*STATUS_OBJECT_NAME_COLLISION*/)
      {
        return ntkernel_error(ntstat);
      }
    }
    // std::cerr << random << std::endl;
    if(nativeh.h != nullptr)
    {
      HANDLE duph;
      {
        // It is entirely undocumented that this is how you clone a file handle with new privs
        memset(&_path, 0, sizeof(_path));
        oa.ObjectName = &_path;
        oa.RootDirectory = nativeh.h;
        IO_STATUS_BLOCK isb = make_iostatus();
        NTSTATUS ntstat = NtOpenFile(&duph, SYNCHRONIZE | DELETE, &oa, &isb, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0x20 /*FILE_SYNCHRONOUS_IO_NONALERT*/);
        if(ntstat < 0)
        {
          return ntkernel_error(ntstat);
        }
      }
      auto unduph = undoer([&duph] { CloseHandle(duph); });
      (void) unduph;
      bool failed = true;
      // Immediately delete, try by POSIX delete first
      {
        IO_STATUS_BLOCK isb = make_iostatus();
        FILE_DISPOSITION_INFORMATION_EX fdie{};
        memset(&fdie, 0, sizeof(fdie));
        fdie.Flags = 0x1 /*FILE_DISPOSITION_DELETE*/ | 0x2 /*FILE_DISPOSITION_POSIX_SEMANTICS*/;
        NTSTATUS ntstat = NtSetInformationFile(duph, &isb, &fdie, sizeof(fdie), FileDispositionInformationEx);
        if(ntstat >= 0)
        {
          failed = false;
        }
      }
      if(failed)
      {
        // Hide this item
        IO_STATUS_BLOCK isb = make_iostatus();
        FILE_BASIC_INFORMATION fbi{};
        memset(&fbi, 0, sizeof(fbi));
        fbi.FileAttributes = FILE_ATTRIBUTE_HIDDEN;
        NtSetInformationFile(nativeh.h, &isb, &fbi, sizeof(fbi), FileBasicInformation);

        // Mark the item as delete on close
        isb = make_iostatus();
        FILE_DISPOSITION_INFORMATION fdi{};
        memset(&fdi, 0, sizeof(fdi));
        fdi._DeleteFile = 1u;
        NTSTATUS ntstat = NtSetInformationFile(duph, &isb, &fdi, sizeof(fdi), FileDispositionInformation);
        if(ntstat >= 0)
        {
          // No need to delete it again on close
          ret.value()._flags &= ~flag::unlink_on_first_close;
        }
      }
    }
    return ret;
  }
}

result<file_handle> file_handle::clone(mode mode_, caching caching_, deadline /*unused*/) const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  result<file_handle> ret(file_handle(native_handle_type(), _devid, _inode, caching_, _flags));
  OUTCOME_TRY(do_clone_handle(ret.value()._v, _v, mode_, caching_, _flags));
  return ret;
}

result<file_handle::extent_type> file_handle::maximum_extent() const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
#if 0
  char buffer[1];
  DWORD read = 0;
  OVERLAPPED ol;
  memset(&ol, 0, sizeof(ol));
  ol.OffsetHigh = ol.Offset = 0xffffffff;
  WriteFile(_v.h, buffer, 0, &read, &ol);
#endif
  FILE_STANDARD_INFO fsi{};
  if(GetFileInformationByHandleEx(_v.h, FileStandardInfo, &fsi, sizeof(fsi)) == 0)
  {
    return win32_error();
  }
  return fsi.EndOfFile.QuadPart;
}

result<file_handle::extent_type> file_handle::truncate(file_handle::extent_type newsize) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  FILE_END_OF_FILE_INFO feofi{};
  feofi.EndOfFile.QuadPart = newsize;
  if(SetFileInformationByHandle(_v.h, FileEndOfFileInfo, &feofi, sizeof(feofi)) == 0)
  {
    return win32_error();
  }
  if(are_safety_barriers_issued())
  {
    FlushFileBuffers(_v.h);
  }
  return newsize;
}

result<std::vector<std::pair<file_handle::extent_type, file_handle::extent_type>>> file_handle::extents() const noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  try
  {
    static_assert(sizeof(std::pair<file_handle::extent_type, file_handle::extent_type>) == sizeof(FILE_ALLOCATED_RANGE_BUFFER), "FILE_ALLOCATED_RANGE_BUFFER is not equivalent to pair<extent_type, extent_type>!");
    std::vector<std::pair<file_handle::extent_type, file_handle::extent_type>> ret;
#ifdef NDEBUG
    ret.resize(64);
#else
    ret.resize(1);
#endif
    FILE_ALLOCATED_RANGE_BUFFER farb{};
    farb.FileOffset.QuadPart = 0;
    farb.Length.QuadPart = (static_cast<extent_type>(1) << 63) - 1;  // Microsoft claims this is 1<<64-1024 for NTFS, but I get bad parameter error with anything higher than 1<<63-1.
    DWORD bytesout = 0;
    OVERLAPPED ol{};
    memset(&ol, 0, sizeof(ol));
    ol.Internal = static_cast<ULONG_PTR>(-1);
    while(DeviceIoControl(_v.h, FSCTL_QUERY_ALLOCATED_RANGES, &farb, sizeof(farb), ret.data(), static_cast<DWORD>(ret.size() * sizeof(FILE_ALLOCATED_RANGE_BUFFER)), &bytesout, &ol) == 0)
    {
      if(ERROR_INSUFFICIENT_BUFFER == GetLastError() || ERROR_MORE_DATA == GetLastError())
      {
        ret.resize(ret.size() * 2);
        continue;
      }
      if(ERROR_SUCCESS != GetLastError())
      {
        return win32_error();
      }
    }
    return ret;
  }
  catch(...)
  {
    return error_from_exception();
  }
}

result<file_handle::extent_type> file_handle::zero(file_handle::extent_type offset, file_handle::extent_type bytes, deadline /*unused*/) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  if(offset + bytes < offset)
  {
    return errc::value_too_large;
  }
  FILE_ZERO_DATA_INFORMATION fzdi{};
  fzdi.FileOffset.QuadPart = offset;
  fzdi.BeyondFinalZero.QuadPart = offset + bytes;
  DWORD bytesout = 0;
  OVERLAPPED ol{};
  memset(&ol, 0, sizeof(ol));
  ol.Internal = static_cast<ULONG_PTR>(-1);
  if(DeviceIoControl(_v.h, FSCTL_SET_ZERO_DATA, &fzdi, sizeof(fzdi), nullptr, 0, &bytesout, &ol) == 0)
  {
    if(ERROR_IO_PENDING == GetLastError())
    {
      NTSTATUS ntstat = ntwait(_v.h, ol, deadline());
      if(ntstat != 0)
      {
        return ntkernel_error(ntstat);
      }
    }
    if(ERROR_SUCCESS != GetLastError())
    {
      return win32_error();
    }
  }
  return success();
}

LLFIO_V2_NAMESPACE_END
