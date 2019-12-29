/* Information about the volume storing a file
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (6 commits)
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
#include "../../../statfs.hpp"
#include "import.hpp"

LLFIO_V2_NAMESPACE_BEGIN

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<size_t> statfs_t::fill(const handle &h, statfs_t::want wanted) noexcept
{
  LLFIO_LOG_FUNCTION_CALL(&h);
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  alignas(8) wchar_t buffer[32769];
  IO_STATUS_BLOCK isb = make_iostatus();
  NTSTATUS ntstat;
  size_t ret = 0;
  if((wanted & want::flags) || (wanted & want::namemax) || (wanted & want::fstypename))
  {
    auto *ffai = reinterpret_cast<FILE_FS_ATTRIBUTE_INFORMATION *>(buffer);
    isb.Status = -1;
    ntstat = NtQueryVolumeInformationFile(h.native_handle().h, &isb, ffai, sizeof(buffer), FileFsAttributeInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    }
    if(ntstat != 0)
    {
      return ntkernel_error(ntstat);
    }
    if(wanted & want::flags)
    {
      f_flags.rdonly = static_cast<uint32_t>((ffai->FileSystemAttributes & FILE_READ_ONLY_VOLUME) != 0u);
      f_flags.acls = static_cast<uint32_t>((ffai->FileSystemAttributes & FILE_PERSISTENT_ACLS) != 0u);
      f_flags.xattr = static_cast<uint32_t>((ffai->FileSystemAttributes & FILE_NAMED_STREAMS) != 0u);
      f_flags.compression = static_cast<uint32_t>((ffai->FileSystemAttributes & FILE_VOLUME_IS_COMPRESSED) != 0u);
      f_flags.extents = static_cast<uint32_t>((ffai->FileSystemAttributes & FILE_SUPPORTS_SPARSE_FILES) != 0u);
      f_flags.filecompression = static_cast<uint32_t>((ffai->FileSystemAttributes & FILE_FILE_COMPRESSION) != 0u);
      ++ret;
    }
    if(wanted & want::namemax)
    {
      f_namemax = ffai->MaximumComponentNameLength;
      ++ret;
    }
    if(wanted & want::fstypename)
    {
      f_fstypename.resize(ffai->FileSystemNameLength / sizeof(wchar_t));
      for(size_t n = 0; n < ffai->FileSystemNameLength / sizeof(wchar_t); n++)
      {
        f_fstypename[n] = static_cast<char>(ffai->FileSystemName[n]);
      }
      ++ret;
    }
  }
  if((wanted & want::bsize) || (wanted & want::blocks) || (wanted & want::bfree) || (wanted & want::bavail))
  {
    auto *fffsi = reinterpret_cast<FILE_FS_FULL_SIZE_INFORMATION *>(buffer);
    isb.Status = -1;
    ntstat = NtQueryVolumeInformationFile(h.native_handle().h, &isb, fffsi, sizeof(buffer), FileFsFullSizeInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    }
    if(ntstat != 0)
    {
      return ntkernel_error(ntstat);
    }
    if(wanted & want::bsize)
    {
      f_bsize = fffsi->BytesPerSector * fffsi->SectorsPerAllocationUnit;
      ++ret;
    }
    if(wanted & want::blocks)
    {
      f_blocks = fffsi->TotalAllocationUnits.QuadPart;
      ++ret;
    }
    if(wanted & want::bfree)
    {
      f_bfree = fffsi->ActualAvailableAllocationUnits.QuadPart;
      ++ret;
    }
    if(wanted & want::bavail)
    {
      f_bavail = fffsi->CallerAvailableAllocationUnits.QuadPart;
      ++ret;
    }
  }
  if(wanted & want::fsid)
  {
    auto *ffoi = reinterpret_cast<FILE_FS_OBJECTID_INFORMATION *>(buffer);
    isb.Status = -1;
    ntstat = NtQueryVolumeInformationFile(h.native_handle().h, &isb, ffoi, sizeof(buffer), FileFsObjectIdInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    }
    if(0 /*STATUS_SUCCESS*/ == ntstat)
    {
      // FAT32 doesn't support filing system id, so sink error
      memcpy(&f_fsid, ffoi->ObjectId, sizeof(f_fsid));
      ++ret;
    }
  }
  if(wanted & want::iosize)
  {
    auto *ffssi = reinterpret_cast<FILE_FS_SECTOR_SIZE_INFORMATION *>(buffer);
    isb.Status = -1;
    ntstat = NtQueryVolumeInformationFile(h.native_handle().h, &isb, ffssi, sizeof(buffer), FileFsSectorSizeInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    }
    if(ntstat != 0)
    {
      return ntkernel_error(ntstat);
    }
    f_iosize = ffssi->PhysicalBytesPerSectorForPerformance;
    ++ret;
  }
  if((wanted & want::mntfromname) || (wanted & want::mntonname))
  {
    // Irrespective we need the path before figuring out the mounted device
    alignas(8) wchar_t buffer2[32769];
    for(;;)
    {
      DWORD pathlen = GetFinalPathNameByHandleW(h.native_handle().h, buffer2, sizeof(buffer2) / sizeof(*buffer2), FILE_NAME_OPENED | VOLUME_NAME_NONE);
      if((pathlen == 0u) || pathlen >= sizeof(buffer2) / sizeof(*buffer2))
      {
        return win32_error();
      }
      buffer2[pathlen] = 0;
      if(wanted & want::mntfromname)
      {
        DWORD len = GetFinalPathNameByHandleW(h.native_handle().h, buffer, sizeof(buffer) / sizeof(*buffer), FILE_NAME_OPENED | VOLUME_NAME_NT);
        if((len == 0u) || len >= sizeof(buffer) / sizeof(*buffer))
        {
          return win32_error();
        }
        buffer[len] = 0;
        if(memcmp(buffer2, buffer + len - pathlen, pathlen) != 0)
        {
          continue;  // path changed
        }
        len -= pathlen;
        // buffer should look like \Device\HarddiskVolumeX
        if(memcmp(buffer, L"\\Device\\HarddiskVolume", 44) != 0)
        {
          return errc::illegal_byte_sequence;
        }
        f_mntfromname.reserve(len + 3);
        f_mntfromname.assign("\\!!");  // escape prefix for NT kernel path
        for(size_t n = 0; n < len; n++)
        {
          f_mntfromname.push_back(static_cast<char>(buffer[n]));
        }
        ++ret;
        wanted &= ~want::mntfromname;
      }
      if(wanted & want::mntonname)
      {
        DWORD len = GetFinalPathNameByHandleW(h.native_handle().h, buffer, sizeof(buffer) / sizeof(*buffer), FILE_NAME_OPENED | VOLUME_NAME_DOS);
        if((len == 0u) || len >= sizeof(buffer) / sizeof(*buffer))
        {
          return win32_error();
        }
        buffer[len] = 0;
        if(memcmp(buffer2, buffer + len - pathlen, pathlen) != 0)
        {
          continue;  // path changed
        }
        len -= pathlen;
        f_mntonname = decltype(f_mntonname)::string_type(buffer, len);
        ++ret;
      }
      break;
    }
  }
  return ret;
}

LLFIO_V2_NAMESPACE_END
