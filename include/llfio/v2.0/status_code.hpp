/* LLFIO error handling
(C) 2018 Niall Douglas <http://www.nedproductions.biz/> (24 commits)
File Created: June 2018


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

#ifndef LLFIO_STATUS_CODE_HPP
#define LLFIO_STATUS_CODE_HPP

#include "logging.hpp"

/* The SG14 status code implementation is quite profoundly different to the
error code implementation. In the error code implementation, std::error_code
is fixed by the standard library, so we wrap it with extra metadata into
an error_info type. error_info then constructs off a code and a code domain
tag.

Status code, on the other hand, is templated and is designed for custom
domains which can set arbitrary payloads. So we define custom domains and
status codes for LLFIO with these combinations:

- win32_error{ DWORD }
- ntkernel_error{ LONG }
- posix_error{ int }
- generic_error{ errc }

Each of these is a separate LLFIO custom status code domain. We also define
an erased form of these custom domains, and that is typedefed to
file_io_error_domain<intptr_t>::value_type.

This design ensure that LLFIO can be configured into either std-based error
handling or SG14 experimental status code handling. It defaults to the latter
as that (a) enables safe header only LLFIO on Windows (b) produces better codegen
(c) drags in far fewer STL headers.
*/

#if LLFIO_EXPERIMENTAL_STATUS_CODE

// Bring in a result implementation based on status_code
#include "outcome/experimental/status_result.hpp"
#include "outcome/try.hpp"
// Bring in status code utility
#include "outcome/experimental/status-code/include/system_code_from_exception.hpp"
#include "outcome/experimental/coroutine_support.hpp"
#if !defined(LLFIO_ENABLE_COROUTINES) && defined(OUTCOME_FOUND_COROUTINE_HEADER)
#define LLFIO_ENABLE_COROUTINES 1
#endif

LLFIO_V2_NAMESPACE_BEGIN

#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO

namespace detail
{
  template <class T> struct file_io_error_value_type
  {
    //! \brief The type of code
    T sc{};

    // The id of the thread where this failure occurred
    uint32_t _thread_id{0};
    // The TLS path store entry
    uint16_t _tls_path_id1{static_cast<uint16_t>(-1)}, _tls_path_id2{static_cast<uint16_t>(-1)};
    // The id of the relevant log entry in the LLFIO log (if logging enabled)
    size_t _log_id{static_cast<size_t>(-1)};

    //! Default construction
    file_io_error_value_type() = default;

    //! Implicitly constructs an instance
    constexpr inline file_io_error_value_type(T _sc)
        : sc(_sc)
    {
    }  // NOLINT

    //! Compares to a T
    constexpr bool operator==(const T &b) const noexcept { return sc == b; }
  };
}  // namespace detail

template <class BaseStatusCodeDomain> class file_io_error_domain;

LLFIO_V2_NAMESPACE_END

// Inject a mixin for our custom status codes
SYSTEM_ERROR2_NAMESPACE_BEGIN
namespace mixins
{
  template <class Base, class BaseStatusCodeDomain> struct mixin<Base, ::LLFIO_V2_NAMESPACE::file_io_error_domain<BaseStatusCodeDomain>> : public Base
  {
    using Base::Base;

    //! Retrieve the paths associated with this failure
    std::pair<const char *, const char *> _paths() const noexcept
    {
      if(QUICKCPPLIB_NAMESPACE::utils::thread::this_thread_id() == this->value()._thread_id)
      {
        auto &tls = ::LLFIO_V2_NAMESPACE::detail::tls_errored_results();
        const char *path1 = tls.get(this->value()._tls_path_id1);
        const char *path2 = tls.get(this->value()._tls_path_id2);
        return {path1, path2};
      }
      return {};
    }
    //! Retrieve the first path associated with this failure
    ::LLFIO_V2_NAMESPACE::filesystem::path path1() const
    {
      if(QUICKCPPLIB_NAMESPACE::utils::thread::this_thread_id() == this->value()._thread_id)
      {
        auto &tls = ::LLFIO_V2_NAMESPACE::detail::tls_errored_results();
        const char *path1 = tls.get(this->value()._tls_path_id1);
        if(path1 != nullptr)
        {
          return ::LLFIO_V2_NAMESPACE::filesystem::path(path1);
        }
      }
      return {};
    }
    //! Retrieve the second path associated with this failure
    ::LLFIO_V2_NAMESPACE::filesystem::path path2() const
    {
      if(QUICKCPPLIB_NAMESPACE::utils::thread::this_thread_id() == this->value()._thread_id)
      {
        auto &tls = ::LLFIO_V2_NAMESPACE::detail::tls_errored_results();
        const char *path2 = tls.get(this->value()._tls_path_id2);
        if(path2 != nullptr)
        {
          return ::LLFIO_V2_NAMESPACE::filesystem::path(path2);
        }
      }
      return {};
    }
  };
}  // namespace mixins
SYSTEM_ERROR2_NAMESPACE_END

LLFIO_V2_NAMESPACE_BEGIN

/*! \class file_io_error_domain
\brief The SG14 status code domain for errors in LLFIO.
*/
template <class BaseStatusCodeDomain> class file_io_error_domain : public BaseStatusCodeDomain
{
  friend class SYSTEM_ERROR2_NAMESPACE::status_code<file_io_error_domain>;
  using _base = BaseStatusCodeDomain;

public:
  using string_ref = typename BaseStatusCodeDomain::string_ref;
  using atomic_refcounted_string_ref = typename BaseStatusCodeDomain::atomic_refcounted_string_ref;

  //! \brief The value type of errors in LLFIO
  using value_type = detail::file_io_error_value_type<typename _base::value_type>;

  file_io_error_domain() = default;
  file_io_error_domain(const file_io_error_domain &) = default;
  file_io_error_domain(file_io_error_domain &&) = default;
  file_io_error_domain &operator=(const file_io_error_domain &) = default;
  file_io_error_domain &operator=(file_io_error_domain &&) = default;
  ~file_io_error_domain() = default;

protected:
  virtual inline string_ref _do_message(const SYSTEM_ERROR2_NAMESPACE::status_code<void> &code) const noexcept override final
  {
    assert(code.domain() == *this);
    const auto &v = static_cast<const SYSTEM_ERROR2_NAMESPACE::status_code<file_io_error_domain> &>(code);  // NOLINT
    // Get the paths for this failure, if any, using the mixins from above
    auto paths = v._paths();
    // Get the base message for this failure
    auto msg = _base::_do_message(code);
    if(paths.first == nullptr && paths.second == nullptr)
    {
      return msg;
    }
    std::string ret;
    try
    {
      ret = msg.c_str();
      if(paths.first != nullptr)
      {
        ret.append(" [path1 = ");
        ret.append(paths.first);
        if(paths.second != nullptr)
        {
          ret.append(", path2 = ");
          ret.append(paths.second);
        }
        ret.append("]");
      }
#if LLFIO_LOGGING_LEVEL >= 2
      if(v.value()._log_id != static_cast<uint32_t>(-1))
      {
        if(log().valid(v.value()._log_id))
        {
          ret.append(" [location = ");
          ret.append(location(log()[v.value()._log_id]));
          ret.append("]");
        }
      }
#endif
    }
    catch(...)
    {
      return string_ref("Failed to retrieve message for status code");
    }
    char *p = (char *) malloc(ret.size() + 1);
    if(p == nullptr)
    {
      return string_ref("Failed to allocate memory to store error string");
    }
    memcpy(p, ret.c_str(), ret.size() + 1);
    return atomic_refcounted_string_ref(p, ret.size());
  }
};

#else   // LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
template <class BaseStatusCodeDomain> using file_io_error_domain = BaseStatusCodeDomain;
#endif  // LLFIO_DISABLE_PATHS_IN_FAILURE_INFO

namespace detail
{
  using file_io_error_domain_value_system_code = file_io_error_value_type<SYSTEM_ERROR2_NAMESPACE::system_code::value_type>;
}

//! An erased status code
using file_io_error = SYSTEM_ERROR2_NAMESPACE::errored_status_code<SYSTEM_ERROR2_NAMESPACE::erased<detail::file_io_error_domain_value_system_code>>;


template <class T> using result = OUTCOME_V2_NAMESPACE::experimental::status_result<T, file_io_error>;
using OUTCOME_V2_NAMESPACE::failure;
using OUTCOME_V2_NAMESPACE::in_place_type;
using OUTCOME_V2_NAMESPACE::success;
#if defined(LLFIO_ENABLE_COROUTINES)
template <class T> using atomic_eager = OUTCOME_V2_NAMESPACE::experimental::awaitables::atomic_eager<T>;
template <class T> using atomic_lazy = OUTCOME_V2_NAMESPACE::experimental::awaitables::atomic_lazy<T>;
template <class T> using eager = OUTCOME_V2_NAMESPACE::experimental::awaitables::eager<T>;
template <class T> using lazy = OUTCOME_V2_NAMESPACE::experimental::awaitables::lazy<T>;
template <class T = void> using coroutine_handle = OUTCOME_V2_NAMESPACE::awaitables::coroutine_handle<T>;
template <class... Args> using coroutine_traits = OUTCOME_V2_NAMESPACE::awaitables::coroutine_traits<Args...>;
using OUTCOME_V2_NAMESPACE::awaitables::suspend_always;
using OUTCOME_V2_NAMESPACE::awaitables::suspend_never;
#endif

//! Choose an errc implementation
using SYSTEM_ERROR2_NAMESPACE::errc;

//! Helper for constructing an error code from an errc
inline file_io_error generic_error(errc c);
#ifndef _WIN32
//! Helper for constructing an error code from a POSIX errno
inline file_io_error posix_error(int c = errno);
#else
//! Helper for constructing an error code from a DWORD
inline file_io_error win32_error(SYSTEM_ERROR2_NAMESPACE::win32::DWORD c = SYSTEM_ERROR2_NAMESPACE::win32::GetLastError());
//! Helper for constructing an error code from a NTSTATUS
inline file_io_error ntkernel_error(SYSTEM_ERROR2_NAMESPACE::win32::NTSTATUS c);
#endif

namespace detail
{
  inline std::ostream &operator<<(std::ostream &s, const file_io_error &v) { return s << "llfio::file_io_error(" << v.message().c_str() << ")"; }
}  // namespace detail
inline file_io_error error_from_exception(std::exception_ptr &&ep = std::current_exception(), SYSTEM_ERROR2_NAMESPACE::system_code not_matched = errc::resource_unavailable_try_again) noexcept
{
  return SYSTEM_ERROR2_NAMESPACE::system_code_from_exception(static_cast<std::exception_ptr &&>(ep), static_cast<SYSTEM_ERROR2_NAMESPACE::system_code &&>(not_matched));
}

LLFIO_V2_NAMESPACE_END


#else  // LLFIO_EXPERIMENTAL_STATUS_CODE


// Bring in a result implementation based on std::error_code
#include "outcome/result.hpp"
#include "outcome/try.hpp"
#include "outcome/utils.hpp"
#include "outcome/coroutine_support.hpp"
#if !defined(LLFIO_ENABLE_COROUTINES) && defined(OUTCOME_FOUND_COROUTINE_HEADER)
#define LLFIO_ENABLE_COROUTINES 1
#endif

LLFIO_V2_NAMESPACE_BEGIN

namespace detail
{
  template <class Dest, class Src> inline void fill_failure_info(Dest &dest, const Src &src);
  template <class Src> inline void append_path_info(Src &src, std::string &ret);
}  // namespace detail

struct error_info;
inline std::error_code make_error_code(error_info ei);

/*! \struct error_info
\brief The cause of the failure of an operation in LLFIO.
*/
struct error_info
{
  friend inline std::error_code make_error_code(error_info ei);
  template <class Src> friend inline void detail::append_path_info(Src &src, std::string &ret);
  template <class Dest, class Src> friend inline void detail::fill_failure_info(Dest &dest, const Src &src);

private:
  // The error code for the failure
  std::error_code ec;

#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
  // The id of the thread where this failure occurred
  uint32_t _thread_id{0};
  // The TLS path store entry
  uint16_t _tls_path_id1{static_cast<uint16_t>(-1)}, _tls_path_id2{static_cast<uint16_t>(-1)};
  // The id of the relevant log entry in the LLFIO log (if logging enabled)
  size_t _log_id{static_cast<size_t>(-1)};

public:
#endif

  //! Default constructor
  error_info() = default;
  // Explicit construction from an error code
  explicit inline error_info(std::error_code _ec);  // NOLINT
  /* NOTE TO SELF: The error_info constructor implementation is in handle.hpp as we need that
  defined before we can do useful logging.
  */
  //! Implicit construct from an error condition enum
  OUTCOME_TEMPLATE(class ErrorCondEnum)
  OUTCOME_TREQUIRES(OUTCOME_TPRED(std::is_error_condition_enum<ErrorCondEnum>::value))
  error_info(ErrorCondEnum &&v)  // NOLINT
      : error_info(make_error_code(std::forward<ErrorCondEnum>(v)))
  {
  }

  //! Retrieve the value of the error code
  int value() const noexcept { return ec.value(); }
  //! Retrieve any first path associated with this failure. Note this only works if called from the same thread as where the failure occurred.
  inline filesystem::path path1() const
  {
#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
    if(QUICKCPPLIB_NAMESPACE::utils::thread::this_thread_id() == _thread_id)
    {
      auto &tls = detail::tls_errored_results();
      const char *path1 = tls.get(_tls_path_id1);
      if(path1 != nullptr)
      {
        return filesystem::path(path1);
      }
    }
#endif
    return {};
  }
  //! Retrieve any second path associated with this failure. Note this only works if called from the same thread as where the failure occurred.
  inline filesystem::path path2() const
  {
#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
    if(QUICKCPPLIB_NAMESPACE::utils::thread::this_thread_id() == _thread_id)
    {
      auto &tls = detail::tls_errored_results();
      const char *path2 = tls.get(_tls_path_id2);
      if(path2 != nullptr)
      {
        return filesystem::path(path2);
      }
    }
#endif
    return {};
  }
  //! Retrieve a descriptive message for this failure, possibly with paths and stack backtraces. Extra detail only appears if called from the same thread as where the failure occurred.
  inline std::string message() const
  {
    std::string ret(ec.message());
#ifndef LLFIO_DISABLE_PATHS_IN_FAILURE_INFO
    detail::append_path_info(*this, ret);
#endif
    return ret;
  }
  /*! Throw this failure as a C++ exception. Firstly if the error code matches any of the standard
  C++ exception types e.g. `bad_alloc`, we throw those types using the string from `message()`
  where possible. We then will throw an `error` exception type.
  */
  inline void throw_exception() const;
};
inline bool operator==(const error_info &a, const error_info &b)
{
  return make_error_code(a) == make_error_code(b);
}
inline bool operator!=(const error_info &a, const error_info &b)
{
  return make_error_code(a) != make_error_code(b);
}
OUTCOME_TEMPLATE(class ErrorCondEnum)
OUTCOME_TREQUIRES(OUTCOME_TPRED(std::is_error_condition_enum<ErrorCondEnum>::value))
inline bool operator==(const error_info &a, const ErrorCondEnum &b)
{
  auto _a = make_error_code(a);
  auto _b = std::error_condition(b);
#ifndef _WIN32
  // Looks like libstdc++ doesn't map system category to generic category, which is a bug
  if(_a.category() == std::system_category() && _b.category() == std::generic_category() && _a.value() == static_cast<int>(b))
    return true;
#endif
  return _a == _b;
}
OUTCOME_TEMPLATE(class ErrorCondEnum)
OUTCOME_TREQUIRES(OUTCOME_TPRED(std::is_error_condition_enum<ErrorCondEnum>::value))
inline bool operator==(const ErrorCondEnum &a, const error_info &b)
{
  auto _a = std::error_condition(a);
  auto _b = make_error_code(b);
#ifndef _WIN32
  // Looks like libstdc++ doesn't map system category to generic category, which is a bug
  if(_a.category() == std::generic_category() && _b.category() == std::system_category() && _b.value() == static_cast<int>(a))
    return true;
#endif
  return _a == _b;
}
OUTCOME_TEMPLATE(class ErrorCondEnum)
OUTCOME_TREQUIRES(OUTCOME_TPRED(std::is_error_condition_enum<ErrorCondEnum>::value))
inline bool operator!=(const error_info &a, const ErrorCondEnum &b)
{
  auto _a = make_error_code(a);
  auto _b = std::error_condition(b);
#ifndef _WIN32
  // Looks like libstdc++ doesn't map system category to generic category, which is a bug
  if(_a.category() == std::system_category() && _b.category() == std::generic_category() && _a.value() == static_cast<int>(b))
    return false;
#endif
  return _a != _b;
}
OUTCOME_TEMPLATE(class ErrorCondEnum)
OUTCOME_TREQUIRES(OUTCOME_TPRED(std::is_error_condition_enum<ErrorCondEnum>::value))
inline bool operator!=(const ErrorCondEnum &a, const error_info &b)
{
  auto _a = std::error_condition(a);
  auto _b = make_error_code(b);
#ifndef _WIN32
  // Looks like libstdc++ doesn't map system category to generic category, which is a bug
  if(_a.category() == std::generic_category() && _b.category() == std::system_category() && _b.value() == static_cast<int>(a))
    return false;
#endif
  return _a != _b;
}
#ifndef NDEBUG
// Is trivial in all ways, except default constructibility
static_assert(std::is_trivially_copyable<error_info>::value, "error_info is not a trivially copyable!");
#endif
inline std::ostream &operator<<(std::ostream &s, const error_info &v)
{
  if(make_error_code(v))
  {
    return s << "llfio::error_info(" << v.message() << ")";
  }
  return s << "llfio::error_info(null)";
}
// Tell Outcome that error_info is to be treated as an error_code
inline std::error_code make_error_code(error_info ei)
{
  return ei.ec;
}
// Tell Outcome to call error_info::throw_exception() on no-value observation
inline void outcome_throw_as_system_error_with_payload(const error_info &ei)
{
  ei.throw_exception();
}

/*! \class error
\brief The exception type synthesised and thrown when an `llfio::result` or `llfio::outcome` is no-value observed.
*/
class error : public filesystem::filesystem_error
{
public:
  error_info ei;

  //! Constructs from an error_info
  explicit error(error_info _ei)
      : filesystem::filesystem_error(_ei.message(), _ei.path1(), _ei.path2(), make_error_code(_ei))
      , ei(_ei)
  {
  }
};

inline void error_info::throw_exception() const
{
  std::string msg;
  try
  {
    msg = message();
  }
  catch(...)
  {
  }
  OUTCOME_V2_NAMESPACE::try_throw_std_exception_from_error(ec, msg);
  throw error(*this);
}

template <class T> using result = OUTCOME_V2_NAMESPACE::result<T, error_info>;
using OUTCOME_V2_NAMESPACE::failure;
using OUTCOME_V2_NAMESPACE::success;
inline error_info error_from_exception(std::exception_ptr &&ep = std::current_exception(), std::error_code not_matched = std::make_error_code(std::errc::resource_unavailable_try_again)) noexcept
{
  return error_info(OUTCOME_V2_NAMESPACE::error_from_exception(std::move(ep), not_matched));
}
using OUTCOME_V2_NAMESPACE::in_place_type;
#if defined(LLFIO_ENABLE_COROUTINES)
template <class T> using atomic_eager = OUTCOME_V2_NAMESPACE::awaitables::atomic_eager<T>;
template <class T> using atomic_lazy = OUTCOME_V2_NAMESPACE::awaitables::atomic_lazy<T>;
template <class T> using eager = OUTCOME_V2_NAMESPACE::awaitables::eager<T>;
template <class T> using lazy = OUTCOME_V2_NAMESPACE::awaitables::lazy<T>;
template <class T = void> using coroutine_handle = OUTCOME_V2_NAMESPACE::awaitables::coroutine_handle<T>;
template <class... Args> using coroutine_traits = OUTCOME_V2_NAMESPACE::awaitables::coroutine_traits<Args...>;
using OUTCOME_V2_NAMESPACE::awaitables::suspend_always;
using OUTCOME_V2_NAMESPACE::awaitables::suspend_never;
#endif

static_assert(OUTCOME_V2_NAMESPACE::trait::is_error_code_available_v<error_info>, "error_info is not detected to be an error code");

//! Choose an errc implementation
using std::errc;

//! Helper for constructing an error info from an errc
inline error_info generic_error(errc c)
{
  return error_info(make_error_code(c));
}
#ifndef _WIN32
//! Helper for constructing an error info from a POSIX errno
inline error_info posix_error(int c = errno)
{
  return error_info(std::error_code(c, std::system_category()));
}
#endif

LLFIO_V2_NAMESPACE_END

#endif  // LLFIO_EXPERIMENTAL_STATUS_CODE


#endif
