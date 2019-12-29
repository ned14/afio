/* Configures LLFIO
(C) 2015-2018 Niall Douglas <http://www.nedproductions.biz/> (24 commits)
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

#ifndef LLFIO_CONFIG_HPP
#define LLFIO_CONFIG_HPP

//#include <iostream>
//#define LLFIO_LOG_TO_OSTREAM std::cerr
//#define LLFIO_LOGGING_LEVEL 1
//#define LLFIO_DISABLE_PATHS_IN_FAILURE_INFO

//! \file config.hpp Configures a compiler environment for LLFIO header and source code

//! \defgroup config Configuration macros

#if !defined(LLFIO_HEADERS_ONLY) && !defined(BOOST_ALL_DYN_LINK)
//! \brief Whether LLFIO is a headers only library. Defaults to 1 unless BOOST_ALL_DYN_LINK is defined. \ingroup config
#define LLFIO_HEADERS_ONLY 1
#endif

//! \def LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
//! \brief Define to not record the current handle's path in any failure info.
#if DOXYGEN_IS_IN_THE_HOUSE
#define LLFIO_DISABLE_PATHS_IN_FAILURE_INFO not defined
#endif

#if !defined(LLFIO_LOGGING_LEVEL)
//! \brief How much detail to log. 0=disabled, 1=fatal, 2=error, 3=warn, 4=info, 5=debug, 6=all.
//! Defaults to error level. \ingroup config
#ifdef NDEBUG
#define LLFIO_LOGGING_LEVEL 1  // fatal
#else
#define LLFIO_LOGGING_LEVEL 3  // warn
#endif
#endif

#ifndef LLFIO_LOG_TO_OSTREAM
#if !defined(NDEBUG) && !defined(LLFIO_DISABLE_LOG_TO_OSTREAM)
#include <iostream>  // for std::cerr
//! \brief Any `ostream` to also log to. If `NDEBUG` is not defined, `std::cerr` is the default.
#define LLFIO_LOG_TO_OSTREAM std::cerr
#endif
#endif

#if !defined(LLFIO_LOG_BACKTRACE_LEVELS)
//! \brief Bit mask of which log levels should be stack backtraced
//! which will slow those logs thirty fold or so. Defaults to (1U<<1U)|(1U<<2U)|(1U<<3U) i.e. stack backtrace
//! on fatal, error and warn logs. \ingroup config
#define LLFIO_LOG_BACKTRACE_LEVELS ((1U << 1U) | (1U << 2U) | (1U << 3U))
#endif

#if !defined(LLFIO_LOGGING_MEMORY)
#ifdef NDEBUG
#define LLFIO_LOGGING_MEMORY 4096
#else
//! \brief How much memory to use for the log.
//! Defaults to 4Kb if NDEBUG defined, else 1Mb. \ingroup config
#define LLFIO_LOGGING_MEMORY (1024 * 1024)
#endif
#endif

#if !defined(LLFIO_EXPERIMENTAL_STATUS_CODE)
//! \brief Whether to use SG14 experimental `status_code` instead of `std::error_code`
#define LLFIO_EXPERIMENTAL_STATUS_CODE 0
#endif


#if defined(_WIN32)
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#error _WIN32_WINNT must at least be set to Windows Vista for LLFIO to work
#endif
#if defined(NTDDI_VERSION) && NTDDI_VERSION < 0x06000000
#error NTDDI_VERSION must at least be set to Windows Vista for LLFIO to work
#endif
#endif

#ifdef __APPLE__
#define LLFIO_MISSING_PIOV 1
#endif

// Pull in detection of __MINGW64_VERSION_MAJOR
#ifdef __MINGW32__
#include <_mingw.h>
#endif

#include "quickcpplib/cpp_feature.h"

#ifndef __cpp_exceptions
#error LLFIO needs C++ exceptions to be turned on
#endif
#ifndef __cpp_alias_templates
#error LLFIO needs template alias support in the compiler
#endif
#ifndef __cpp_variadic_templates
#error LLFIO needs variadic template support in the compiler
#endif
#if __cpp_constexpr < 201304L && !defined(_MSC_VER)
#error LLFIO needs relaxed constexpr (C++ 14) support in the compiler
#endif
#ifndef __cpp_init_captures
#error LLFIO needs lambda init captures support in the compiler (C++ 14)
#endif
#ifndef __cpp_attributes
#error LLFIO needs attributes support in the compiler
#endif
#ifndef __cpp_variable_templates
#error LLFIO needs variable template support in the compiler
#endif
#ifndef __cpp_generic_lambdas
#error LLFIO needs generic lambda support in the compiler
#endif
#ifdef __has_include
// clang-format off
#if !__has_include(<filesystem>) && !__has_include(<experimental/filesystem>)
// clang-format on
#error LLFIO needs an implementation of the Filesystem TS in the standard library
#endif
#endif


#include "quickcpplib/import.h"

#if defined(LLFIO_UNSTABLE_VERSION) && !defined(LLFIO_DISABLE_ABI_PERMUTATION)
#include "../revision.hpp"
#define LLFIO_V2 (QUICKCPPLIB_BIND_NAMESPACE_VERSION(llfio_v2, LLFIO_PREVIOUS_COMMIT_UNIQUE))
#else
#define LLFIO_V2 (QUICKCPPLIB_BIND_NAMESPACE_VERSION(llfio_v2))
#endif
/*! \def LLFIO_V2
\ingroup config
\brief The namespace configuration of this LLFIO v2. Consists of a sequence
of bracketed tokens later fused by the preprocessor into namespace and C++ module names.
*/
#if DOXYGEN_IS_IN_THE_HOUSE
//! The LLFIO namespace
namespace llfio_v2_xxx
{
  //! Collection of file system based algorithms
  namespace algorithm
  {
  }
  //! YAML databaseable empirical testing of a storage's behaviour
  namespace storage_profile
  {
  }
  //! Utility routines often useful when using LLFIO
  namespace utils
  {
  }
}  // namespace llfio_v2_xxx
/*! \brief The namespace of this LLFIO v2 which will be some unknown inline
namespace starting with `v2_` inside the `boost::llfio` namespace.
\ingroup config
*/
#define LLFIO_V2_NAMESPACE llfio_v2_xxx
/*! \brief Expands into the appropriate namespace markup to enter the LLFIO v2 namespace.
\ingroup config
*/
#define LLFIO_V2_NAMESPACE_BEGIN                                                                                                                                                                                                                                                                                               \
  namespace llfio_v2_xxx                                                                                                                                                                                                                                                                                                       \
  {
/*! \brief Expands into the appropriate namespace markup to enter the C++ module
exported LLFIO v2 namespace.
\ingroup config
*/
#define LLFIO_V2_NAMESPACE_EXPORT_BEGIN                                                                                                                                                                                                                                                                                        \
  export namespace llfio_v2_xxx                                                                                                                                                                                                                                                                                                \
  {
/*! \brief Expands into the appropriate namespace markup to exit the LLFIO v2 namespace.
\ingroup config
*/
#define LLFIO_V2_NAMESPACE_END }
#elif defined(GENERATING_LLFIO_MODULE_INTERFACE)
#define LLFIO_V2_NAMESPACE QUICKCPPLIB_BIND_NAMESPACE(LLFIO_V2)
#define LLFIO_V2_NAMESPACE_BEGIN QUICKCPPLIB_BIND_NAMESPACE_BEGIN(LLFIO_V2)
#define LLFIO_V2_NAMESPACE_EXPORT_BEGIN QUICKCPPLIB_BIND_NAMESPACE_EXPORT_BEGIN(LLFIO_V2)
#define LLFIO_V2_NAMESPACE_END QUICKCPPLIB_BIND_NAMESPACE_END(LLFIO_V2)
#else
#define LLFIO_V2_NAMESPACE QUICKCPPLIB_BIND_NAMESPACE(LLFIO_V2)
#define LLFIO_V2_NAMESPACE_BEGIN QUICKCPPLIB_BIND_NAMESPACE_BEGIN(LLFIO_V2)
#define LLFIO_V2_NAMESPACE_EXPORT_BEGIN QUICKCPPLIB_BIND_NAMESPACE_BEGIN(LLFIO_V2)
#define LLFIO_V2_NAMESPACE_END QUICKCPPLIB_BIND_NAMESPACE_END(LLFIO_V2)
#endif

LLFIO_V2_NAMESPACE_BEGIN
class handle;
class file_handle;
LLFIO_V2_NAMESPACE_END

// Bring in the Boost-lite macros
#include "quickcpplib/config.hpp"
#if LLFIO_LOGGING_LEVEL
#include "quickcpplib/ringbuffer_log.hpp"
#include "quickcpplib/utils/thread.hpp"
#endif
// Bring in filesystem
#if defined(__has_include)
// clang-format off
#if __has_include(<filesystem>) && (__cplusplus >= 201700 || _HAS_CXX17)
#include <filesystem>
LLFIO_V2_NAMESPACE_BEGIN
namespace filesystem = std::filesystem;
LLFIO_V2_NAMESPACE_END
#elif __has_include(<experimental/filesystem>) && (!defined(_MSC_VER) || _MSC_VER < 1923)  // C++ 14 filesystem support was dropped in VS2019 16.3
#include <experimental/filesystem>
LLFIO_V2_NAMESPACE_BEGIN
namespace filesystem = std::experimental::filesystem;
LLFIO_V2_NAMESPACE_END
#elif __has_include(<filesystem>)
#if defined(_MSC_VER) && _MSC_VER >= 1923
#error MSVC dropped support for C++ 14 <filesystem> from VS2019 16.3 onwards. Please enable C++ 17 or later.
#endif
#include <filesystem>
LLFIO_V2_NAMESPACE_BEGIN
namespace filesystem = std::filesystem;
LLFIO_V2_NAMESPACE_END
#endif
#elif __PCPP_ALWAYS_TRUE__
#include <experimental/filesystem>
LLFIO_V2_NAMESPACE_BEGIN
namespace filesystem = std::experimental::filesystem;
LLFIO_V2_NAMESPACE_END
// clang-format on
#elif defined(_MSC_VER)
#include <filesystem>
LLFIO_V2_NAMESPACE_BEGIN
namespace filesystem = std::experimental::filesystem;
LLFIO_V2_NAMESPACE_END
#else
#error No <filesystem> implementation found
#endif
LLFIO_V2_NAMESPACE_BEGIN
struct path_hasher
{
  size_t operator()(const filesystem::path &p) const { return std::hash<filesystem::path::string_type>()(p.native()); }
};
LLFIO_V2_NAMESPACE_END
#include <ctime>  // for struct timespec


// Configure LLFIO_DECL
#if(defined(LLFIO_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)) && !defined(LLFIO_STATIC_LINK)

#if defined(LLFIO_SOURCE)
#define LLFIO_DECL QUICKCPPLIB_SYMBOL_EXPORT
#define LLFIO_BUILD_DLL
#else
#define LLFIO_DECL QUICKCPPLIB_SYMBOL_IMPORT
#endif
#else
#define LLFIO_DECL
#endif  // building a shared library

// Configure LLFIO_THREAD_LOCAL
#ifndef LLFIO_THREAD_LOCAL_IS_CXX11
#define LLFIO_THREAD_LOCAL_IS_CXX11 QUICKCPPLIB_THREAD_LOCAL_IS_CXX11
#endif
#ifndef LLFIO_THREAD_LOCAL
#define LLFIO_THREAD_LOCAL QUICKCPPLIB_THREAD_LOCAL
#endif
#ifndef LLFIO_NODISCARD
#define LLFIO_NODISCARD QUICKCPPLIB_NODISCARD
#endif
#ifndef LLFIO_TEMPLATE
#define LLFIO_TEMPLATE(...) QUICKCPPLIB_TEMPLATE(__VA_ARGS__)
#endif
#ifndef LLFIO_TREQUIRES
#define LLFIO_TREQUIRES(...) QUICKCPPLIB_TREQUIRES(__VA_ARGS__)
#endif
#ifndef LLFIO_TEXPR
#define LLFIO_TEXPR(...) QUICKCPPLIB_TEXPR(__VA_ARGS__)
#endif
#ifndef LLFIO_TPRED
#define LLFIO_TPRED(...) QUICKCPPLIB_TPRED(__VA_ARGS__)
#endif
#ifndef LLFIO_REQUIRES
#define LLFIO_REQUIRES(...) QUICKCPPLIB_REQUIRES(__VA_ARGS__)
#endif

// A unique identifier generating macro
#define LLFIO_GLUE2(x, y) x##y
#define LLFIO_GLUE(x, y) LLFIO_GLUE2(x, y)
#define LLFIO_UNIQUE_NAME LLFIO_GLUE(__t, __COUNTER__)

// Used to tag functions which need to be made free by the AST tool
#ifndef LLFIO_MAKE_FREE_FUNCTION
#if 0  //__cplusplus >= 201700  // makes annoying warnings
#define LLFIO_MAKE_FREE_FUNCTION [[llfio::make_free_function]]
#else
#define LLFIO_MAKE_FREE_FUNCTION
#endif
#endif

// Bring in bitfields
#include "quickcpplib/bitfield.hpp"
// Bring in scoped undo
#include "quickcpplib/scoped_undo.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using QUICKCPPLIB_NAMESPACE::scoped_undo::undoer;
LLFIO_V2_NAMESPACE_END
// Bring in a spinlock
#include "quickcpplib/spinlock.hpp"
LLFIO_V2_NAMESPACE_BEGIN
namespace spinlocks = QUICKCPPLIB_NAMESPACE::configurable_spinlock;
LLFIO_V2_NAMESPACE_END
// Bring in a span implementation
#include "quickcpplib/span.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::span;
LLFIO_V2_NAMESPACE_END
// Bring in an optional implementation
#include "quickcpplib/optional.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::optional;
LLFIO_V2_NAMESPACE_END
// Bring in a byte implementation
#include "quickcpplib/byte.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using QUICKCPPLIB_NAMESPACE::byte::byte;
using QUICKCPPLIB_NAMESPACE::byte::to_byte;
LLFIO_V2_NAMESPACE_END
// Bring in a string_view implementation
#include "quickcpplib/string_view.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::string_view;
LLFIO_V2_NAMESPACE_END
// Bring in a function_ptr implementation
#include "quickcpplib/function_ptr.hpp"
LLFIO_V2_NAMESPACE_BEGIN
template <class F, size_t callable_storage_bytes = 32 - sizeof(uintptr_t)> using function_ptr = QUICKCPPLIB_NAMESPACE::function_ptr::function_ptr<F, callable_storage_bytes>;
using QUICKCPPLIB_NAMESPACE::function_ptr::emplace_function_ptr;
using QUICKCPPLIB_NAMESPACE::function_ptr::emplace_function_ptr_nothrow;
using QUICKCPPLIB_NAMESPACE::function_ptr::make_function_ptr;
using QUICKCPPLIB_NAMESPACE::function_ptr::make_function_ptr_nothrow;
LLFIO_V2_NAMESPACE_END
// Bring in an ensure_flushes implementation
#include "quickcpplib/mem_flush_loads_stores.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::mem_flush_loads_stores;
LLFIO_V2_NAMESPACE_END
// Bring in a start_lifetime_as and launder implementation
#include "quickcpplib/start_lifetime_as.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::start_lifetime_as;
LLFIO_V2_NAMESPACE_END
// Bring in a detach_cast implementation
#include "quickcpplib/detach_cast.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::detach_cast;
LLFIO_V2_NAMESPACE_END
// Bring in an in_place_detach implementation
#include "quickcpplib/in_place_detach_attach.hpp"
LLFIO_V2_NAMESPACE_BEGIN
using namespace QUICKCPPLIB_NAMESPACE::in_place_attach_detach;
using QUICKCPPLIB_NAMESPACE::in_place_attach_detach::in_place_attach;
using QUICKCPPLIB_NAMESPACE::in_place_attach_detach::in_place_detach;
LLFIO_V2_NAMESPACE_END


LLFIO_V2_NAMESPACE_BEGIN

namespace detail
{
  // Used to cast an unknown input to some unsigned integer
  LLFIO_TEMPLATE(class T, class U)
  LLFIO_TREQUIRES(LLFIO_TPRED(std::is_unsigned<T>::value && !std::is_same<std::decay_t<U>, std::nullptr_t>::value))
  inline T unsigned_integer_cast(U &&v) { return static_cast<T>(v); }
  LLFIO_TEMPLATE(class T)
  LLFIO_TREQUIRES(LLFIO_TPRED(std::is_unsigned<T>::value))
  inline T unsigned_integer_cast(std::nullptr_t /* unused */) { return static_cast<T>(0); }
  LLFIO_TEMPLATE(class T, class U)
  LLFIO_TREQUIRES(LLFIO_TPRED(std::is_unsigned<T>::value))
  inline T unsigned_integer_cast(U *v) { return static_cast<T>(reinterpret_cast<uintptr_t>(v)); }
}  // namespace detail

LLFIO_V2_NAMESPACE_END


LLFIO_V2_NAMESPACE_BEGIN

namespace detail
{
  // A move only capable lightweight std::function, as std::function can't handle move only callables
  template <class F> class function_ptr;
  template <class R, class... Args> class function_ptr<R(Args...)>
  {
    struct function_ptr_storage
    {
      function_ptr_storage() = default;
      function_ptr_storage(const function_ptr_storage &) = delete;
      function_ptr_storage(function_ptr_storage &&) = delete;
      function_ptr_storage &operator=(const function_ptr_storage &) = delete;
      function_ptr_storage &operator=(function_ptr_storage &&) = delete;
      virtual ~function_ptr_storage() = default;
      virtual R operator()(Args &&... args) = 0;
    };
    template <class U> struct function_ptr_storage_impl : public function_ptr_storage
    {
      U c;
      template <class... Args2>
      constexpr explicit function_ptr_storage_impl(Args2 &&... args)
          : c(std::forward<Args2>(args)...)
      {
      }
      R operator()(Args &&... args) final { return c(std::move(args)...); }
    };
    function_ptr_storage *ptr;
    template <class U> struct emplace_t
    {
    };
    template <class U, class V> friend inline function_ptr<U> make_function_ptr(V &&f);  // NOLINT
    template <class U>
    explicit function_ptr(std::nullptr_t, U &&f)
        : ptr(new function_ptr_storage_impl<typename std::decay<U>::type>(std::forward<U>(f)))
    {
    }
    template <class R_, class U, class... Args2> friend inline function_ptr<R_> emplace_function_ptr(Args2 &&... args);  // NOLINT
    template <class U, class... Args2>
    explicit function_ptr(emplace_t<U> /*unused*/, Args2 &&... args)
        : ptr(new function_ptr_storage_impl<U>(std::forward<Args2>(args)...))
    {
    }

  public:
    constexpr function_ptr() noexcept
        : ptr(nullptr)
    {
    }
    constexpr explicit function_ptr(function_ptr_storage *p) noexcept
        : ptr(p)
    {
    }
    constexpr function_ptr(function_ptr &&o) noexcept
        : ptr(o.ptr)
    {
      o.ptr = nullptr;
    }
    function_ptr &operator=(function_ptr &&o) noexcept
    {
      delete ptr;
      ptr = o.ptr;
      o.ptr = nullptr;
      return *this;
    }
    function_ptr(const function_ptr &) = delete;
    function_ptr &operator=(const function_ptr &) = delete;
    ~function_ptr() { delete ptr; }
    explicit constexpr operator bool() const noexcept { return !!ptr; }
    constexpr R operator()(Args... args) const { return (*ptr)(std::move(args)...); }
    constexpr function_ptr_storage *get() noexcept { return ptr; }
    constexpr void reset(function_ptr_storage *p = nullptr) noexcept
    {
      delete ptr;
      ptr = p;
    }
    constexpr function_ptr_storage *release() noexcept
    {
      auto p = ptr;
      ptr = nullptr;
      return p;
    }
  };
  template <class R, class U> inline function_ptr<R> make_function_ptr(U &&f) { return function_ptr<R>(nullptr, std::forward<U>(f)); }
  template <class R, class U, class... Args> inline function_ptr<R> emplace_function_ptr(Args &&... args) { return function_ptr<R>(typename function_ptr<R>::template emplace_t<U>(), std::forward<Args>(args)...); }
}  // namespace detail

// Native handle support
namespace win
{
  using handle = void *;
  using dword = unsigned long;
}  // namespace win

LLFIO_V2_NAMESPACE_END


#if 0
///////////////////////////////////////////////////////////////////////////////
//  Auto library naming
#if !defined(LLFIO_SOURCE) && !defined(BOOST_ALL_NO_LIB) && !defined(LLFIO_NO_LIB) && !LLFIO_STANDALONE && !LLFIO_HEADERS_ONLY

#define BOOST_LIB_NAME boost_llfio

// tell the auto-link code to select a dll when required:
#if defined(BOOST_ALL_DYN_LINK) || defined(LLFIO_DYN_LINK)
#define BOOST_DYN_LINK
#endif

#include <boost/config/auto_link.hpp>

#endif  // auto-linking disabled
#endif

//#define BOOST_THREAD_VERSION 4
//#define BOOST_THREAD_PROVIDES_VARIADIC_THREAD
//#define BOOST_THREAD_DONT_PROVIDE_FUTURE
//#define BOOST_THREAD_PROVIDES_SIGNATURE_PACKAGED_TASK
#if LLFIO_HEADERS_ONLY == 1 && !defined(LLFIO_SOURCE)
/*! \brief Expands into the appropriate markup to declare an `extern`
function exported from the LLFIO DLL if not building headers only.
\ingroup config
*/
#define LLFIO_HEADERS_ONLY_FUNC_SPEC inline
/*! \brief Expands into the appropriate markup to declare a class member
function exported from the LLFIO DLL if not building headers only.
\ingroup config
*/
#define LLFIO_HEADERS_ONLY_MEMFUNC_SPEC inline
/*! \brief Expands into the appropriate markup to declare a virtual class member
function exported from the LLFIO DLL if not building headers only.
\ingroup config
*/
#define LLFIO_HEADERS_ONLY_VIRTUAL_SPEC inline virtual
#else
#define LLFIO_HEADERS_ONLY_FUNC_SPEC extern LLFIO_DECL
#define LLFIO_HEADERS_ONLY_MEMFUNC_SPEC
#define LLFIO_HEADERS_ONLY_VIRTUAL_SPEC virtual
#endif

#endif
