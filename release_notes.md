<center><table border="0" cellpadding="4">
<tr>
<td align="center"> <a href="https://github.com/ned14/llfio">LLFIO</a><br><a href="https://github.com/ned14/llfio">on GitHub</a> </td>
<td align="center"> <a href="http://my.cdash.org/index.php?project=Boost.AFIO">CTest summary</a><br><a href="http://my.cdash.org/index.php?project=Boost.AFIO">dashboard</a> </td>
<td align="center"> <a href="https://travis-ci.org/ned14/llfio">Linux and MacOS CI:</a><img src="https://travis-ci.org/ned14/llfio.svg?branch=master"/> </td>
<td align="center"> <a href="https://ci.appveyor.com/project/ned14/llfio/branch/master">Windows CI:</a><img src="https://ci.appveyor.com/api/projects/status/dfctqfap3kpx89om/branch/master?svg=true"/> </td>
<td align="center"> <a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-source-latest.tar.xz">Latest stable</a><br><a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-source-latest.tar.xz">sources</a> </td>
<td align="center"> <a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-linux64-latest.tgz">Latest stable</a><br><a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-linux64-latest.tgz">Ubuntu 18.04 x64 prebuilt</a> </td>
<td align="center"> <!--<a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-darwin-latest.tgz">Latest stable</a><br><a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-darwin64-latest.tgz">OS X x64 prebuilt</a>--> </td>
<td align="center"> <a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-win64-latest.zip">Latest stable</a><br/><a href="https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-win64-latest.zip">VS2017 x64 prebuilt</a> </td>
</tr>
</table></center>

Herein lies my proposed zero whole machine memory copy async file i/o and filesystem
library for the C++ standard, intended for storage devices with ~1 microsecond
4Kb transfer latencies and those supporting Storage Class Memory (SCM)/Direct Access
Storage (DAX). Its i/o overhead, including syscall overhead, has been benchmarked to
100 nanoseconds on Linux which corresponds to a theoretical maximum of 10M IOPS @ QD1,
approx 40Gb/sec per thread. It has particularly strong support for writing portable
filesystem algorithms which work well with directly mapped non-volatile storage such
as Intel Optane.

It is a complete rewrite after a Boost peer review in August 2015. Its github
source code repository lives at https://github.com/ned14/llfio.

- Portable to any conforming C++ 14 compiler with a working Filesystem TS in its STL.
- Will make use of any Concepts TS if you have them.
- Provides view adapters into the Ranges TS, so ready for STL2.
- Original error code is always preserved, even down to the original NT kernel error code if a NT kernel API was used.
    - Optional configuration based on [P1028](https://wg21.link/P1028) *SG14 status_code and standard error object
    for P0709 Zero-overhead deterministic exceptions*.
- Race free filesystem design used throughout (i.e. no TOCTOU).
- Zero malloc, zero exception throw and zero whole system memory copy design used throughout, even down to paths (which can hit 64Kb!).
- Comprehensive support for virtual and mapped memory of both SCM/DAX and page cached storage, including large, huge and super pages.
- Works very well with the C++ standard library, and is being proposed for standardisation into C++. See <a href="https://wg21.link/P1031">P1031 *Low level file i/o*</a>.
- Deep Coroutines TS support, simply `.co_read()` or `.co_write()` from within a coroutine.

\note Note that this code is of late alpha quality. It's quite reliable on Windows and Linux, but be careful when using it!

Examples of use:
<table width="100%" border="0" cellpadding="4">
<tr>
<td width="50%" valign="top">
\snippet use_cases.cpp sparse_array
</td>
<td width="50%" valign="top">
\snippet use_cases.cpp coroutine_write
</td>
</tr>
</table>

See https://github.com/ned14/llfio/blob/master/programs/fs-probe/fs_probe_results.yaml
for a database of latencies for various previously tested OS, filing systems and storage devices.

Todo list for already implemented parts: https://ned14.github.io/llfio/todo.html

<p>&nbsp;</p>
<center><span style="font-size: large; text-decoration: underline;">[Build instructions can found here](Build.md)</span></center>
<p>&nbsp;</p>

## v2 architecture and design implemented:

| NEW in v2 | Boost peer review feedback |     |
| --------- | -------------------------- | --- |
| ✔ | ✔ | Universal native handle/fd abstraction instead of `void *`.
| ✔ | ✔ | Perfectly/Ideally low memory (de)allocation per op (usually none).
| ✔ | ✔ | noexcept API throughout returning error_code for failure instead of throwing exceptions.
| ✔ | ✔ | LLFIO v1 handle type split into hierarchy of types:<ol><li>handle - provides open, close, get path, clone, set/unset append only, change caching, characteristics<li>fs_handle - handles with an inode number<li>path_handle - a race free anchor to a subset of the filesystem<li>directory_handle - enumerates the filesystem<li>io_handle - adds synchronous scatter-gather i/o, byte range locking<li>file_handle - adds open/create file, get and set maximum extent<li>async_file_handle - adds asynchronous scatter-gather i/o<li>mapped_file_handle - adds low latency memory mapped scatter-gather i/o</ol>
| ✔ | ✔ | Cancelable i/o (made possible thanks to dropping XP support).
| ✔ | ✔ | All shared_ptr usage removed as all use of multiple threads removed.
| ✔ | ✔ | Use of std::vector to transport scatter-gather sequences replaced with C++ 20 `span<>` borrowed views.
| ✔ | ✔ | Completion callbacks are now some arbitrary type `U&&` instead of a future continuation. Type erasure for its storage is bound into the one single memory allocation for everything needed to execute the op, and so therefore overhead is optimal.
| ✔ | ✔ | Filing system algorithms made generic and broken out into public `llfio::algorithms` template library (the LLFIO FTL).
| ✔ | ✔ | Abstraction of native handle management via bitfield specified "characteristics".
| ✔ |   | Storage profiles, a YAML database of behaviours of hardware, OS and filing system combinations.
| ✔ |   | Absolute and interval deadline timed i/o throughout (made possible thanks to dropping XP support).
| ✔ |   | Dependency on ASIO/Networking TS removed completely.
| ✔ |   | Four choices of algorithm implementing a shared filing system mutex.
| ✔ |   | Uses CMake, CTest, CDash and CPack with automatic usage of C++ Modules or precompiled headers where available.
| ✔ |   | Far more comprehensive memory map and virtual memory facilities.
| ✔ |   | Much more granular, micro level unit testing of individual functions.
| ✔ |   | Much more granular, micro level internal logging of every code path taken.
| ✔ |   | Path views used throughout, thus avoiding string copying and allocation in `std::filesystem::path`.
| ✔ |   | Paths are equally interpreted as UTF-8 on all platforms.
| ✔ |   | We never store nor retain a path, as they are inherently racy and are best avoided.
| ✔ | ✔ | Parent handle caching is hard coded in, it is now an optional user applied templated adapter class.

Todo:

| NEW in v2 | Boost peer review feedback |     |
| --------- | -------------------------- | --- |
| ✔ |   | clang AST assisted SWIG bindings for other languages.
| ✔ |   | Statistical tracking of operation latencies so realtime IOPS can be measured.



## Planned features implemented:

| NEW in v2 | Windows | POSIX |     |
| --------- | --------| ----- | --- |
| ✔ | ✔ | ✔ | Native handle cloning.
| ✔ (up from four) | ✔ | ✔ | Maximum possible (seven) forms of kernel caching.
|   | ✔ | ✔ | Absolute path open.
|   | ✔ | ✔ | Relative "anchored" path open enabling race free file system.
| ✔ | ✔ |   | Win32 path support (260 path limit).
|   | ✔ |   | NT kernel path support (32,768 path limit).
| ✔ | ✔ | ✔ | Synchronous universal scatter-gather i/o.
| ✔ (POSIX AIO support) | ✔ | ✔ | Asynchronous universal scatter-gather i/o.
| ✔ | ✔ | ✔ | i/o deadlines and cancellation.
|   | ✔ | ✔ | Retrieving and setting the current maximum extent (size) of an open file.
|   | ✔ | ✔ | Retrieving the current path of an open file irrespective of where it has been renamed to by third parties.
|   | ✔ | ✔ | statfs_t ported over from LLFIO v1.
|   | ✔ | ✔ | utils namespace ported over from LLFIO v1.
| ✔ | ✔ | ✔ | `shared_fs_mutex` shared/exclusive entities locking based on lock files
| ✔ | ✔ | ✔ | Byte range shared/exclusive locking.
| ✔ | ✔ | ✔ | `shared_fs_mutex` shared/exclusive entities locking based on byte ranges
| ✔ | ✔ | ✔ | `shared_fs_mutex` shared/exclusive entities locking based on atomic append
|   | ✔ | ✔ | Memory mapped files and virtual memory management (`section_handle`, `map_handle` and `mapped_file_handle`)
| ✔ | ✔ | ✔ | `shared_fs_mutex` shared/exclusive entities locking based on memory maps
| ✔ | ✔ | ✔ | Universal portable UTF-8 path views.
|   | ✔ | ✔ | "Hole punching" and hole enumeration ported over from LLFIO v1.
|   | ✔ | ✔ | Directory handles and very fast directory enumeration ported over from LLFIO v1.
| ✔ | ✔ | ✔ | `shared_fs_mutex` shared/exclusive entities locking based on safe byte ranges
|   | ✔ | ✔ | Set random or sequential i/o (prefetch).
| ✔ | ✔ | ✔ | i/o on `async_file_handle` is coroutines awaitable.
| ✔ | ✔ | ✔ | `llfio::algorithm::trivial_vector<T>` with constant time reallocation if `T` is trivially copyable.
|   | ✔ | ✔ | `symlink_handle`.
| ✔ | ✔ | ✔ | Large, huge and massive page size support for memory allocation and (POSIX only) file maps.

Todo thereafter in order of priority:

| NEW in v2 | Windows | POSIX |     |
| --------- | --------| ----- | --- |
| ✔ |   |   | A mechanism for writing a `stat_t` onto an inode.
| ✔ |   |   | Page allocator based on an index of linked list of free pages. See notes.
| ✔ |   |   | Optionally concurrent B+ tree index based on page allocator for key-value store.
| ✔ |   |   | Attributes extending `span<buffers_type>` with DMA colouring.
| ✔ |   |   | Coroutine generator for iterating a file's contents in DMA friendly way.
| ✔ |   |   | Ranges & Concurrency based reliable directory hierarchy deletion algorithm.
| ✔ |   |   | Ranges & Concurrency based reliable directory hierarchy copy algorithm.
| ✔ |   |   | Ranges & Concurrency based reliable directory hierarchy update (two and three way) algorithm.
| ✔ |   |   | Linux KAIO support for native non-blocking `O_DIRECT` i/o
| ✔ |   |   | `std::pmr::memory_resource` adapting a file backing if on C++ 17.
| ✔ |   |   | Extended attributes support.
| ✔ |   |   | Algorithm to replace all duplicate content with hard links.
| ✔ |   |   | Algorithm to figure out all paths for a hard linked inode.
| ✔ |   |   | Algorithm to compare two or three directory enumerations and give differences. Probably blocked on the Ranges TS.

Features possibly to be added after a Boost peer review:
- Directory change monitoring.
- Permissions support (ACLs).


<table width="100%" border="0" cellpadding="4">
<tr>
<th colspan="3">Why you might need LLFIO<hr></th>
</tr>
<tr>
<td valign="top" width="33%">
Manufacturer claimed 4Kb transfer latencies for the physical hardware:
- Spinning rust hard drive latency @ QD1: **9000us**
- SATA flash drive latency @ QD1: **800us**
- NVMe flash drive latency @ QD1: **300us**
- RTT UDP packet latency over a LAN: **60us**
- NVMe Optane drive latency @ QD1: **60us**
- `memcpy(4Kb)` latency: **5us** (main memory) to **1.3us** (L3 cache)
- RTT PCIe latency: **0.5us**
</td>
<td valign="top" width="33%">
100% read QD1 4Kb direct transfer latencies for the software with LLFIO:
- &lt; 99% spinning rust hard drive latency: Windows **187,231us** FreeBSD **9,836us** Linux **26,468us**
- &lt; 99% SATA flash drive latency: Windows **290us** Linux **158us**
- &lt; 99% NVMe drive latency: Windows **37us** FreeBSD **70us** Linux **30us**
</td>
<td valign="top" width="33%">
75% read 25% write QD4 4Kb direct transfer latencies for the software with LLFIO:
- &lt; 99% spinning rust hard drive latency: Windows **48,185us** FreeBSD **61,834us** Linux **104,507us**
- &lt; 99% SATA flash drive latency: Windows **1,812us** Linux **1,416us**
- &lt; 99% NVMe drive latency: Windows **50us** FreeBSD **143us** Linux **40us**
</td>
</tr>
</table>

Max bandwidth for the physical hardware:
- DDR4 2133: **30Gb/sec** (main memory)
- x4 PCIe 4.0: **7.5Gb/sec** (arrives end of 2017, the 2018 NVMe drives will use PCIe 4.0)
- x4 PCIe 3.0: **3.75Gb/sec** (985Mb/sec per PCIe lane)
- 2017 XPoint drive (x4 PCIe 3.0): **2.5Gb/sec**
- 2017 NVMe flash drive (x4 PCIe 3.0): **2Gb/sec**
- 10Gbit LAN: **1.2Gb/sec**
