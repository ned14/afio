/* A handle to a symbolic link
(C) 2018 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
File Created: Jul 2018


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

#include "../../../symlink_handle.hpp"
#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

result<symlink_handle> symlink_handle::clone(mode mode_, deadline /*unused*/) const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  result<symlink_handle> ret(symlink_handle(native_handle_type(), _devid, _inode, _flags));
  OUTCOME_TRY(do_clone_handle(ret.value()._v, _v, mode_, caching::all, _flags));
  return ret;
}

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<symlink_handle> symlink_handle::symlink(const path_handle &base, symlink_handle::path_view_type path, symlink_handle::mode _mode, symlink_handle::creation _creation, flag flags) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  result<symlink_handle> ret(symlink_handle(native_handle_type(), 0, 0, flags));
  native_handle_type &nativeh = ret.value()._v;
  LLFIO_LOG_FUNCTION_CALL(&ret);
  nativeh.behaviour |= native_handle_type::disposition::symlink;
  DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  if(_mode == mode::append || _creation == creation::truncate_existing)
  {
    return errc::function_not_supported;
  }
  static int obtained_privilege;
  if(_creation != creation::open_existing)
  {
    if(0 == obtained_privilege)
    {
      // Attempt to enable SeCreateSymbolicLinkPrivilege
      obtained_privilege = 2;
      HANDLE token;
      if(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
      {
        TOKEN_PRIVILEGES privs = {1};
        if(LookupPrivilegeValueW(NULL, L"SeCreateSymbolicLinkPrivilege", &privs.Privileges[0].Luid))
        {
          privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
          if(AdjustTokenPrivileges(token, FALSE, &privs, 0, NULL, NULL) && GetLastError() == S_OK)
          {
            obtained_privilege = 1;
          }
        }
        CloseHandle(token);
      }
    }
    if(1 != obtained_privilege)
    {
      return errc::function_not_supported;
    }
  }
  OUTCOME_TRY(access, access_mask_from_handle_mode(nativeh, _mode, flags));
  OUTCOME_TRY(attribs, attributes_from_handle_caching_and_flags(nativeh, caching::all, flags));
  nativeh.behaviour &= ~native_handle_type::disposition::seekable;  // not seekable
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
    OUTCOME_TRY(ntflags, ntflags_from_handle_caching_and_flags(nativeh, caching::all, flags));
    ntflags |= 0x4000 /*FILE_OPEN_FOR_BACKUP_INTENT*/ | 0x00200000 /*FILE_OPEN_REPARSE_POINT*/;
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
    // required to open a symlink
    attribs |= FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
    path_view::c_str<> zpath(path, false);
    if(INVALID_HANDLE_VALUE == (nativeh.h = CreateFileW_(zpath.buffer, access, fileshare, nullptr, creation, attribs, nullptr)))  // NOLINT
    {
      DWORD errcode = GetLastError();
      // assert(false);
      return win32_error(errcode);
    }
  }
  return ret;
}

result<symlink_handle::buffers_type> symlink_handle::read(symlink_handle::io_request<symlink_handle::buffers_type> req) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  using windows_nt_kernel::REPARSE_DATA_BUFFER;
  symlink_handle::buffers_type tofill;
  if(req.kernelbuffer.empty())
  {
    // Let's assume the average symbolic link will be 256 characters long.
    size_t toallocate = (sizeof(FILE_ID_FULL_DIR_INFORMATION) + 256 * sizeof(wchar_t));
    auto *mem = new(std::nothrow) char[toallocate];
    if(mem == nullptr)
    {
      return errc::not_enough_memory;
    }
    tofill._kernel_buffer = std::unique_ptr<char[]>(mem);
    tofill._kernel_buffer_size = toallocate;
  }
  REPARSE_DATA_BUFFER *rpd;
  size_t bytes;
  for(;;)
  {
    rpd = req.kernelbuffer.empty() ? reinterpret_cast<REPARSE_DATA_BUFFER *>(tofill._kernel_buffer.get()) : reinterpret_cast<REPARSE_DATA_BUFFER *>(req.kernelbuffer.data());
    bytes = req.kernelbuffer.empty() ? static_cast<ULONG>(tofill._kernel_buffer_size) : static_cast<ULONG>(req.kernelbuffer.size());
    DWORD written = 0;
    if(!DeviceIoControl(_v.h, FSCTL_GET_REPARSE_POINT, NULL, 0, rpd, (DWORD) bytes, &written, NULL))
    {
      DWORD errcode = GetLastError();
      if(req.kernelbuffer.empty() && (errcode == ERROR_INSUFFICIENT_BUFFER || errcode == ERROR_MORE_DATA))
      {
        tofill._kernel_buffer.reset();
        size_t toallocate = tofill._kernel_buffer_size * 2;
        auto *mem = new(std::nothrow) char[toallocate];
        if(mem == nullptr)
        {
          return errc::not_enough_memory;
        }
        tofill._kernel_buffer = std::unique_ptr<char[]>(mem);
        tofill._kernel_buffer_size = toallocate;
        continue;
      }
      return win32_error(errcode);
    }
    switch(rpd->ReparseTag)
    {
    case IO_REPARSE_TAG_MOUNT_POINT:
      tofill._link = path_view(rpd->MountPointReparseBuffer.PathBuffer + rpd->MountPointReparseBuffer.SubstituteNameOffset / sizeof(rpd->MountPointReparseBuffer.PathBuffer[0]), rpd->MountPointReparseBuffer.SubstituteNameLength / sizeof(rpd->MountPointReparseBuffer.PathBuffer[0]), true);
      tofill._type = symlink_type::win_junction;
      return std::move(tofill);
    case IO_REPARSE_TAG_SYMLINK:
      tofill._link = path_view(rpd->SymbolicLinkReparseBuffer.PathBuffer + rpd->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(rpd->SymbolicLinkReparseBuffer.PathBuffer[0]), rpd->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(rpd->SymbolicLinkReparseBuffer.PathBuffer[0]), true);
      tofill._type = symlink_type::symbolic;
      return std::move(tofill);
    }
    return errc::protocol_not_supported;
  }
}

result<symlink_handle::const_buffers_type> symlink_handle::write(symlink_handle::io_request<symlink_handle::const_buffers_type> req, deadline /*unused*/) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_LOG_FUNCTION_CALL(this);
  using windows_nt_kernel::REPARSE_DATA_BUFFER;
  size_t destpathbytes = req.buffers.path().native_size() * sizeof(wchar_t);
  size_t buffersize = sizeof(REPARSE_DATA_BUFFER) + destpathbytes * 2 + 256;
  if(buffersize < req.kernelbuffer.size())
  {
    return errc::not_enough_memory;
  }
  const size_t headerlen = offsetof(REPARSE_DATA_BUFFER, MountPointReparseBuffer);
  auto *buffer = req.kernelbuffer.empty() ? alloca(buffersize) : req.kernelbuffer.data();
  memset(buffer, 0, sizeof(REPARSE_DATA_BUFFER));
  auto *rpd = (REPARSE_DATA_BUFFER *) buffer;
  path_view::c_str<> zpath(req.buffers.path(), true);
  switch(req.buffers.type())
  {
  case symlink_type::none:
    return errc::invalid_argument;
  case symlink_type::symbolic:
  {
    const size_t reparsebufferheaderlen = offsetof(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) - headerlen;
    rpd->ReparseTag = IO_REPARSE_TAG_SYMLINK;
    if(zpath.length >= 4 && zpath.buffer[0] == '\\' && zpath.buffer[1] == '!' && zpath.buffer[2] == '!' && zpath.buffer[3] == '\\')
    {
      memcpy(rpd->SymbolicLinkReparseBuffer.PathBuffer, zpath.buffer + 3, destpathbytes - 6 + sizeof(wchar_t));
      rpd->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
      rpd->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT) destpathbytes - 6;
      rpd->SymbolicLinkReparseBuffer.PrintNameOffset = (USHORT)(destpathbytes - 6 + sizeof(wchar_t));
      rpd->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT) destpathbytes - 6;
      memcpy(rpd->SymbolicLinkReparseBuffer.PathBuffer + rpd->SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(wchar_t), zpath.buffer + 3, rpd->SymbolicLinkReparseBuffer.PrintNameLength - 6 + sizeof(wchar_t));
    }
    else
    {
      memcpy(rpd->SymbolicLinkReparseBuffer.PathBuffer, zpath.buffer, destpathbytes + sizeof(wchar_t));
      rpd->SymbolicLinkReparseBuffer.SubstituteNameOffset = 0;
      rpd->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT) destpathbytes;
      rpd->SymbolicLinkReparseBuffer.PrintNameOffset = (USHORT)(destpathbytes + sizeof(wchar_t));
      rpd->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT) destpathbytes;
      memcpy(rpd->SymbolicLinkReparseBuffer.PathBuffer + rpd->SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(wchar_t), zpath.buffer, rpd->SymbolicLinkReparseBuffer.PrintNameLength + sizeof(wchar_t));
    }
    rpd->SymbolicLinkReparseBuffer.Flags = req.buffers.path().is_relative() ? 0x1 /*SYMLINK_FLAG_RELATIVE*/ : 0;
    rpd->ReparseDataLength = (USHORT)(rpd->SymbolicLinkReparseBuffer.SubstituteNameLength + rpd->SymbolicLinkReparseBuffer.PrintNameLength + 2 * sizeof(wchar_t) + reparsebufferheaderlen);
    break;
  }
  case symlink_type::win_wsl:
    // TODO FIXME
    abort();
  case symlink_type::win_junction:
  {
    const size_t reparsebufferheaderlen = offsetof(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) - headerlen;
    rpd->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    if(zpath.length >= 4 && zpath.buffer[0] == '\\' && zpath.buffer[1] == '!' && zpath.buffer[2] == '!' && zpath.buffer[3] == '\\')
    {
      memcpy(rpd->MountPointReparseBuffer.PathBuffer, zpath.buffer + 3, destpathbytes - 6 + sizeof(wchar_t));
      rpd->MountPointReparseBuffer.SubstituteNameOffset = 0;
      rpd->MountPointReparseBuffer.SubstituteNameLength = (USHORT) destpathbytes - 6;
      rpd->MountPointReparseBuffer.PrintNameOffset = (USHORT)(destpathbytes - 6 + sizeof(wchar_t));
      rpd->MountPointReparseBuffer.PrintNameLength = (USHORT) destpathbytes - 6;
      memcpy(rpd->MountPointReparseBuffer.PathBuffer + rpd->MountPointReparseBuffer.PrintNameOffset / sizeof(wchar_t), zpath.buffer + 3, rpd->MountPointReparseBuffer.PrintNameLength - 6 + sizeof(wchar_t));
    }
    else
    {
      memcpy(rpd->MountPointReparseBuffer.PathBuffer, zpath.buffer, destpathbytes + sizeof(wchar_t));
      rpd->MountPointReparseBuffer.SubstituteNameOffset = 0;
      rpd->MountPointReparseBuffer.SubstituteNameLength = (USHORT) destpathbytes;
      rpd->MountPointReparseBuffer.PrintNameOffset = (USHORT)(destpathbytes + sizeof(wchar_t));
      rpd->MountPointReparseBuffer.PrintNameLength = (USHORT) destpathbytes;
      memcpy(rpd->MountPointReparseBuffer.PathBuffer + rpd->MountPointReparseBuffer.PrintNameOffset / sizeof(wchar_t), zpath.buffer, rpd->MountPointReparseBuffer.PrintNameLength + sizeof(wchar_t));
    }
    rpd->ReparseDataLength = (USHORT)(rpd->MountPointReparseBuffer.SubstituteNameLength + rpd->MountPointReparseBuffer.PrintNameLength + 2 * sizeof(wchar_t) + reparsebufferheaderlen);
    break;
  }
  }
  DWORD bytesout = 0;
  if(DeviceIoControl(_v.h, FSCTL_SET_REPARSE_POINT, rpd, (DWORD)(rpd->ReparseDataLength + headerlen), NULL, 0, &bytesout, NULL) == 0)
  {
    return win32_error();
  }
  return success(std::move(req.buffers));
}

LLFIO_V2_NAMESPACE_END
