/* A handle to something
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (11 commits)
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

#include "../../../handle.hpp"
#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

handle::~handle()
{
  if(_v)
  {
    // Call close() below
    auto ret = handle::close();
    if(ret.has_error())
    {
      LLFIO_LOG_FATAL(_v.h, "handle::~handle() close failed");
      abort();
    }
  }
}

result<handle::path_type> handle::current_path() const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  try
  {
    // Most efficient, least memory copying method is direct fill of a wstring which is moved into filesystem::path
    filesystem::path::string_type buffer;
    buffer.resize(32769);
    auto *_buffer = const_cast<wchar_t *>(buffer.data());
    memcpy(_buffer, L"\\!!", 6);
    DWORD len = GetFinalPathNameByHandleW(_v.h, _buffer + 3, (DWORD)(buffer.size() - 4 * sizeof(wchar_t)), VOLUME_NAME_NT);  // NOLINT
    if(len == 0)
    {
      return win32_error();
    }
    buffer.resize(3 + len);
    // As of Windows 10 1709, there are such things as actually unlinked files, so detect those
    if(filesystem::path::string_type::npos != buffer.find(L"\\$Extend\\$Deleted\\"))
    {
      return path_type();
    }
    return path_type(buffer);
  }
  catch(...)
  {
    return error_from_exception();
  }
}

result<void> handle::close() noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  if(_v)
  {
#ifndef NDEBUG
    // Trap when refined handle implementations don't set their vptr properly (this took a while to debug!)
    if((static_cast<unsigned>(_v.behaviour) & 0xff00) != 0 && !(_v.behaviour & native_handle_type::disposition::_child_close_executed))
    {
      LLFIO_LOG_FATAL(this, "handle::close() called on a derived handle implementation, this suggests vptr is incorrect");
      abort();
    }
#endif
    if(are_safety_barriers_issued() && is_writable())
    {
      if(FlushFileBuffers(_v.h) == 0)
      {
        return win32_error();
      }
    }
    if(CloseHandle(_v.h) == 0)
    {
      return win32_error();
    }
    _v = native_handle_type();
  }
  return success();
}

result<handle> handle::clone() const noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  result<handle> ret(handle(native_handle_type(), _caching, _flags));
  ret.value()._v.behaviour = _v.behaviour;
  if(DuplicateHandle(GetCurrentProcess(), _v.h, GetCurrentProcess(), &ret.value()._v.h, 0, 0, DUPLICATE_SAME_ACCESS) == 0)
  {
    return win32_error();
  }
  return ret;
}

result<void> handle::set_append_only(bool enable) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(this);
  // This works only due to special handling in OVERLAPPED later
  if(enable)
  {
    // Set append_only
    _v.behaviour |= native_handle_type::disposition::append_only;
  }
  else
  {
    // Remove append_only
    _v.behaviour &= ~native_handle_type::disposition::append_only;
  }
  return success();
}

LLFIO_V2_NAMESPACE_END
