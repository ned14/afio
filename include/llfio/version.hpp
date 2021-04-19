//! \file version.hpp Controls the version of LLFIO for cmake, shared library and C++ namespace mangling
#undef LLFIO_VERSION_MAJOR
#undef LLFIO_VERSION_MINOR
#undef LLFIO_VERSION_PATCH
#undef LLFIO_VERSION_REVISION
#undef LLFIO_VERSION_GLUE2
#undef LLFIO_VERSION_GLUE
#undef LLFIO_HEADERS_VERSION
#undef LLFIO_NAMESPACE_VERSION

//! \brief Major version for cmake and DLL version stamping \ingroup config
#define LLFIO_VERSION_MAJOR 2
//! \brief Minor version for cmake and DLL version stamping \ingroup config
#define LLFIO_VERSION_MINOR 0
//! \brief Patch version for cmake and DLL version stamping \ingroup config
#define LLFIO_VERSION_PATCH 0
//! \brief Revision version for cmake and DLL version stamping \ingroup config
#define LLFIO_VERSION_REVISION 0

//! \brief Defined between stable releases of LLFIO. It means the inline namespace
//! will be permuted per-commit to ensure ABI uniqueness. \ingroup config
#define LLFIO_UNSTABLE_VERSION

#define LLFIO_VERSION_GLUE2(a, b, c) a##b##c
#define LLFIO_VERSION_GLUE(a, b, c) LLFIO_VERSION_GLUE2(a, b, c)
#define LLFIO_NAMESPACE_VERSION LLFIO_VERSION_GLUE(LLFIO_VERSION_MAJOR, _, LLFIO_VERSION_MINOR)

#if defined(_MSC_VER) && !defined(__clang__) && (!defined(_MSVC_TRADITIONAL) || _MSVC_TRADITIONAL)
#define LLFIO_HEADERS_VERSION LLFIO_VERSION_GLUE(LLFIO_VERSION_MAJOR, ., LLFIO_VERSION_MINOR)
#else
#define LLFIO_HEADERS_VERSION LLFIO_VERSION_MAJOR.LLFIO_VERSION_MINOR
#endif
//! \brief The namespace LLFIO_V2_NAMESPACE::v ## LLFIO_NAMESPACE_VERSION
#define LLFIO_NAMESPACE_VERSION LLFIO_VERSION_GLUE(LLFIO_VERSION_MAJOR, _, LLFIO_VERSION_MINOR)
