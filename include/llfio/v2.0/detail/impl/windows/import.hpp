/* Declarations for Microsoft Windows system APIs
(C) 2015-2018 Niall Douglas <http://www.nedproductions.biz/> (14 commits)
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

#ifndef LLFIO_WINDOWS_H
#define LLFIO_WINDOWS_H

#include "../../../handle.hpp"
#include <memory>  // for unique_ptr
#include <mutex>

#ifndef _WIN32
#error You should not include windows/import.hpp on not Windows platforms
#endif

#include <sal.h>

// At some future point we will not do this, and instead import symbols manually
// to avoid the windows.h inclusion
#if 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#else
#error todo
#endif

#if !LLFIO_EXPERIMENTAL_STATUS_CODE
// Bring in the custom NT kernel error code category
#if LLFIO_HEADERS_ONLY
#if !defined(NTKERNEL_ERROR_CATEGORY_INLINE)
#pragma message("WARNING: LLFIO_HEADERS_ONLY=1, LLFIO_EXPERIMENTAL_STATUS_CODE=0 and NTKERNEL_ERROR_CATEGORY_INLINE=1 on Windows, this can produce unreliable binaries where semantic comparisons of error codes randomly fail!")
#define NTKERNEL_ERROR_CATEGORY_INLINE 1  // BOLD!
#define NTKERNEL_ERROR_CATEGORY_STATIC 1
#endif
#endif
#include "ntkernel-error-category/ntkernel_category.hpp"
#endif

LLFIO_V2_NAMESPACE_BEGIN

#if LLFIO_EXPERIMENTAL_STATUS_CODE
#else
//! Helper for constructing an error info from a DWORD
inline error_info win32_error(DWORD c = GetLastError())
{
  return error_info(std::error_code(c, std::system_category()));
}
//! Helper for constructing an error info from a NTSTATUS
inline error_info ntkernel_error(NTSTATUS c)
{
  return error_info(std::error_code(c, ntkernel_error_category::ntkernel_category()));
}
#endif

namespace windows_nt_kernel
{
// Weirdly these appear to be undefined sometimes?
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS) 0x00000000L)
#endif
#ifndef STATUS_ALERTED
#define STATUS_ALERTED ((NTSTATUS) 0x00000101L)
#endif
#ifndef STATUS_DELETE_PENDING
#define STATUS_DELETE_PENDING ((NTSTATUS) 0xC0000056)
#endif

  // From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/FILE_INFORMATION_CLASS.html
  typedef enum _FILE_INFORMATION_CLASS
  {  // NOLINT
    FileDirectoryInformation = 1,
    FileFullDirectoryInformation,
    FileBothDirectoryInformation,
    FileBasicInformation,
    FileStandardInformation,
    FileInternalInformation,
    FileEaInformation,
    FileAccessInformation,
    FileNameInformation,
    FileRenameInformation,
    FileLinkInformation,
    FileNamesInformation,
    FileDispositionInformation,
    FilePositionInformation,
    FileFullEaInformation,
    FileModeInformation,
    FileAlignmentInformation,
    FileAllInformation,
    FileAllocationInformation,
    FileEndOfFileInformation,
    FileAlternateNameInformation,
    FileStreamInformation,
    FilePipeInformation,
    FilePipeLocalInformation,
    FilePipeRemoteInformation,
    FileMailslotQueryInformation,
    FileMailslotSetInformation,
    FileCompressionInformation,
    FileObjectIdInformation,
    FileCompletionInformation,
    FileMoveClusterInformation,
    FileQuotaInformation,
    FileReparsePointInformation,
    FileNetworkOpenInformation,
    FileAttributeTagInformation,
    FileTrackingInformation,
    FileIdBothDirectoryInformation,
    FileIdFullDirectoryInformation,
    FileValidDataLengthInformation,
    FileShortNameInformation,
    FileIoCompletionNotificationInformation,
    FileIoStatusBlockRangeInformation,
    FileIoPriorityHintInformation,
    FileSfioReserveInformation,
    FileSfioVolumeInformation,
    FileHardLinkInformation,
    FileProcessIdsUsingFileInformation,
    FileNormalizedNameInformation,
    FileNetworkPhysicalNameInformation,
    FileIdGlobalTxDirectoryInformation,
    FileIsRemoteDeviceInformation,
    FileUnusedInformation,
    FileNumaNodeInformation,
    FileStandardLinkInformation,
    FileRemoteProtocolInformation,
    FileRenameInformationBypassAccessCheck,
    FileLinkInformationBypassAccessCheck,
    FileVolumeNameInformation,
    FileIdInformation,
    FileIdExtdDirectoryInformation,
    FileReplaceCompletionInformation,
    FileHardLinkFullIdInformation,
    FileIdExtdBothDirectoryInformation,
    FileDispositionInformationEx,
    FileRenameInformationEx,
    FileRenameInformationExBypassAccessCheck,
    FileDesiredStorageClassInformation,
    FileStatInformation,
    FileMemoryPartitionInformation,
    FileStatLxInformation,
    FileCaseSensitiveInformation,
    FileLinkInformationEx,
    FileLinkInformationExBypassAccessCheck,
    FileStorageReserveIdInformation,
    FileCaseSensitiveInformationForceAccessCheck,
    FileMaximumInformation
  } FILE_INFORMATION_CLASS,
  *PFILE_INFORMATION_CLASS;

  typedef enum
  {  // NOLINT
    FileFsVolumeInformation = 1,
    FileFsLabelInformation = 2,
    FileFsSizeInformation = 3,
    FileFsDeviceInformation = 4,
    FileFsAttributeInformation = 5,
    FileFsControlInformation = 6,
    FileFsFullSizeInformation = 7,
    FileFsObjectIdInformation = 8,
    FileFsDriverPathInformation = 9,
    FileFsVolumeFlagsInformation = 10,
    FileFsSectorSizeInformation = 11
  } FS_INFORMATION_CLASS;

  typedef enum
  {
    ObjectBasicInformation = 0,
    ObjectNameInformation = 1,
    ObjectTypeInformation = 2
  } OBJECT_INFORMATION_CLASS;  // NOLINT

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#ifndef STATUS_TIMEOUT
#define STATUS_TIMEOUT ((NTSTATUS) 0x00000102)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS) 0x00000103)
#endif
#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS) 0x80000005)
#endif

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff550671(v=vs.85).aspx
  typedef struct _IO_STATUS_BLOCK  // NOLINT
  {
    union {
      NTSTATUS Status;
      PVOID Pointer;
    };
    ULONG_PTR Information;
  } IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

  // From http://msdn.microsoft.com/en-us/library/windows/desktop/aa380518(v=vs.85).aspx
  typedef struct _LSA_UNICODE_STRING  // NOLINT
  {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
  } LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff557749(v=vs.85).aspx
  typedef struct _OBJECT_ATTRIBUTES  // NOLINT
  {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
  } OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

  using PIO_APC_ROUTINE = void(NTAPI *)(_In_ PVOID ApcContext, _In_ PIO_STATUS_BLOCK IoStatusBlock, _In_ ULONG Reserved);

  typedef struct _IMAGEHLP_LINE64  // NOLINT
  {
    DWORD SizeOfStruct;
    PVOID Key;
    DWORD LineNumber;
    PTSTR FileName;
    DWORD64 Address;
  } IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;

  typedef enum _SECTION_INHERIT
  {
    ViewShare = 1,
    ViewUnmap = 2
  } SECTION_INHERIT,
  *PSECTION_INHERIT;  // NOLINT

  typedef struct _WIN32_MEMORY_RANGE_ENTRY  // NOLINT
  {
    PVOID VirtualAddress;
    SIZE_T NumberOfBytes;
  } WIN32_MEMORY_RANGE_ENTRY, *PWIN32_MEMORY_RANGE_ENTRY;

  typedef enum _SECTION_INFORMATION_CLASS
  {
    SectionBasicInformation,
    SectionImageInformation
  } SECTION_INFORMATION_CLASS,
  *PSECTION_INFORMATION_CLASS;  // NOLINT

  typedef struct _SECTION_BASIC_INFORMATION  // NOLINT
  {
    PVOID BaseAddress;
    ULONG AllocationAttributes;
    LARGE_INTEGER MaximumSize;
  } SECTION_BASIC_INFORMATION, *PSECTION_BASIC_INFORMATION;

  // From https://msdn.microsoft.com/en-us/library/bb432383%28v=vs.85%29.aspx
  using NtQueryObject_t = NTSTATUS(NTAPI *)(_In_opt_ HANDLE Handle, _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass, _Out_opt_ PVOID ObjectInformation, _In_ ULONG ObjectInformationLength, _Out_opt_ PULONG ReturnLength);

  // From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtQueryInformationFile.html
  // and http://msdn.microsoft.com/en-us/library/windows/hardware/ff567052(v=vs.85).aspx
  using NtQueryInformationFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _Out_ PVOID FileInformation, _In_ ULONG Length, _In_ FILE_INFORMATION_CLASS FileInformationClass);

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff567070(v=vs.85).aspx
  using NtQueryVolumeInformationFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _Out_ PVOID FsInformation, _In_ ULONG Length, _In_ FS_INFORMATION_CLASS FsInformationClass);

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff566492(v=vs.85).aspx
  using NtOpenDirectoryObject_t = NTSTATUS(NTAPI *)(_Out_ PHANDLE DirectoryHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes);


  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff567011(v=vs.85).aspx
  using NtOpenFile_t = NTSTATUS(NTAPI *)(_Out_ PHANDLE FileHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ ULONG ShareAccess, _In_ ULONG OpenOptions);

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff566424(v=vs.85).aspx
  using NtCreateFile_t = NTSTATUS(NTAPI *)(_Out_ PHANDLE FileHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_opt_ PLARGE_INTEGER AllocationSize, _In_ ULONG FileAttributes, _In_ ULONG ShareAccess, _In_ ULONG CreateDisposition,
                                           _In_ ULONG CreateOptions, _In_opt_ PVOID EaBuffer, _In_ ULONG EaLength);

  using NtReadFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_opt_ HANDLE Event , _In_opt_ PIO_APC_ROUTINE ApcRoutine, _In_opt_ PVOID ApcContext, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _Out_ PVOID Buffer, _In_ ULONG Length, _In_opt_ PLARGE_INTEGER ByteOffset, _In_opt_ PULONG Key);

  using NtReadFileScatter_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_opt_ HANDLE Event , _In_opt_ PIO_APC_ROUTINE ApcRoutine, _In_opt_ PVOID ApcContext, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ FILE_SEGMENT_ELEMENT SegmentArray, _In_ ULONG Length, _In_opt_ PLARGE_INTEGER ByteOffset, _In_opt_ PULONG Key);

  using NtWriteFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_opt_ HANDLE Event, _In_opt_ PIO_APC_ROUTINE ApcRoutine, _In_opt_ PVOID ApcContext, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ PVOID Buffer, _In_ ULONG Length, _In_opt_ PLARGE_INTEGER ByteOffset, _In_opt_ PULONG Key);

  using NtWriteFileGather_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_opt_ HANDLE Event, _In_opt_ PIO_APC_ROUTINE ApcRoutine, _In_opt_ PVOID ApcContext, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ FILE_SEGMENT_ELEMENT SegmentArray, _In_ ULONG Length, _In_opt_ PLARGE_INTEGER ByteOffset, _In_opt_ PULONG Key);

  using NtDeleteFile_t = NTSTATUS(NTAPI *)(_In_ POBJECT_ATTRIBUTES ObjectAttributes);

  using NtClose_t = NTSTATUS(NTAPI *)(_Out_ HANDLE FileHandle);

  // From https://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtCreateNamedPipeFile.html
  using NtCreateNamedPipeFile_t = NTSTATUS(NTAPI *)(_Out_ PHANDLE NamedPipeFileHandle, _In_ ACCESS_MASK DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ ULONG ShareAccess, _In_ ULONG CreateDisposition, _In_ ULONG CreateOptions, _In_ ULONG WriteModeMessage,
                                                    _In_ ULONG ReadModeMessage, _In_ ULONG NonBlocking, _In_ ULONG MaxInstances, _In_ ULONG InBufferSize, _In_ ULONG OutBufferSize, _In_ PLARGE_INTEGER DefaultTimeOut);

  // From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtQueryDirectoryFile.html
  // and http://msdn.microsoft.com/en-us/library/windows/hardware/ff567047(v=vs.85).aspx
  using NtQueryDirectoryFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_opt_ HANDLE Event, _In_opt_ PIO_APC_ROUTINE ApcRoutine, _In_opt_ PVOID ApcContext, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _Out_ PVOID FileInformation, _In_ ULONG Length, _In_ FILE_INFORMATION_CLASS FileInformationClass,
                                                   _In_ BOOLEAN ReturnSingleEntry, _In_opt_ PUNICODE_STRING FileName, _In_ BOOLEAN RestartScan);

  // From http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/NT%20Objects/File/NtSetInformationFile.html
  // and http://msdn.microsoft.com/en-us/library/windows/hardware/ff567096(v=vs.85).aspx
  using NtSetInformationFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ PVOID FileInformation, _In_ ULONG Length, _In_ FILE_INFORMATION_CLASS FileInformationClass);

  // From http://msdn.microsoft.com/en-us/library/ms648412(v=vs.85).aspx
  using NtWaitForSingleObject_t = NTSTATUS(NTAPI *)(_In_ HANDLE Handle, _In_ BOOLEAN Alertable, _In_opt_ PLARGE_INTEGER Timeout);

  typedef enum _OBJECT_WAIT_TYPE
  {
    WaitAllObject,
    WaitAnyObject
  } OBJECT_WAIT_TYPE,
  *POBJECT_WAIT_TYPE;  // NOLINT

  using NtWaitForMultipleObjects_t = NTSTATUS(NTAPI *)(_In_ ULONG Count, _In_ HANDLE Object[], _In_ OBJECT_WAIT_TYPE WaitType, _In_ BOOLEAN Alertable, _In_opt_ PLARGE_INTEGER Time);

  using NtDelayExecution_t = NTSTATUS(NTAPI *)(_In_ BOOLEAN Alertable, _In_opt_ LARGE_INTEGER *Interval);

  using NtRemoveIoCompletionEx_t = NTSTATUS(NTAPI *)(_In_ HANDLE IoCompletionHandle, /*PFILE_IO_COMPLETION_INFORMATION*/ LPOVERLAPPED_ENTRY IoCompletionInformation, _In_ ULONG Count, _Out_ PULONG NumEntriesRemoved, _In_opt_ PLARGE_INTEGER Timeout, _In_ BOOLEAN Alertable);

  // From https://msdn.microsoft.com/en-us/library/windows/hardware/ff566474(v=vs.85).aspx
  using NtLockFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_opt_ HANDLE Event, _In_opt_ PIO_APC_ROUTINE ApcRoutine, _In_opt_ PVOID ApcContext, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ PLARGE_INTEGER ByteOffset, _In_ PLARGE_INTEGER Length, _In_ ULONG Key, _In_ BOOLEAN FailImmediately,
                                         _In_ BOOLEAN ExclusiveLock);

  // From https://msdn.microsoft.com/en-us/library/windows/hardware/ff567118(v=vs.85).aspx
  using NtUnlockFile_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ PLARGE_INTEGER ByteOffset, _In_ PLARGE_INTEGER Length, _In_ ULONG Key);

  using NtCreateSection_t = NTSTATUS(NTAPI *)(_Out_ PHANDLE SectionHandle, _In_ ACCESS_MASK DesiredAccess, _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes, _In_opt_ PLARGE_INTEGER MaximumSize, _In_ ULONG SectionPageProtection, _In_ ULONG AllocationAttributes, _In_opt_ HANDLE FileHandle);

  using NtQuerySection_t = NTSTATUS(NTAPI *)(_In_ HANDLE SectionHandle, _In_ SECTION_INFORMATION_CLASS InformationClass, _Out_ PVOID InformationBuffer, _In_ ULONG InformationBufferSize, _Out_opt_ PULONG ResultLength);

  using NtExtendSection_t = NTSTATUS(NTAPI *)(_In_ HANDLE SectionHandle, _In_opt_ PLARGE_INTEGER MaximumSize);

  using NtMapViewOfSection_t = NTSTATUS(NTAPI *)(_In_ HANDLE SectionHandle, _In_ HANDLE ProcessHandle, _Inout_ PVOID *BaseAddress, _In_ ULONG_PTR ZeroBits, _In_ SIZE_T CommitSize, _Inout_opt_ PLARGE_INTEGER SectionOffset, _Inout_ PSIZE_T ViewSize, _In_ SECTION_INHERIT InheritDisposition, _In_ ULONG AllocationType,
                                                 _In_ ULONG Win32Protect);

  using NtUnmapViewOfSection_t = NTSTATUS(NTAPI *)(_In_ HANDLE ProcessHandle, _In_opt_ PVOID BaseAddress);

  using NtFlushBuffersFileEx_t = NTSTATUS(NTAPI *)(_In_ HANDLE FileHandle, _In_ ULONG Flags, _In_reads_bytes_(ParametersSize) PVOID Parameters, _In_ ULONG ParametersSize, _Out_ PIO_STATUS_BLOCK IoStatusBlock);

  using NtSetSystemInformation_t = NTSTATUS(NTAPI *)(_In_ INT SystemInformationClass, _In_ PVOID SystemInformation, _In_ ULONG SystemInformationLength);

  using NtAllocateVirtualMemory_t = NTSTATUS(NTAPI *)(_In_ HANDLE ProcessHandle, _Inout_ PVOID *BaseAddress, _In_ ULONG_PTR ZeroBits, _Inout_ PSIZE_T RegionSize, _In_ ULONG AllocationType, _In_ ULONG Protect);

  using NtFreeVirtualMemory_t = NTSTATUS(NTAPI *)(_In_ HANDLE ProcessHandle, _Inout_ PVOID *BaseAddress, _Inout_ PSIZE_T RegionSize, _In_ ULONG FreeType);


  using RtlGenRandom_t = BOOLEAN(NTAPI *)(_Out_ PVOID RandomBuffer, _In_ ULONG RandomBufferLength);

  using OpenProcessToken_t = BOOL(NTAPI *)(_In_ HANDLE ProcessHandle, _In_ DWORD DesiredAccess, _Out_ PHANDLE TokenHandle);

  using LookupPrivilegeValue_t = BOOL(NTAPI *)(_In_opt_ LPCTSTR lpSystemName, _In_ LPCTSTR lpName, _Out_ PLUID lpLuid);

  using AdjustTokenPrivileges_t = BOOL(NTAPI *)(_In_ HANDLE TokenHandle, _In_ BOOL DisableAllPrivileges, _In_opt_ PTOKEN_PRIVILEGES NewState, _In_ DWORD BufferLength, _Out_opt_ PTOKEN_PRIVILEGES PreviousState, _Out_opt_ PDWORD ReturnLength);

  using PrefetchVirtualMemory_t = BOOL(NTAPI *)(_In_ HANDLE hProcess, _In_ ULONG_PTR NumberOfEntries, _In_ PWIN32_MEMORY_RANGE_ENTRY VirtualAddresses, _In_ ULONG Flags);

  using DiscardVirtualMemory_t = BOOL(NTAPI *)(_In_ PVOID VirtualAddress, _In_ SIZE_T Size);

  using RtlCaptureStackBackTrace_t = USHORT(NTAPI *)(_In_ ULONG FramesToSkip, _In_ ULONG FramesToCapture, _Out_ PVOID *BackTrace, _Out_opt_ PULONG BackTraceHash);

  using SymInitialize_t = BOOL(NTAPI *)(_In_ HANDLE hProcess, _In_opt_ PCTSTR UserSearchPath, _In_ BOOL fInvadeProcess);

  using SymGetLineFromAddr64_t = BOOL(NTAPI *)(_In_ HANDLE hProcess, _In_ DWORD64 dwAddr, _Out_ PDWORD pdwDisplacement, _Out_ PIMAGEHLP_LINE64 Line);

  using RtlDosPathNameToNtPathName_U_t = BOOLEAN(NTAPI *)(__in PCWSTR DosFileName, __out PUNICODE_STRING NtFileName, __out_opt PWSTR *FilePart, __out_opt PVOID RelativeName);

  using RtlUTF8ToUnicodeN_t = NTSTATUS(NTAPI *)(_Out_opt_ PWSTR UnicodeStringDestination, _In_ ULONG UnicodeStringMaxByteCount, _Out_ PULONG UnicodeStringActualByteCount, _In_ PCCH UTF8StringSource, _In_ ULONG UTF8StringByteCount);

  using RtlUnicodeToUTF8N_t = NTSTATUS(NTAPI *)(_Out_opt_ PCHAR UTF8StringDestination, _In_ ULONG UTF8StringMaxByteCount, _Out_ PULONG UTF8StringActualByteCount, _In_ PCWCH UnicodeStringSource, _In_ ULONG UnicodeStringByteCount);

  using RtlAnsiStringToUnicodeString_t = NTSTATUS(NTAPI *)(PUNICODE_STRING DestinationString, PCANSI_STRING SourceString, BOOLEAN AllocateDestinationString);

  using RtlUnicodeStringToAnsiString_t = NTSTATUS(NTAPI *)(PANSI_STRING DestinationString, PCUNICODE_STRING SourceString, BOOLEAN AllocateDestinationString);

  using RtlOemStringToUnicodeString_t = NTSTATUS(NTAPI *)(PUNICODE_STRING DestinationString, PCOEM_STRING SourceString, BOOLEAN AllocateDestinationString);

  using RtlUnicodeStringToOemString_t = NTSTATUS(NTAPI *)(POEM_STRING DestinationString, PCUNICODE_STRING SourceString, BOOLEAN AllocateDestinationString);

  using RtlFreeAnsiString_t = NTSTATUS(NTAPI *)(PANSI_STRING String);

  using RtlFreeUnicodeString_t = NTSTATUS(NTAPI *)(PUNICODE_STRING String);

  typedef struct _FILE_BASIC_INFORMATION  // NOLINT
  {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
  } FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

  typedef struct _FILE_STANDARD_INFORMATION  // NOLINT
  {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
  } FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

  typedef struct _FILE_INTERNAL_INFORMATION  // NOLINT
  {
    LARGE_INTEGER IndexNumber;
  } FILE_INTERNAL_INFORMATION, *PFILE_INTERNAL_INFORMATION;

  typedef struct _FILE_EA_INFORMATION  // NOLINT
  {
    union {
      ULONG EaSize;
      ULONG ReparsePointTag;
    };
  } FILE_EA_INFORMATION, *PFILE_EA_INFORMATION;

  typedef struct _FILE_ACCESS_INFORMATION  // NOLINT
  {
    ACCESS_MASK AccessFlags;
  } FILE_ACCESS_INFORMATION, *PFILE_ACCESS_INFORMATION;

  typedef struct _FILE_POSITION_INFORMATION  // NOLINT
  {
    LARGE_INTEGER CurrentByteOffset;
  } FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

  typedef struct _FILE_MODE_INFORMATION  // NOLINT
  {
    ULONG Mode;
  } FILE_MODE_INFORMATION, *PFILE_MODE_INFORMATION;

  typedef struct _FILE_ALIGNMENT_INFORMATION  // NOLINT
  {
    ULONG AlignmentRequirement;
  } FILE_ALIGNMENT_INFORMATION, *PFILE_ALIGNMENT_INFORMATION;

  typedef struct _FILE_NAME_INFORMATION  // NOLINT
  {
    ULONG FileNameLength;
    WCHAR FileName[1];
  } FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

  typedef struct _FILE_RENAME_INFORMATION  // NOLINT
  {
    ULONG Flags;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
  } FILE_RENAME_INFORMATION, *PFILE_RENAME_INFORMATION;

  typedef struct _FILE_LINK_INFORMATION  // NOLINT
  {
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
  } FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;

  typedef struct _FILE_DISPOSITION_INFORMATION  // NOLINT
  {
    BOOLEAN _DeleteFile;
  } FILE_DISPOSITION_INFORMATION, *PFILE_DISPOSITION_INFORMATION;

  typedef struct _FILE_DISPOSITION_INFORMATION_EX  // NOLINT
  {
    ULONG Flags;
  } FILE_DISPOSITION_INFORMATION_EX, *PFILE_DISPOSITION_INFORMATION_EX;

  typedef struct _FILE_COMPLETION_INFORMATION  // NOLINT
  {
    HANDLE Port;
    PVOID Key;
  } FILE_COMPLETION_INFORMATION, *PFILE_COMPLETION_INFORMATION;

  typedef struct _FILE_ALL_INFORMATION  // NOLINT
  {
    FILE_BASIC_INFORMATION BasicInformation;
    FILE_STANDARD_INFORMATION StandardInformation;
    FILE_INTERNAL_INFORMATION InternalInformation;
    FILE_EA_INFORMATION EaInformation;
    FILE_ACCESS_INFORMATION AccessInformation;
    FILE_POSITION_INFORMATION PositionInformation;
    FILE_MODE_INFORMATION ModeInformation;
    FILE_ALIGNMENT_INFORMATION AlignmentInformation;
    FILE_NAME_INFORMATION NameInformation;
  } FILE_ALL_INFORMATION, *PFILE_ALL_INFORMATION;

  typedef struct _FILE_FS_ATTRIBUTE_INFORMATION  // NOLINT
  {
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
  } FILE_FS_ATTRIBUTE_INFORMATION, *PFILE_FS_ATTRIBUTE_INFORMATION;

  typedef struct _FILE_FS_FULL_SIZE_INFORMATION  // NOLINT
  {
    LARGE_INTEGER TotalAllocationUnits;
    LARGE_INTEGER CallerAvailableAllocationUnits;
    LARGE_INTEGER ActualAvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
  } FILE_FS_FULL_SIZE_INFORMATION, *PFILE_FS_FULL_SIZE_INFORMATION;

  typedef struct _FILE_FS_OBJECTID_INFORMATION  // NOLINT
  {
    UCHAR ObjectId[16];
    UCHAR ExtendedInfo[48];
  } FILE_FS_OBJECTID_INFORMATION, *PFILE_FS_OBJECTID_INFORMATION;

  typedef struct _FILE_FS_SECTOR_SIZE_INFORMATION  // NOLINT
  {
    ULONG LogicalBytesPerSector;
    ULONG PhysicalBytesPerSectorForAtomicity;
    ULONG PhysicalBytesPerSectorForPerformance;
    ULONG FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    ULONG Flags;
    ULONG ByteOffsetForSectorAlignment;
    ULONG ByteOffsetForPartitionAlignment;
  } FILE_FS_SECTOR_SIZE_INFORMATION, *PFILE_FS_SECTOR_SIZE_INFORMATION;

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff540310(v=vs.85).aspx
  typedef struct _FILE_ID_FULL_DIR_INFORMATION  // NOLINT
  {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    union {
      ULONG EaSize;
      ULONG ReparsePointTag;
    };
    LARGE_INTEGER FileId;
    WCHAR FileName[1];
  } FILE_ID_FULL_DIR_INFORMATION, *PFILE_ID_FULL_DIR_INFORMATION;

  // From https://msdn.microsoft.com/en-us/library/windows/hardware/ff540354(v=vs.85).aspx
  typedef struct _FILE_REPARSE_POINT_INFORMATION  // NOLINT
  {
    LARGE_INTEGER FileReference;
    ULONG Tag;
  } FILE_REPARSE_POINT_INFORMATION, *PFILE_REPARSE_POINT_INFORMATION;

  // From http://msdn.microsoft.com/en-us/library/windows/hardware/ff552012(v=vs.85).aspx
  typedef struct _REPARSE_DATA_BUFFER  // NOLINT
  {
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
      struct
      {
        USHORT SubstituteNameOffset;
        USHORT SubstituteNameLength;
        USHORT PrintNameOffset;
        USHORT PrintNameLength;
        ULONG Flags;
        WCHAR PathBuffer[1];
      } SymbolicLinkReparseBuffer;
      struct
      {
        USHORT SubstituteNameOffset;
        USHORT SubstituteNameLength;
        USHORT PrintNameOffset;
        USHORT PrintNameLength;
        WCHAR PathBuffer[1];
      } MountPointReparseBuffer;
      struct
      {
        UCHAR DataBuffer[1];
      } GenericReparseBuffer;
    };
  } REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

  typedef struct _FILE_CASE_SENSITIVE_INFORMATION
  {
    ULONG Flags;
  } FILE_CASE_SENSITIVE_INFORMATION, *PFILE_CASE_SENSITIVE_INFORMATION;

  static NtQueryObject_t NtQueryObject;
  static NtQueryInformationFile_t NtQueryInformationFile;
  static NtQueryVolumeInformationFile_t NtQueryVolumeInformationFile;
  static NtOpenDirectoryObject_t NtOpenDirectoryObject;
  static NtOpenFile_t NtOpenFile;
  static NtCreateFile_t NtCreateFile;
  static NtReadFile_t NtReadFile;
  static NtReadFileScatter_t NtReadFileScatter;
  static NtWriteFile_t NtWriteFile;
  static NtWriteFileGather_t NtWriteFileGather;
  static NtDeleteFile_t NtDeleteFile;
  static NtClose_t NtClose;
  static NtCreateNamedPipeFile_t NtCreateNamedPipeFile;
  static NtQueryDirectoryFile_t NtQueryDirectoryFile;
  static NtSetInformationFile_t NtSetInformationFile;
  static NtWaitForSingleObject_t NtWaitForSingleObject;
  static NtWaitForMultipleObjects_t NtWaitForMultipleObjects;
  static NtDelayExecution_t NtDelayExecution;
  static NtRemoveIoCompletionEx_t NtRemoveIoCompletionEx;
  static NtLockFile_t NtLockFile;
  static NtUnlockFile_t NtUnlockFile;
  static NtCreateSection_t NtCreateSection;
  static NtQuerySection_t NtQuerySection;
  static NtExtendSection_t NtExtendSection;
  static NtMapViewOfSection_t NtMapViewOfSection;
  static NtUnmapViewOfSection_t NtUnmapViewOfSection;
  static NtFlushBuffersFileEx_t NtFlushBuffersFileEx;
  static NtSetSystemInformation_t NtSetSystemInformation;
  static NtAllocateVirtualMemory_t NtAllocateVirtualMemory;
  static NtFreeVirtualMemory_t NtFreeVirtualMemory;
  static RtlGenRandom_t RtlGenRandom;
  static OpenProcessToken_t OpenProcessToken;
  static LookupPrivilegeValue_t LookupPrivilegeValue;
  static AdjustTokenPrivileges_t AdjustTokenPrivileges;
  static PrefetchVirtualMemory_t PrefetchVirtualMemory_;
  static DiscardVirtualMemory_t DiscardVirtualMemory_;
  static SymInitialize_t SymInitialize;
  static SymGetLineFromAddr64_t SymGetLineFromAddr64;
  static RtlCaptureStackBackTrace_t RtlCaptureStackBackTrace;
  static RtlDosPathNameToNtPathName_U_t RtlDosPathNameToNtPathName_U;
  static RtlUTF8ToUnicodeN_t RtlUTF8ToUnicodeN;
  static RtlUnicodeToUTF8N_t RtlUnicodeToUTF8N;
  static RtlAnsiStringToUnicodeString_t RtlAnsiStringToUnicodeString;
  static RtlUnicodeStringToAnsiString_t RtlUnicodeStringToAnsiString;
  static RtlOemStringToUnicodeString_t RtlOemStringToUnicodeString;
  static RtlUnicodeStringToOemString_t RtlUnicodeStringToOemString;
  static RtlFreeUnicodeString_t RtlFreeUnicodeString;
  static RtlFreeAnsiString_t RtlFreeAnsiString;


#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4706)  // assignment within conditional
#pragma warning(disable : 6387)  // MSVC sanitiser warns that GetModuleHandleA() might fail (hah!)
#endif
  inline void doinit()
  {
    if(RtlFreeAnsiString != nullptr)
    {
      return;
    }
    static std::mutex lock;
    std::lock_guard<decltype(lock)> g(lock);
    static HMODULE ntdllh = GetModuleHandleA("NTDLL.DLL");
    static HMODULE kernel32 = GetModuleHandleA("KERNEL32.DLL");
    if(NtQueryObject == nullptr)
    {
      if((NtQueryObject = reinterpret_cast<NtQueryObject_t>(GetProcAddress(ntdllh, "NtQueryObject"))) == nullptr)
      {
        abort();
      }
    }
    if(NtQueryInformationFile == nullptr)
    {
      if((NtQueryInformationFile = reinterpret_cast<NtQueryInformationFile_t>(GetProcAddress(ntdllh, "NtQueryInformationFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtQueryVolumeInformationFile == nullptr)
    {
      if((NtQueryVolumeInformationFile = reinterpret_cast<NtQueryVolumeInformationFile_t>(GetProcAddress(ntdllh, "NtQueryVolumeInformationFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtOpenDirectoryObject == nullptr)
    {
      if((NtOpenDirectoryObject = reinterpret_cast<NtOpenDirectoryObject_t>(GetProcAddress(ntdllh, "NtOpenDirectoryObject"))) == nullptr)
      {
        abort();
      }
    }
    if(NtOpenFile == nullptr)
    {
      if((NtOpenFile = reinterpret_cast<NtOpenFile_t>(GetProcAddress(ntdllh, "NtOpenFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtCreateFile == nullptr)
    {
      if((NtCreateFile = reinterpret_cast<NtCreateFile_t>(GetProcAddress(ntdllh, "NtCreateFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtReadFile == nullptr)
    {
      if((NtReadFile = reinterpret_cast<NtReadFile_t>(GetProcAddress(ntdllh, "NtReadFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtReadFileScatter == nullptr)
    {
      if((NtReadFileScatter = reinterpret_cast<NtReadFileScatter_t>(GetProcAddress(ntdllh, "NtReadFileScatter"))) == nullptr)
      {
        abort();
      }
    }
    if(NtWriteFile == nullptr)
    {
      if((NtWriteFile = reinterpret_cast<NtWriteFile_t>(GetProcAddress(ntdllh, "NtWriteFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtWriteFileGather == nullptr)
    {
      if((NtWriteFileGather = reinterpret_cast<NtWriteFileGather_t>(GetProcAddress(ntdllh, "NtWriteFileGather"))) == nullptr)
      {
        abort();
      }
    }
    if(NtDeleteFile == nullptr)
    {
      if((NtDeleteFile = reinterpret_cast<NtDeleteFile_t>(GetProcAddress(ntdllh, "NtDeleteFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtClose == nullptr)
    {
      if((NtClose = reinterpret_cast<NtClose_t>(GetProcAddress(ntdllh, "NtClose"))) == nullptr)
      {
        abort();
      }
    }
    if(NtCreateNamedPipeFile == nullptr)
    {
      if((NtCreateNamedPipeFile = reinterpret_cast<NtCreateNamedPipeFile_t>(GetProcAddress(ntdllh, "NtCreateNamedPipeFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtQueryDirectoryFile == nullptr)
    {
      if((NtQueryDirectoryFile = reinterpret_cast<NtQueryDirectoryFile_t>(GetProcAddress(ntdllh, "NtQueryDirectoryFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtSetInformationFile == nullptr)
    {
      if((NtSetInformationFile = reinterpret_cast<NtSetInformationFile_t>(GetProcAddress(ntdllh, "NtSetInformationFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtWaitForSingleObject == nullptr)
    {
      if((NtWaitForSingleObject = reinterpret_cast<NtWaitForSingleObject_t>(GetProcAddress(ntdllh, "NtWaitForSingleObject"))) == nullptr)
      {
        abort();
      }
    }
    if(NtWaitForMultipleObjects == nullptr)
    {
      if((NtWaitForMultipleObjects = reinterpret_cast<NtWaitForMultipleObjects_t>(GetProcAddress(ntdllh, "NtWaitForMultipleObjects"))) == nullptr)
      {
        abort();
      }
    }
    if(NtDelayExecution == nullptr)
    {
      if((NtDelayExecution = reinterpret_cast<NtDelayExecution_t>(GetProcAddress(ntdllh, "NtDelayExecution"))) == nullptr)
      {
        abort();
      }
    }
    if(NtRemoveIoCompletionEx == nullptr)
    {
      if((NtRemoveIoCompletionEx = reinterpret_cast<NtRemoveIoCompletionEx_t>(GetProcAddress(ntdllh, "NtRemoveIoCompletionEx"))) == nullptr)
      {
        abort();
      }
    }
    if(NtLockFile == nullptr)
    {
      if((NtLockFile = reinterpret_cast<NtLockFile_t>(GetProcAddress(ntdllh, "NtLockFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtUnlockFile == nullptr)
    {
      if((NtUnlockFile = reinterpret_cast<NtUnlockFile_t>(GetProcAddress(ntdllh, "NtUnlockFile"))) == nullptr)
      {
        abort();
      }
    }
    if(NtCreateSection == nullptr)
    {
      if((NtCreateSection = reinterpret_cast<NtCreateSection_t>(GetProcAddress(ntdllh, "NtCreateSection"))) == nullptr)
      {
        abort();
      }
    }
    if(NtQuerySection == nullptr)
    {
      if((NtQuerySection = reinterpret_cast<NtQuerySection_t>(GetProcAddress(ntdllh, "NtQuerySection"))) == nullptr)
      {
        abort();
      }
    }
    if(NtExtendSection == nullptr)
    {
      if((NtExtendSection = reinterpret_cast<NtExtendSection_t>(GetProcAddress(ntdllh, "NtExtendSection"))) == nullptr)
      {
        abort();
      }
    }
    if(NtMapViewOfSection == nullptr)
    {
      if((NtMapViewOfSection = reinterpret_cast<NtMapViewOfSection_t>(GetProcAddress(ntdllh, "NtMapViewOfSection"))) == nullptr)
      {
        abort();
      }
    }
    if(NtUnmapViewOfSection == nullptr)
    {
      if((NtUnmapViewOfSection = reinterpret_cast<NtUnmapViewOfSection_t>(GetProcAddress(ntdllh, "NtUnmapViewOfSection"))) == nullptr)
      {
        abort();
      }
    }
    if(NtFlushBuffersFileEx == nullptr)
    {
      if((NtFlushBuffersFileEx = reinterpret_cast<NtFlushBuffersFileEx_t>(GetProcAddress(ntdllh, "NtFlushBuffersFileEx"))) == nullptr)
      {
        abort();
      }
    }
    if(NtSetSystemInformation == nullptr)
    {
      if((NtSetSystemInformation = reinterpret_cast<NtSetSystemInformation_t>(GetProcAddress(ntdllh, "NtSetSystemInformation"))) == nullptr)
      {
        abort();
      }
    }
    if(NtAllocateVirtualMemory == nullptr)
    {
      if((NtAllocateVirtualMemory = reinterpret_cast<NtAllocateVirtualMemory_t>(GetProcAddress(ntdllh, "NtAllocateVirtualMemory"))) == nullptr)
      {
        abort();
      }
    }
    if(NtFreeVirtualMemory == nullptr)
    {
      if((NtFreeVirtualMemory = reinterpret_cast<NtFreeVirtualMemory_t>(GetProcAddress(ntdllh, "NtFreeVirtualMemory"))) == nullptr)
      {
        abort();
      }
    }

    HMODULE advapi32 = LoadLibraryA("ADVAPI32.DLL");
    if(RtlGenRandom == nullptr)
    {
      if((RtlGenRandom = reinterpret_cast<RtlGenRandom_t>(GetProcAddress(advapi32, "SystemFunction036"))) == nullptr)
      {
        abort();
      }
    }
    if(OpenProcessToken == nullptr)
    {
      if((OpenProcessToken = reinterpret_cast<OpenProcessToken_t>(GetProcAddress(advapi32, "OpenProcessToken"))) == nullptr)
      {
        abort();
      }
    }
    if(!LookupPrivilegeValue)
    {
      if((LookupPrivilegeValue = reinterpret_cast<LookupPrivilegeValue_t>(GetProcAddress(advapi32, "LookupPrivilegeValueW"))) == nullptr)
      {
        abort();
      }
    }
    if(AdjustTokenPrivileges == nullptr)
    {
      if((AdjustTokenPrivileges = reinterpret_cast<AdjustTokenPrivileges_t>(GetProcAddress(advapi32, "AdjustTokenPrivileges"))) == nullptr)
      {
        abort();
      }
    }
    // Only provided on Windows 8 and above
    if(PrefetchVirtualMemory_ == nullptr)
    {
      PrefetchVirtualMemory_ = reinterpret_cast<PrefetchVirtualMemory_t>(GetProcAddress(kernel32, "PrefetchVirtualMemory"));
    }
    if(DiscardVirtualMemory_ == nullptr)
    {
      DiscardVirtualMemory_ = reinterpret_cast<DiscardVirtualMemory_t>(GetProcAddress(kernel32, "DiscardVirtualMemory"));
    }
#ifdef LLFIO_OP_STACKBACKTRACEDEPTH
    if(dbghelp)
    {
      HMODULE dbghelp = LoadLibraryA("DBGHELP.DLL");
      if(!(SymInitialize = (SymInitialize_t) GetProcAddress(dbghelp, "SymInitializeW")))
        abort();
      if(!SymInitialize(GetCurrentProcess(), nullptr, true))
        abort();
      if(!(SymGetLineFromAddr64 = (SymGetLineFromAddr64_t) GetProcAddress(dbghelp, "SymGetLineFromAddrW64")))
        abort();
    }
#endif
    if(RtlCaptureStackBackTrace == nullptr)
    {
      if((RtlCaptureStackBackTrace = reinterpret_cast<RtlCaptureStackBackTrace_t>(GetProcAddress(ntdllh, "RtlCaptureStackBackTrace"))) == nullptr)
      {
        abort();
      }
    }

    if(RtlDosPathNameToNtPathName_U == nullptr)
    {
      if((RtlDosPathNameToNtPathName_U = reinterpret_cast<RtlDosPathNameToNtPathName_U_t>(GetProcAddress(ntdllh, "RtlDosPathNameToNtPathName_U"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlUTF8ToUnicodeN == nullptr)
    {
      if((RtlUTF8ToUnicodeN = reinterpret_cast<RtlUTF8ToUnicodeN_t>(GetProcAddress(ntdllh, "RtlUTF8ToUnicodeN"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlUnicodeToUTF8N == nullptr)
    {
      if((RtlUnicodeToUTF8N = reinterpret_cast<RtlUnicodeToUTF8N_t>(GetProcAddress(ntdllh, "RtlUnicodeToUTF8N"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlAnsiStringToUnicodeString == nullptr)
    {
      if((RtlAnsiStringToUnicodeString = reinterpret_cast<RtlAnsiStringToUnicodeString_t>(GetProcAddress(ntdllh, "RtlAnsiStringToUnicodeString"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlUnicodeStringToAnsiString == nullptr)
    {
      if((RtlUnicodeStringToAnsiString = reinterpret_cast<RtlUnicodeStringToAnsiString_t>(GetProcAddress(ntdllh, "RtlUnicodeStringToAnsiString"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlOemStringToUnicodeString == nullptr)
    {
      if((RtlOemStringToUnicodeString = reinterpret_cast<RtlOemStringToUnicodeString_t>(GetProcAddress(ntdllh, "RtlOemStringToUnicodeString"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlUnicodeStringToOemString == nullptr)
    {
      if((RtlUnicodeStringToOemString = reinterpret_cast<RtlUnicodeStringToOemString_t>(GetProcAddress(ntdllh, "RtlUnicodeStringToOemString"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlFreeUnicodeString == nullptr)
    {
      if((RtlFreeUnicodeString = reinterpret_cast<RtlFreeUnicodeString_t>(GetProcAddress(ntdllh, "RtlFreeUnicodeString"))) == nullptr)
      {
        abort();
      }
    }
    if(RtlFreeAnsiString == nullptr)
    {
      if((RtlFreeAnsiString = reinterpret_cast<RtlFreeAnsiString_t>(GetProcAddress(ntdllh, "RtlFreeAnsiString"))) == nullptr)
      {
        abort();
      }
    }

    // MAKE SURE you update the early exit check at the top to whatever the last of these is!
  }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  inline void init()
  {
    static bool initialised = false;
    if(!initialised)
    {
      doinit();
      initialised = true;
    }
  }

  inline filesystem::file_type to_st_type(ULONG FileAttributes, ULONG ReparsePointTag)
  {
#ifdef LLFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
    if(FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && (ReparsePointTag == IO_REPARSE_TAG_MOUNT_POINT || ReparsePointTag == IO_REPARSE_TAG_SYMLINK))
      return filesystem::file_type::symlink_file;
    // return filesystem::file_type::reparse_file;
    else if(FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      return filesystem::file_type::directory_file;
    else
      return filesystem::file_type::regular_file;
#else
    if(((FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) && (ReparsePointTag == IO_REPARSE_TAG_MOUNT_POINT || ReparsePointTag == IO_REPARSE_TAG_SYMLINK))
    {
      return filesystem::file_type::symlink;
      // return filesystem::file_type::reparse_file;
    }
    if((FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
    {
      return filesystem::file_type::directory;
    }


    return filesystem::file_type::regular;

#endif
  }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6326)  // comparison of constants
#endif
  inline std::chrono::system_clock::time_point to_timepoint(LARGE_INTEGER time)
  {
    // For speed we make the big assumption that the STL's system_clock is based on the time_t epoch 1st Jan 1970.
    static constexpr unsigned long long FILETIME_OFFSET_TO_1970 = ((27111902ULL << 32U) + 3577643008ULL);
    // Need to have this self-adapt to the STL being used
    static constexpr unsigned long long STL_TICKS_PER_SEC = static_cast<unsigned long long>(std::chrono::system_clock::period::den) / std::chrono::system_clock::period::num;
    static constexpr unsigned long long multiplier = STL_TICKS_PER_SEC >= 10000000ULL ? STL_TICKS_PER_SEC / 10000000ULL : 1;
    static constexpr unsigned long long divider = STL_TICKS_PER_SEC >= 10000000ULL ? 1 : 10000000ULL / STL_TICKS_PER_SEC;

    unsigned long long ticks_since_1970 = (time.QuadPart - FILETIME_OFFSET_TO_1970);  // In 100ns increments
    std::chrono::system_clock::duration duration(ticks_since_1970 * multiplier / divider);
    return std::chrono::system_clock::time_point(duration);
  }
  inline LARGE_INTEGER from_timepoint(std::chrono::system_clock::time_point time)
  {
    // For speed we make the big assumption that the STL's system_clock is based on the time_t epoch 1st Jan 1970.
    static constexpr unsigned long long FILETIME_OFFSET_TO_1970 = ((27111902ULL << 32U) + 3577643008ULL);
    static const std::chrono::system_clock::time_point time_point_1970 = std::chrono::system_clock::from_time_t(0);

    LARGE_INTEGER ret{};
    ret.QuadPart = FILETIME_OFFSET_TO_1970 + std::chrono::duration_cast<std::chrono::nanoseconds>(time - time_point_1970).count() / 100;
    return ret;
  }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Adapted from http://www.cprogramming.com/snippets/source-code/convert-ntstatus-win32-error
// Could use RtlNtStatusToDosError() instead
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6387)  // MSVC sanitiser warns on misuse of GetOverlappedResult
#endif
  inline DWORD win32_error_from_nt_status(NTSTATUS ntstatus)
  {
    DWORD br;
    OVERLAPPED o{};

    SetLastError(0);
    o.Internal = ntstatus;
    o.InternalHigh = 0;
    o.Offset = 0;
    o.OffsetHigh = 0;
    o.hEvent = nullptr;
    GetOverlappedResult(nullptr, &o, &br, FALSE);
    return GetLastError();
  }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}  // namespace windows_nt_kernel

#if 0
inline void fill_stat_t(stat_t &stat, LLFIO_POSIX_STAT_STRUCT s, metadata_flags wanted)
{
#ifndef _WIN32
  if (!!(wanted&metadata_flags::dev)) { stat.st_dev = s.st_dev; }
#endif
  if (!!(wanted&metadata_flags::ino)) { stat.st_ino = s.st_ino; }
  if (!!(wanted&metadata_flags::type)) { stat.st_type = to_st_type(s.st_mode); }
#ifndef _WIN32
  if (!!(wanted&metadata_flags::perms)) { stat.st_mode = s.st_perms; }
#endif
  if (!!(wanted&metadata_flags::nlink)) { stat.st_nlink = s.st_nlink; }
#ifndef _WIN32
  if (!!(wanted&metadata_flags::uid)) { stat.st_uid = s.st_uid; }
  if (!!(wanted&metadata_flags::gid)) { stat.st_gid = s.st_gid; }
  if (!!(wanted&metadata_flags::rdev)) { stat.st_rdev = s.st_rdev; }
#endif
  if (!!(wanted&metadata_flags::atim)) { stat.st_atim = chrono::system_clock::from_time_t(s.st_atime); }
  if (!!(wanted&metadata_flags::mtim)) { stat.st_mtim = chrono::system_clock::from_time_t(s.st_mtime); }
  if (!!(wanted&metadata_flags::birthtim)) { stat.st_birthtim = chrono::system_clock::from_time_t(s.st_ctime); }
  if (!!(wanted&metadata_flags::size)) { stat.st_size = s.st_size; }
}
#endif

// Utility routines for implementing deadline sleeps on Windows which only provides interval sleeps
#if 0  // This is the win32 edition. The NT kernel edition is much cleaner and lower overhead.
struct win_handle_deleter
{
  void operator()(HANDLE h) { CloseHandle(h); }
};
inline std::unique_ptr<void, win_handle_deleter> create_waitable_timer()
{
  HANDLE ret = CreateWaitableTimer(nullptr, true, nullptr);
  if(INVALID_HANDLE_VALUE == ret)
    throw std::system_error(GetLastError(), std::system_category());
  return std::unique_ptr<void, win_handle_deleter>(ret);
}
inline HANDLE get_thread_local_waitable_timer()
{
  static thread_local auto self = create_waitable_timer();
  return self.get();
}

/*! Defines a number of variables into its scope:
- began_steady: Set to the steady clock at the beginning of a sleep
- end_utc: Set to the system clock when the sleep must end
- sleep_interval: Set to the number of steady milliseconds until the sleep must end
- sleep_object: Set to a primed deadline timer HANDLE which will signal when the system clock reaches the deadline
*/
#define LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d)                                                                                                                                                                                                                                                                                    \
  std::chrono::steady_clock::time_point began_steady;                                                                                                                                                                                                                                                                          \
                                                                                                                                                                                                                                                                                                                               \
  std::chrono::system_clock::time_point end_utc;                                                                                                                                                                                                                                                                               \
                                                                                                                                                                                                                                                                                                                               \
  if(d)                                                                                                                                                                                                                                                                                                                        \
                                                                                                                                                                                                                                                                                                                               \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
      began_steady = std::chrono::steady_clock::now();                                                                                                                                                                                                                                                                         \
    else                                                                                                                                                                                                                                                                                                                       \
      end_utc = (d).to_time_point();                                                                                                                                                                                                                                                                                           \
  }                                                                                                                                                                                                                                                                                                                            \
                                                                                                                                                                                                                                                                                                                               \
  DWORD sleep_interval = INFINITE;                                                                                                                                                                                                                                                                                             \
                                                                                                                                                                                                                                                                                                                               \
  HANDLE sleep_object = nullptr;

#define LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d)                                                                                                                                                                                                                                                                                    \
                                                                                                                                                                                                                                                                                                                               \
  if(d)                                                                                                                                                                                                                                                                                                                        \
                                                                                                                                                                                                                                                                                                                               \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                                                                          \
      std::chrono::milliseconds ms;                                                                                                                                                                                                                                                                                            \
      ms = std::chrono::duration_cast<std::chrono::milliseconds>((began_steady + std::chrono::nanoseconds(d.nsecs)) - std::chrono::steady_clock::now());                                                                                                                                                                       \
      if(ms.count() < 0)                                                                                                                                                                                                                                                                                                       \
        sleep_interval = 0;                                                                                                                                                                                                                                                                                                    \
      else                                                                                                                                                                                                                                                                                                                     \
        sleep_interval = (DWORD) ms.count();                                                                                                                                                                                                                                                                                   \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      sleep_object = get_thread_local_waitable_timer();                                                                                                                                                                                                                                                                        \
      LARGE_INTEGER due_time = windows_nt_kernel::from_timepoint(end_utc);                                                                                                                                                                                                                                                     \
      if(!SetWaitableTimer(sleep_object, &due_time, 0, nullptr, nullptr, false))                                                                                                                                                                                                                                               \
        throw std::system_error(GetLastError(), std::system_category());                                                                                                                                                                                                                                                       \
    }                                                                                                                                                                                                                                                                                                                          \
  }

#define LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(type, d)                                                                                                                                                                                                                                                                            \
                                                                                                                                                                                                                                                                                                                               \
  if(d)                                                                                                                                                                                                                                                                                                                        \
                                                                                                                                                                                                                                                                                                                               \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::steady_clock::now() >= (began_steady + std::chrono::nanoseconds((d).nsecs)))                                                                                                                                                                                                                             \
        return make_errored_result<type>(errc::timed_out);                                                                                                                                                                                                                                                                     \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::system_clock::now() >= end_utc)                                                                                                                                                                                                                                                                          \
        return make_errored_result<type>(errc::timed_out);                                                                                                                                                                                                                                                                     \
    }                                                                                                                                                                                                                                                                                                                          \
  }
#else
/*! Defines a number of variables into its scope:
- began_steady: Set to the steady clock at the beginning of a sleep
- end_utc: Set to the system clock when the sleep must end
- sleep_interval: Set to the number of steady milliseconds until the sleep must end
- sleep_object: Set to a primed deadline timer HANDLE which will signal when the system clock reaches the deadline
*/
#define LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d)                                                                                                                                                                                                                                                                                    \
  std::chrono::steady_clock::time_point began_steady;                                                                                                                                                                                                                                                                          \
                                                                                                                                                                                                                                                                                                                               \
  std::chrono::system_clock::time_point end_utc;                                                                                                                                                                                                                                                                               \
                                                                                                                                                                                                                                                                                                                               \
  alignas(8) LARGE_INTEGER _timeout{};                                                                                                                                                                                                                                                                                         \
                                                                                                                                                                                                                                                                                                                               \
  memset(&_timeout, 0, sizeof(_timeout));                                                                                                                                                                                                                                                                                      \
                                                                                                                                                                                                                                                                                                                               \
  LARGE_INTEGER *timeout = nullptr;                                                                                                                                                                                                                                                                                            \
                                                                                                                                                                                                                                                                                                                               \
  if(d)                                                                                                                                                                                                                                                                                                                        \
                                                                                                                                                                                                                                                                                                                               \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
      began_steady = std::chrono::steady_clock::now();                                                                                                                                                                                                                                                                         \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      end_utc = (d).to_time_point();                                                                                                                                                                                                                                                                                           \
      _timeout = windows_nt_kernel::from_timepoint(end_utc);                                                                                                                                                                                                                                                                   \
    }                                                                                                                                                                                                                                                                                                                          \
    timeout = &_timeout;                                                                                                                                                                                                                                                                                                       \
  }

#define LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d)                                                                                                                                                                                                                                                                                    \
  if((d) && (d).steady)                                                                                                                                                                                                                                                                                                        \
  {                                                                                                                                                                                                                                                                                                                            \
    std::chrono::nanoseconds ns = std::chrono::duration_cast<std::chrono::nanoseconds>((began_steady + std::chrono::nanoseconds((d).nsecs)) - std::chrono::steady_clock::now());                                                                                                                                               \
    if(ns.count() < 0)                                                                                                                                                                                                                                                                                                         \
      _timeout.QuadPart = 0;                                                                                                                                                                                                                                                                                                   \
    else                                                                                                                                                                                                                                                                                                                       \
      _timeout.QuadPart = ns.count() / -100;                                                                                                                                                                                                                                                                                   \
  }

#define LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d)                                                                                                                                                                                                                                                                                  \
                                                                                                                                                                                                                                                                                                                               \
  if(d)                                                                                                                                                                                                                                                                                                                        \
                                                                                                                                                                                                                                                                                                                               \
  {                                                                                                                                                                                                                                                                                                                            \
    if((d).steady)                                                                                                                                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::steady_clock::now() >= (began_steady + std::chrono::nanoseconds((d).nsecs)))                                                                                                                                                                                                                             \
        return errc::timed_out;                                                                                                                                                                                                                                                                                                \
    }                                                                                                                                                                                                                                                                                                                          \
    else                                                                                                                                                                                                                                                                                                                       \
    {                                                                                                                                                                                                                                                                                                                          \
      if(std::chrono::system_clock::now() >= end_utc)                                                                                                                                                                                                                                                                          \
        return errc::timed_out;                                                                                                                                                                                                                                                                                                \
    }                                                                                                                                                                                                                                                                                                                          \
  }
#endif

// Initialise an IO_STATUS_BLOCK for later wait operations
inline windows_nt_kernel::IO_STATUS_BLOCK make_iostatus() noexcept
{
  windows_nt_kernel::IO_STATUS_BLOCK isb{};
  memset(&isb, 0, sizeof(isb));
  isb.Status = -1;
  return isb;
}

// Wait for an overlapped handle to complete a specific operation
inline NTSTATUS ntwait(HANDLE h, windows_nt_kernel::IO_STATUS_BLOCK &isb, const deadline &d) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  if(isb.Status != 0x103 /*STATUS_PENDING*/)
  {
    assert(isb.Status != -1);  // should not call ntwait() if operation failed to start
    // He's done already (or never started)
    return isb.Status;
  }
  LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
  do  // needs to be a do, not while in order to flip auto reset event objects etc.
  {
    LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);
    NTSTATUS ntstat = NtWaitForSingleObject(h, 0u, timeout);
    if(STATUS_TIMEOUT == ntstat)
    {
      // If he'll still pending, we must cancel the i/o
      if(isb.Status == 0x103 /*STATUS_PENDING*/)
      {
        // There is a race between the above check and CancelIoEx
        if(!CancelIoEx(h, (LPOVERLAPPED) &isb) && ERROR_NOT_FOUND != GetLastError())
        {
          LLFIO_LOG_FATAL(&h, "Failed to cancel i/o due to deadline timeout");
          abort();
        }
        isb.Status = STATUS_TIMEOUT;
      }
      return isb.Status;
    }
  } while(isb.Status == 0x103 /*STATUS_PENDING*/);
  return isb.Status;
}
inline NTSTATUS ntwait(HANDLE h, OVERLAPPED &ol, const deadline &d) noexcept
{
  return ntwait(h, reinterpret_cast<windows_nt_kernel::IO_STATUS_BLOCK &>(ol), d);
}

// Sleep the thread until some deadline
inline bool ntsleep(const deadline &d, bool return_on_alert = false) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
  alignas(8) LARGE_INTEGER infinity{};
  infinity.QuadPart = INT64_MIN;
  for(;;)
  {
    LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);
    // Pump alerts and APCs
    NTSTATUS ntstat = NtDelayExecution(1u, timeout != nullptr ? timeout : &infinity);
    (void) ntstat;
    if((d).steady)
    {
      if(std::chrono::steady_clock::now() >= (began_steady + std::chrono::nanoseconds((d).nsecs)))
      {
        return false;
      }
    }
    else
    {
      if(std::chrono::system_clock::now() >= end_utc)
      {
        return false;
      }
    }
    if(return_on_alert)
    {
      return true;
    }
  }
}


// Utility routine for building an ACCESS_MASK from a handle::mode
inline result<ACCESS_MASK> access_mask_from_handle_mode(native_handle_type &nativeh, handle::mode _mode, handle::flag flags)
{
  ACCESS_MASK access = SYNCHRONIZE;  // appears that this is the bare minimum for the call to succeed
  switch(_mode)
  {
  case handle::mode::unchanged:
    return errc::invalid_argument;
  case handle::mode::none:
    break;
  case handle::mode::attr_read:
    access |= FILE_READ_ATTRIBUTES | STANDARD_RIGHTS_READ;
    break;
  case handle::mode::attr_write:
    access |= DELETE | FILE_READ_ATTRIBUTES | STANDARD_RIGHTS_READ | FILE_WRITE_ATTRIBUTES | STANDARD_RIGHTS_WRITE;
    break;
  case handle::mode::read:
    access |= GENERIC_READ;
    nativeh.behaviour |= native_handle_type::disposition::seekable | native_handle_type::disposition::readable;
    break;
  case handle::mode::write:
    access |= DELETE | GENERIC_WRITE | GENERIC_READ;
    nativeh.behaviour |= native_handle_type::disposition::seekable | native_handle_type::disposition::readable | native_handle_type::disposition::writable;
    break;
  case handle::mode::append:
    access |= DELETE | GENERIC_READ | FILE_WRITE_ATTRIBUTES | STANDARD_RIGHTS_WRITE | FILE_APPEND_DATA;
    nativeh.behaviour |= native_handle_type::disposition::writable | native_handle_type::disposition::append_only;
    break;
  }
  // Should we allow unlink on close if opening read only? I guess if Windows allows it, so should we.
  if(flags & handle::flag::unlink_on_first_close)
  {
    access |= DELETE;
  }
  return access;
}
inline result<DWORD> attributes_from_handle_caching_and_flags(native_handle_type &nativeh, handle::caching _caching, handle::flag flags)
{
  DWORD attribs = 0;
  if(flags & handle::flag::multiplexable)
  {
    attribs |= FILE_FLAG_OVERLAPPED;
    nativeh.behaviour |= native_handle_type::disposition::nonblocking;
  }
  switch(_caching)
  {
  case handle::caching::unchanged:
    return errc::invalid_argument;
  case handle::caching::none:
    attribs |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    nativeh.behaviour |= native_handle_type::disposition::aligned_io;
    break;
  case handle::caching::only_metadata:
    attribs |= FILE_FLAG_NO_BUFFERING;
    nativeh.behaviour |= native_handle_type::disposition::aligned_io;
    break;
  case handle::caching::reads:
  case handle::caching::reads_and_metadata:
    attribs |= FILE_FLAG_WRITE_THROUGH;
    break;
  case handle::caching::all:
  case handle::caching::safety_barriers:
    break;
  case handle::caching::temporary:
    attribs |= FILE_ATTRIBUTE_TEMPORARY;
    break;
  }
  if(flags & handle::flag::unlink_on_first_close)
  {
    attribs |= FILE_FLAG_DELETE_ON_CLOSE;
  }
  if(flags & handle::flag::disable_prefetching)
  {
    attribs |= FILE_FLAG_RANDOM_ACCESS;
  }
  if(flags & handle::flag::maximum_prefetching)
  {
    attribs |= FILE_FLAG_SEQUENTIAL_SCAN;
  }
  return attribs;
}
inline result<DWORD> ntflags_from_handle_caching_and_flags(native_handle_type &nativeh, handle::caching _caching, handle::flag flags)
{
  DWORD ntflags = 0;
  if(flags & handle::flag::multiplexable)
  {
    nativeh.behaviour |= native_handle_type::disposition::nonblocking;
  }
  else
  {
    ntflags |= 0x20 /*FILE_SYNCHRONOUS_IO_NONALERT*/;
  }
  switch(_caching)
  {
  case handle::caching::unchanged:
    return errc::invalid_argument;
  case handle::caching::none:
    ntflags |= 0x00000008 /*FILE_NO_INTERMEDIATE_BUFFERING*/ | 0x00000002 /*FILE_WRITE_THROUGH*/;
    nativeh.behaviour |= native_handle_type::disposition::aligned_io;
    break;
  case handle::caching::only_metadata:
    ntflags |= 0x00000008 /*FILE_NO_INTERMEDIATE_BUFFERING*/;
    nativeh.behaviour |= native_handle_type::disposition::aligned_io;
    break;
  case handle::caching::reads:
  case handle::caching::reads_and_metadata:
    ntflags |= 0x00000002 /*FILE_WRITE_THROUGH*/;
    break;
  case handle::caching::all:
  case handle::caching::safety_barriers:
    break;
  case handle::caching::temporary:
    // should be handled by attributes_from_handle_caching_and_flags
    break;
  }
  if(flags & handle::flag::unlink_on_first_close)
  {
    ntflags |= 0x00001000 /*FILE_DELETE_ON_CLOSE*/;
  }
  if(flags & handle::flag::disable_prefetching)
  {
    ntflags |= 0x00000800 /*FILE_RANDOM_ACCESS*/;
  }
  if(flags & handle::flag::maximum_prefetching)
  {
    ntflags |= 0x00000004 /*FILE_SEQUENTIAL_ONLY*/;
  }
  return ntflags;
}

inline result<void> do_clone_handle(native_handle_type &dest, const native_handle_type &src, handle::mode mode_, handle::caching caching_, handle::flag _flags, bool isdir = false) noexcept
{
  // Fast path
  if(mode_ == handle::mode::unchanged && caching_ == handle::caching::unchanged)
  {
    dest.behaviour = src.behaviour;
    if(DuplicateHandle(GetCurrentProcess(), src.h, GetCurrentProcess(), &dest.h, 0, 0, DUPLICATE_SAME_ACCESS) == 0)
    {
      return win32_error();
    }
    return success();
  }
  // Slow path
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  dest.behaviour |= src.behaviour & ~127U;  // propagate type of handle only
  DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  OUTCOME_TRY(access, access_mask_from_handle_mode(dest, mode_, _flags));
  OUTCOME_TRYV(attributes_from_handle_caching_and_flags(dest, caching_, _flags));
  if(isdir)
  {
    /* It is super important that we remove the DELETE permission for directories as otherwise relative renames
    will always fail due to an unfortunate design choice by Microsoft.
    */
    access &= ~DELETE;
  }
  OUTCOME_TRY(ntflags, ntflags_from_handle_caching_and_flags(dest, caching_, _flags));
  ntflags |= 0x040 /*FILE_NON_DIRECTORY_FILE*/;  // do not open a directory
  OBJECT_ATTRIBUTES oa{};
  memset(&oa, 0, sizeof(oa));
  oa.Length = sizeof(OBJECT_ATTRIBUTES);
  // It is entirely undocumented that this is how you clone a file handle with new privs
  UNICODE_STRING _path{};
  memset(&_path, 0, sizeof(_path));
  oa.ObjectName = &_path;
  oa.RootDirectory = src.h;
  IO_STATUS_BLOCK isb = make_iostatus();
  NTSTATUS ntstat = NtOpenFile(&dest.h, access, &oa, &isb, fileshare, ntflags);
  if(STATUS_PENDING == ntstat)
  {
    ntstat = ntwait(dest.h, isb, deadline());
  }
  if(ntstat < 0)
  {
    return ntkernel_error(ntstat);
  }
  return success();
}

inline result<void> do_lock_file_range(native_handle_type &_v, handle::extent_type offset, handle::extent_type bytes, bool exclusive, deadline d) noexcept
{
  if(d && d.nsecs > 0 && !_v.is_nonblocking())
  {
    return errc::not_supported;
  }
  DWORD flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  if(d && (d.nsecs == 0u))
  {
    flags |= LOCKFILE_FAIL_IMMEDIATELY;
  }
  LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
  OVERLAPPED ol{};
  memset(&ol, 0, sizeof(ol));
  ol.Internal = static_cast<ULONG_PTR>(-1);
  ol.OffsetHigh = (offset >> 32) & 0xffffffff;
  ol.Offset = offset & 0xffffffff;
  DWORD bytes_high = bytes == 0u ? MAXDWORD : static_cast<DWORD>((bytes >> 32) & 0xffffffff);
  DWORD bytes_low = bytes == 0u ? MAXDWORD : static_cast<DWORD>(bytes & 0xffffffff);
  if(LockFileEx(_v.h, flags, 0, bytes_low, bytes_high, &ol) == 0)
  {
    if(ERROR_LOCK_VIOLATION == GetLastError() && d && (d.nsecs == 0u))
    {
      return errc::timed_out;
    }
    if(ERROR_IO_PENDING != GetLastError())
    {
      return win32_error();
    }
  }
  // If handle is overlapped, wait for completion of each i/o.
  if(_v.is_nonblocking())
  {
    if(STATUS_TIMEOUT == ntwait(_v.h, ol, d))
    {
      return errc::timed_out;
    }
    // It seems the NT kernel is guilty of casting bugs sometimes
    ol.Internal = ol.Internal & 0xffffffff;
    if(ol.Internal != 0)
    {
      return ntkernel_error(static_cast<NTSTATUS>(ol.Internal));
    }
  }
  return success();
}

inline void do_unlock_file_range(native_handle_type &_v, handle::extent_type offset, handle::extent_type bytes) noexcept
{
  OVERLAPPED ol{};
  memset(&ol, 0, sizeof(ol));
  ol.Internal = static_cast<ULONG_PTR>(-1);
  ol.OffsetHigh = (offset >> 32) & 0xffffffff;
  ol.Offset = offset & 0xffffffff;
  DWORD bytes_high = bytes == 0u ? MAXDWORD : static_cast<DWORD>((bytes >> 32) & 0xffffffff);
  DWORD bytes_low = bytes == 0u ? MAXDWORD : static_cast<DWORD>(bytes & 0xffffffff);
  if(UnlockFileEx(_v.h, 0, bytes_low, bytes_high, &ol) == 0)
  {
    if(ERROR_IO_PENDING != GetLastError())
    {
      auto ret = win32_error();
      (void) ret;
      LLFIO_LOG_FATAL(_v.h, "io_handle::unlock() failed");
      std::terminate();
    }
  }
  // If handle is overlapped, wait for completion of each i/o.
  if(_v.is_nonblocking())
  {
    ntwait(_v.h, ol, deadline());
    if(ol.Internal != 0)
    {
      // It seems the NT kernel is guilty of casting bugs sometimes
      ol.Internal = ol.Internal & 0xffffffff;
      auto ret = ntkernel_error(static_cast<NTSTATUS>(ol.Internal));
      (void) ret;
      LLFIO_LOG_FATAL(_v.h, "io_handle::unlock() failed");
      std::terminate();
    }
  }
}

/* Our own custom CreateFileW() implementation.

The Win32 CreateFileW() implementation is unfortunately slow. It also, very annoyingly,
maps STATUS_DELETE_PENDING onto ERROR_ACCESS_DENIED instead of to ERROR_DELETE_PENDING
which means our file open routines return EACCES, which is useless to us for detecting
when we are trying to open files being deleted. We therefore reimplement CreateFileW()
with a non-broken version.

Another bug fix is that CREATE_ALWAYS is mapped to FILE_OPEN_IF on Win32, which does
not replace the inode if it already exists. We map it correctly to FILE_SUPERSEDE.

This edition does pretty much the same as the Win32 edition, minus support for file
templates and lpFileName being anything but a file path.
*/
inline HANDLE CreateFileW_(_In_ LPCTSTR lpFileName, _In_ DWORD dwDesiredAccess, _In_ DWORD dwShareMode, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes, _In_ DWORD dwCreationDisposition, _In_ DWORD dwFlagsAndAttributes, _In_opt_ HANDLE hTemplateFile, bool forcedir = false)
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  if((lpFileName == nullptr) || (lpFileName[0] == 0u))
  {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return INVALID_HANDLE_VALUE;  // NOLINT
  }
  if((hTemplateFile != nullptr) || (lstrcmpW(lpFileName, L"CONIN$") == 0) || (lstrcmpW(lpFileName, L"CONOUT$") == 0))
  {
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;  // NOLINT
  }
  switch(dwCreationDisposition)
  {
  case CREATE_NEW:
    dwCreationDisposition = FILE_CREATE;
    break;
  case CREATE_ALWAYS:
    dwCreationDisposition = FILE_SUPERSEDE;  // WARNING: Win32 uses FILE_OVERWRITE_IF;
    break;
  case OPEN_EXISTING:
    dwCreationDisposition = FILE_OPEN;
    break;
  case OPEN_ALWAYS:
    dwCreationDisposition = FILE_OPEN_IF;
    break;
  case TRUNCATE_EXISTING:
    dwCreationDisposition = FILE_OVERWRITE;
    break;
  default:
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;  // NOLINT
  }
  ULONG flags = 0;
  if((dwFlagsAndAttributes & FILE_FLAG_OVERLAPPED) == 0u)
  {
    flags |= FILE_SYNCHRONOUS_IO_NONALERT;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_WRITE_THROUGH) != 0u)
  {
    flags |= FILE_WRITE_THROUGH;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_NO_BUFFERING) != 0u)
  {
    flags |= FILE_NO_INTERMEDIATE_BUFFERING;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_RANDOM_ACCESS) != 0u)
  {
    flags |= FILE_RANDOM_ACCESS;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_SEQUENTIAL_SCAN) != 0u)
  {
    flags |= FILE_SEQUENTIAL_ONLY;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE) != 0u)
  {
    flags |= FILE_DELETE_ON_CLOSE;
    dwDesiredAccess |= DELETE;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) != 0u)
  {
    if((dwDesiredAccess & GENERIC_ALL) != 0u)
    {
      flags |= FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REMOTE_INSTANCE;
    }
    else
    {
      if((dwDesiredAccess & GENERIC_READ) != 0u)
      {
        flags |= FILE_OPEN_FOR_BACKUP_INTENT;
      }
      if((dwDesiredAccess & GENERIC_WRITE) != 0u)
      {
        flags |= FILE_OPEN_REMOTE_INSTANCE;
      }
    }
    if(forcedir)
    {
      flags |= FILE_DIRECTORY_FILE;
    }
  }
  else
  {
    flags |= FILE_NON_DIRECTORY_FILE;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_OPEN_REPARSE_POINT) != 0u)
  {
    flags |= FILE_OPEN_REPARSE_POINT;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_OPEN_NO_RECALL) != 0u)
  {
    flags |= FILE_OPEN_NO_RECALL;
  }

  UNICODE_STRING NtPath{};
  if(RtlDosPathNameToNtPathName_U(lpFileName, &NtPath, nullptr, nullptr) == 0u)
  {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;  // NOLINT
  }
  auto unntpath = undoer([&NtPath] {
    if(HeapFree(GetProcessHeap(), 0, NtPath.Buffer) == 0)
    {
      abort();
    }
  });

  OBJECT_ATTRIBUTES ObjectAttributes{};
  InitializeObjectAttributes(&ObjectAttributes, &NtPath, 0, nullptr, nullptr);
  if(lpSecurityAttributes != nullptr)
  {
    if(lpSecurityAttributes->bInheritHandle != 0)
    {
      ObjectAttributes.Attributes |= OBJ_INHERIT;
    }
    ObjectAttributes.SecurityDescriptor = lpSecurityAttributes->lpSecurityDescriptor;
  }
  if((dwFlagsAndAttributes & FILE_FLAG_POSIX_SEMANTICS) == 0u)
  {
    ObjectAttributes.Attributes |= OBJ_CASE_INSENSITIVE;
  }

  HANDLE ret = INVALID_HANDLE_VALUE;  // NOLINT
  IO_STATUS_BLOCK isb = make_iostatus();
  dwFlagsAndAttributes &= ~0xfff80000;
  NTSTATUS ntstat = NtCreateFile(&ret, dwDesiredAccess, &ObjectAttributes, &isb, nullptr, dwFlagsAndAttributes, dwShareMode, dwCreationDisposition, flags, nullptr, 0);
  if(STATUS_SUCCESS == ntstat)
  {
    return ret;
  }

  win32_error_from_nt_status(ntstat);
  if(STATUS_DELETE_PENDING == ntstat)
  {
    SetLastError(ERROR_DELETE_PENDING);
  }
  return INVALID_HANDLE_VALUE;  // NOLINT
}

// Detect if we are running under SUID or SGID
inline bool running_under_suid_gid()
{
  HANDLE processtoken;
  if(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &processtoken) == 0)
  {
    abort();
  }
  auto unprocesstoken = undoer([&processtoken] { CloseHandle(processtoken); });
  (void) unprocesstoken;
  DWORD written;
  char buffer1[1024], buffer2[1024];
  if(GetTokenInformation(processtoken, TokenUser, buffer1, sizeof(buffer1), &written) == 0)
  {
    abort();
  }
  if(GetTokenInformation(processtoken, TokenOwner, buffer2, sizeof(buffer2), &written) == 0)
  {
    abort();
  }
  auto *tu = reinterpret_cast<TOKEN_USER *>(buffer1);
  auto *to = reinterpret_cast<TOKEN_OWNER *>(buffer2);
  return EqualSid(tu->User.Sid, to->Owner) == 0;
}

LLFIO_V2_NAMESPACE_END

#endif
