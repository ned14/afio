/* A profile of an OS and filing system
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (5 commits)
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
#include "../../../storage_profile.hpp"
#include "import.hpp"

#include <winioctl.h>
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
#include <intrin.h>  // for __cpuid
#endif

LLFIO_V2_NAMESPACE_BEGIN

namespace storage_profile
{
  namespace system
  {
// OS name, version
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6387)  // MSVC sanitiser warns that GetModuleHandleA() might fail (hah!)
#endif
    outcome<void> os(storage_profile &sp, file_handle & /*unused*/) noexcept
    {
      static std::string os_name, os_ver;
      if(!os_name.empty())
      {
        sp.os_name.value = os_name;
        sp.os_ver.value = os_ver;
      }
      else
      {
        try
        {
          using std::to_string;
          RTL_OSVERSIONINFOW ovi{};
          memset(&ovi, 0, sizeof(ovi));
          ovi.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
          // GetVersionEx() is no longer useful since Win8.1
          using RtlGetVersion_t = LONG (*)(PRTL_OSVERSIONINFOW);
          static RtlGetVersion_t RtlGetVersion;
          if(RtlGetVersion == nullptr)
          {
            RtlGetVersion = reinterpret_cast<RtlGetVersion_t>(GetProcAddress(GetModuleHandleW(L"NTDLL.DLL"), "RtlGetVersion"));
          }
          if(RtlGetVersion == nullptr)
          {
            return win32_error();
          }
          RtlGetVersion(&ovi);
          sp.os_name.value = "Microsoft Windows ";
          sp.os_name.value.append(ovi.dwPlatformId == VER_PLATFORM_WIN32_NT ? "NT" : "Unknown");
          sp.os_ver.value.append(to_string(ovi.dwMajorVersion) + "." + to_string(ovi.dwMinorVersion) + "." + to_string(ovi.dwBuildNumber));
          os_name = sp.os_name.value;
          os_ver = sp.os_ver.value;
        }
        catch(...)
        {
          return std::current_exception();
        }
      }
      return success();
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    // CPU name, architecture, physical cores
    outcome<void> cpu(storage_profile &sp, file_handle & /*unused*/) noexcept
    {
      static std::string cpu_name, cpu_architecture;
      static unsigned cpu_physical_cores;
      if(!cpu_name.empty())
      {
        sp.cpu_name.value = cpu_name;
        sp.cpu_architecture.value = cpu_architecture;
        sp.cpu_physical_cores.value = cpu_physical_cores;
      }
      else
      {
        try
        {
          SYSTEM_INFO si{};
          memset(&si, 0, sizeof(si));
          GetNativeSystemInfo(&si);
          switch(si.wProcessorArchitecture)
          {
          case PROCESSOR_ARCHITECTURE_AMD64:
            sp.cpu_name.value = sp.cpu_architecture.value = "x64";
            break;
          case PROCESSOR_ARCHITECTURE_ARM:
            sp.cpu_name.value = sp.cpu_architecture.value = "ARM";
            break;
          case PROCESSOR_ARCHITECTURE_IA64:
            sp.cpu_name.value = sp.cpu_architecture.value = "IA64";
            break;
          case PROCESSOR_ARCHITECTURE_INTEL:
            sp.cpu_name.value = sp.cpu_architecture.value = "x86";
            break;
          default:
            sp.cpu_name.value = sp.cpu_architecture.value = "unknown";
            break;
          }
          {
            DWORD size = 0;

            GetLogicalProcessorInformation(nullptr, &size);
            if(ERROR_INSUFFICIENT_BUFFER != GetLastError())
            {
              return win32_error();
            }

            std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(size);
            if(GetLogicalProcessorInformation(&buffer.front(), &size) == FALSE)
            {
              return win32_error();
            }

            const size_t Elements = size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

            sp.cpu_physical_cores.value = 0;
            for(size_t i = 0; i < Elements; ++i)
            {
              if(buffer[i].Relationship == RelationProcessorCore)
              {
                ++sp.cpu_physical_cores.value;
              }
            }
          }
#if defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
// We can do a much better CPU name on x86/x64
#if defined(__clang__)
          auto __cpuid = [](int *cpuInfo, int func) { __asm__ __volatile__("cpuid\n\t" : "=a"(cpuInfo[0]), "=b"(cpuInfo[1]), "=c"(cpuInfo[2]), "=d"(cpuInfo[3]) : "0"(func)); };
#endif
          sp.cpu_name.value.clear();
          {
            char buffer[62];
            memset(buffer, 32, 62);
            int nBuff[4];
            __cpuid(nBuff, 0);
            *reinterpret_cast<int *>(&buffer[0]) = nBuff[1];
            *reinterpret_cast<int *>(&buffer[4]) = nBuff[3];
            *reinterpret_cast<int *>(&buffer[8]) = nBuff[2];

            // Do we have a brand string?
            __cpuid(nBuff, 0x80000000);
            if(static_cast<unsigned>(nBuff[0]) >= 0x80000004)
            {
              __cpuid(reinterpret_cast<int *>(&buffer[14]), 0x80000002);
              __cpuid(reinterpret_cast<int *>(&buffer[30]), 0x80000003);
              __cpuid(reinterpret_cast<int *>(&buffer[46]), 0x80000004);
            }
            else
            {
              memcpy(&buffer[14], "unbranded", 10);
            }

            // Trim string
            for(size_t n = 0; n < 62; n++)
            {
              if((n == 0u) || buffer[n] != 32 || buffer[n - 1] != 32)
              {
                if(buffer[n] != 0)
                {
                  sp.cpu_name.value.push_back(buffer[n]);
                }
              }
            }
          }
#endif
          cpu_name = sp.cpu_name.value;
          cpu_architecture = sp.cpu_architecture.value;
          cpu_physical_cores = sp.cpu_physical_cores.value;
        }
        catch(...)
        {
          return std::current_exception();
        }
      }
      return success();
    }
    namespace windows
    {
      outcome<void> _mem(storage_profile &sp, file_handle & /*unused*/) noexcept
      {
        MEMORYSTATUSEX ms{};
        memset(&ms, 0, sizeof(ms));
        ms.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&ms);
        sp.mem_quantity.value = static_cast<unsigned long long>(ms.ullTotalPhys);
        sp.mem_in_use.value = static_cast<float>(ms.ullTotalPhys - ms.ullAvailPhys) / ms.ullTotalPhys;
        return success();
      }
    }  // namespace windows
  }    // namespace system
  namespace storage
  {
    namespace windows
    {
      // Controller type, max transfer, max buffers. Device name, size
      outcome<void> _device(storage_profile &sp, file_handle & /*unused*/, const std::string &_mntfromname, const std::string & /*fstypename*/) noexcept
      {
        try
        {
          alignas(8) wchar_t buffer[32769];
          // Firstly open a handle to the volume
          OUTCOME_TRY(volumeh, file_handle::file({}, _mntfromname, handle::mode::none, handle::creation::open_existing, handle::caching::only_metadata));
          STORAGE_PROPERTY_QUERY spq{};
          memset(&spq, 0, sizeof(spq));
          spq.PropertyId = StorageAdapterProperty;
          spq.QueryType = PropertyStandardQuery;
          auto *sad = reinterpret_cast<STORAGE_ADAPTER_DESCRIPTOR *>(buffer);
          OVERLAPPED ol{};
          memset(&ol, 0, sizeof(ol));
          ol.Internal = static_cast<ULONG_PTR>(-1);
          if(DeviceIoControl(volumeh.native_handle().h, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), sad, sizeof(buffer), nullptr, &ol) == 0)
          {
            if(ERROR_IO_PENDING == GetLastError())
            {
              NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol, deadline());
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
          switch(sad->BusType)
          {
          case BusTypeScsi:
            sp.controller_type.value = "SCSI";
            break;
          case BusTypeAtapi:
            sp.controller_type.value = "ATAPI";
            break;
          case BusTypeAta:
            sp.controller_type.value = "ATA";
            break;
          case BusType1394:
            sp.controller_type.value = "1394";
            break;
          case BusTypeSsa:
            sp.controller_type.value = "SSA";
            break;
          case BusTypeFibre:
            sp.controller_type.value = "Fibre";
            break;
          case BusTypeUsb:
            sp.controller_type.value = "USB";
            break;
          case BusTypeRAID:
            sp.controller_type.value = "RAID";
            break;
          case BusTypeiScsi:
            sp.controller_type.value = "iSCSI";
            break;
          case BusTypeSas:
            sp.controller_type.value = "SAS";
            break;
          case BusTypeSata:
            sp.controller_type.value = "SATA";
            break;
          case BusTypeSd:
            sp.controller_type.value = "SD";
            break;
          case BusTypeMmc:
            sp.controller_type.value = "MMC";
            break;
          case BusTypeVirtual:
            sp.controller_type.value = "Virtual";
            break;
          case BusTypeFileBackedVirtual:
            sp.controller_type.value = "File Backed Virtual";
            break;
          case BusTypeSpaces:
            sp.controller_type.value = "Storage Spaces";
            break;
          case BusTypeNvme:
            sp.controller_type.value = "NVMe";
            break;
          case BusTypeSCM:  // NOTE: If this is not found, update your Windows SDK to something recent!!!
            sp.controller_type.value = "Storage Class Memory";
            break;
          default:
            sp.controller_type.value = "unknown";
            break;
          }
          sp.controller_max_transfer.value = sad->MaximumTransferLength;
          sp.controller_max_buffers.value = sad->MaximumPhysicalPages;

          // Now ask the volume what physical disks it spans
          auto *vde = reinterpret_cast<VOLUME_DISK_EXTENTS *>(buffer);
          ol.Internal = static_cast<ULONG_PTR>(-1);
          if(DeviceIoControl(volumeh.native_handle().h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, vde, sizeof(buffer), nullptr, &ol) == 0)
          {
            if(ERROR_IO_PENDING == GetLastError())
            {
              NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol, deadline());
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
          DWORD disk_extents = vde->NumberOfDiskExtents;
          sp.device_name.value.clear();
          if(disk_extents > 0)
          {
            // For now we only care about the first physical device
            alignas(8) wchar_t physicaldrivename[32769] = L"\\\\.\\PhysicalDrive", *e;
            for(e = physicaldrivename; *e != 0u; e++)
            {
              ;
            }
            if(vde->Extents[0].DiskNumber >= 100)
            {
              *e++ = '0' + ((vde->Extents[0].DiskNumber / 100) % 10);
            }
            if(vde->Extents[0].DiskNumber >= 10)
            {
              *e++ = '0' + ((vde->Extents[0].DiskNumber / 10) % 10);
            }
            *e++ = '0' + (vde->Extents[0].DiskNumber % 10);
            *e = 0;
            OUTCOME_TRY(diskh, file_handle::file({}, path_view(physicaldrivename, e - physicaldrivename, true), handle::mode::none, handle::creation::open_existing, handle::caching::only_metadata));
            memset(&spq, 0, sizeof(spq));
            spq.PropertyId = StorageDeviceProperty;
            spq.QueryType = PropertyStandardQuery;
            auto *sdd = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR *>(buffer);
            ol.Internal = static_cast<ULONG_PTR>(-1);
            if(DeviceIoControl(diskh.native_handle().h, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), sdd, sizeof(buffer), nullptr, &ol) == 0)
            {
              if(ERROR_IO_PENDING == GetLastError())
              {
                NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol, deadline());
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
            if(sdd->VendorIdOffset > 0 && sdd->VendorIdOffset < sizeof(buffer))
            {
              for(auto n = sdd->VendorIdOffset; (reinterpret_cast<const char *>(buffer))[n] != 0; n++)
              {
                sp.device_name.value.push_back((reinterpret_cast<const char *>(buffer))[n]);
              }
              sp.device_name.value.push_back(',');
            }
            if(sdd->ProductIdOffset > 0 && sdd->ProductIdOffset < sizeof(buffer))
            {
              for(auto n = sdd->ProductIdOffset; (reinterpret_cast<const char *>(buffer))[n] != 0; n++)
              {
                sp.device_name.value.push_back((reinterpret_cast<const char *>(buffer))[n]);
              }
              sp.device_name.value.push_back(',');
            }
            if(sdd->ProductRevisionOffset > 0 && sdd->ProductRevisionOffset < sizeof(buffer))
            {
              for(auto n = sdd->ProductRevisionOffset; (reinterpret_cast<const char *>(buffer))[n] != 0; n++)
              {
                sp.device_name.value.push_back((reinterpret_cast<const char *>(buffer))[n]);
              }
              sp.device_name.value.push_back(',');
            }
            if(!sp.device_name.value.empty())
            {
              sp.device_name.value.resize(sp.device_name.value.size() - 1);
            }
            if(disk_extents > 1)
            {
              sp.device_name.value.append(" (NOTE: plus additional devices)");
            }

            // Get device size
            // IOCTL_STORAGE_READ_CAPACITY needs GENERIC_READ privs which requires admin privs
            // so simply fetch the geometry
            auto *dg = reinterpret_cast<DISK_GEOMETRY_EX *>(buffer);
            ol.Internal = static_cast<ULONG_PTR>(-1);
            if(DeviceIoControl(diskh.native_handle().h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, dg, sizeof(buffer), nullptr, &ol) == 0)
            {
              if(ERROR_IO_PENDING == GetLastError())
              {
                NTSTATUS ntstat = ntwait(volumeh.native_handle().h, ol, deadline());
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
            sp.device_size.value = dg->DiskSize.QuadPart;
          }
        }
        catch(...)
        {
          return std::current_exception();
        }
        return success();
      }
    }  // namespace windows
  }    // namespace storage
}  // namespace storage_profile

LLFIO_V2_NAMESPACE_END
