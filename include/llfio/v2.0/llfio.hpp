//! \file v2.0/llfio.hpp The master *versioned* LLFIO include file. All version specific LLFIO consuming libraries should include this header only.

#undef LLFIO_VERSION_MAJOR
#undef LLFIO_VERSION_MINOR
#undef LLFIO_VERSION_PATCH
// Remove any previously defined versioning
#undef LLFIO_VERSION_REVISION
#undef LLFIO_VERSION_GLUE2
#undef LLFIO_VERSION_GLUE
#undef LLFIO_HEADERS_VERSION
#undef LLFIO_NAMESPACE_VERSION
#undef LLFIO_MODULE_NAME

#define LLFIO_VERSION_GLUE2(a, b, c) a##b##c
#define LLFIO_VERSION_GLUE(a, b, c) LLFIO_VERSION_GLUE2(a, b, c)

// Hard coded as this is a specific version
#define LLFIO_VERSION_MAJOR 2
#define LLFIO_VERSION_MINOR 0
#define LLFIO_VERSION_PATCH 0
#define LLFIO_VERSION_REVISION 0
//! \brief The namespace LLFIO_V2_NAMESPACE::v ## LLFIO_NAMESPACE_VERSION
#define LLFIO_NAMESPACE_VERSION LLFIO_VERSION_GLUE(LLFIO_VERSION_MAJOR, _, LLFIO_VERSION_MINOR)

#if defined(__cpp_modules) || defined(DOXYGEN_SHOULD_SKIP_THIS)
#if defined(_MSC_VER) && !defined(__clang__) && (!defined(_MSVC_TRADITIONAL) || _MSVC_TRADITIONAL)
//! \brief The LLFIO C++ module name
#define LLFIO_MODULE_NAME LLFIO_VERSION_GLUE(llfio_v, LLFIO_NAMESPACE_VERSION, )
#else
//! \brief The LLFIO C++ module name
#define LLFIO_MODULE_NAME LLFIO_VERSION_GLUE(llfio_v, LLFIO_NAMESPACE_VERSION, )
#endif
#endif

// If C++ Modules are on and we are not compiling the library,
// we are either generating the interface or importing
#if !defined(__cpp_modules) || defined(GENERATING_LLFIO_MODULE_INTERFACE) || LLFIO_DISABLE_CXX_MODULES
// C++ Modules not on, therefore include as usual
#define LLFIO_INCLUDE_ALL
#else
#if defined(GENERATING_LLFIO_MODULE_INTERFACE)
// We are generating this module's interface
#define QUICKCPPLIB_HEADERS_ONLY 0
#define LLFIO_HEADERS_ONLY 0
#define LLFIO_INCLUDE_ALL
#elif defined(LLFIO_SOURCE)
// We are implementing this module
#define LLFIO_INCLUDE_ALL
#else
// We are importing this module
import LLFIO_MODULE_NAME;
#undef LLFIO_INCLUDE_ALL
#endif
#endif

#ifdef LLFIO_INCLUDE_ALL

#include "config.hpp"

// Predeclare to keep the single header edition happy
#include "handle.hpp"
#include "stat.hpp"
#include "utils.hpp"

#include "directory_handle.hpp"
#ifndef LLFIO_EXCLUDE_DYNAMIC_THREAD_POOL_GROUP
#include "dynamic_thread_pool_group.hpp"
#endif
#include "fast_random_file_handle.hpp"
#include "file_handle.hpp"
#include "process_handle.hpp"
#include "statfs.hpp"
#ifdef LLFIO_INCLUDE_STORAGE_PROFILE
#include "storage_profile.hpp"
#endif
#include "symlink_handle.hpp"

#include "algorithm/clone.hpp"
#include "algorithm/contents.hpp"
#include "algorithm/handle_adapter/cached_parent.hpp"
#include "algorithm/reduce.hpp"
#include "algorithm/shared_fs_mutex/atomic_append.hpp"
#include "algorithm/shared_fs_mutex/byte_ranges.hpp"
#include "algorithm/shared_fs_mutex/lock_files.hpp"
#include "algorithm/shared_fs_mutex/safe_byte_ranges.hpp"
#include "algorithm/summarize.hpp"

#ifndef LLFIO_EXCLUDE_MAPPED_FILE_HANDLE
#include "algorithm/handle_adapter/xor.hpp"
#include "algorithm/shared_fs_mutex/memory_map.hpp"
#include "algorithm/trivial_vector.hpp"
#include "mapped.hpp"
#endif

#endif
