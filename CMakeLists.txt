# LLFIO cmake
# (C) 2016-2021 Niall Douglas <http://www.nedproductions.biz/>
# File Created: June 2016
# 
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License in the accompanying file
# Licence.txt or at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# 
# 
# Distributed under the Boost Software License, Version 1.0.
#     (See accompanying file Licence.txt or copy at
#           http://www.boost.org/LICENSE_1_0.txt)

cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
include(cmake/QuickCppLibBootstrap.cmake)
include(QuickCppLibRequireOutOfSourceBuild)
include(QuickCppLibUtils)
include(QuickCppLibPolicies)

option(LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE "Whether to use SG14 status_code for failure handling" OFF)
option(LLFIO_ENABLE_DEPENDENCY_SMOKE_TEST "Whether to build executables which are smoke tests that LLFIO is fully working. Used by various package managers such as vcpkg." OFF)
option(LLFIO_ASSUME_CROSS_COMPILING "Whether to assume we are cross compiling. Normally automatically detected, but if automatic detection doesn't work, a working <filesystem> will not be found during cmake configure." OFF)
option(UNIT_TESTS_BUILD_ALL "Whether to run all of the unit test suite." OFF)
set(UNIT_TESTS_CXX_VERSION "latest" CACHE STRING "The version of C++ to use in the header-only unit tests")
if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD" OR APPLE)
  option(LLFIO_USE_LIBDISPATCH "Whether to use libdispatch/Grand Unified Dispatch (defaults on on BSD/Mac OS)" ON)
else()
  option(LLFIO_USE_LIBDISPATCH "Whether to use libdispatch/Grand Unified Dispatch (defaults on on BSD/Mac OS)" OFF)
endif()

ensure_git_subrepo("${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/ntkernel-error-category/include" "https://github.com/ned14/ntkernel-error-category.git")

# Parse the version we tell cmake directly from the version header file
ParseProjectVersionFromHpp("${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/version.hpp" VERSIONSTRING)
# Sets the usual PROJECT_NAME etc
project(llfio VERSION ${VERSIONSTRING} LANGUAGES C CXX)
# Also set a *cmake* namespace for this project
set(PROJECT_NAMESPACE)
# Setup this cmake environment for this project
include(QuickCppLibSetupProject)
if(NOT PROJECT_IS_DEPENDENCY)
  # This file should be updated with the last git SHA next commit
  UpdateRevisionHppFromGit("${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/revision.hpp")
endif()
# Find my library dependencies
find_quickcpplib_library(quickcpplib
  GIT_REPOSITORY "https://github.com/ned14/quickcpplib.git"
  REQUIRED
  IS_HEADER_ONLY
)
find_quickcpplib_library(outcome
  GIT_REPOSITORY "https://github.com/ned14/outcome.git"
  GIT_TAG "master"
  REQUIRED
  IS_HEADER_ONLY
)
if(WIN32)
  set(OLD_PROJECT_IS_DEPENDENCY ${PROJECT_IS_DEPENDENCY})
  set(PROJECT_IS_DEPENDENCY On)
  add_subdirectory("include/llfio/ntkernel-error-category" EXCLUDE_FROM_ALL)
  if(OLD_PROJECT_IS_DEPENDENCY)
    set(PROJECT_IS_DEPENDENCY ${OLD_PROJECT_IS_DEPENDENCY})
  else()
    set(PROJECT_IS_DEPENDENCY)
  endif()
endif()

# Make the standard static and shared libraries, and if supported by this compiler, C++ modules
# for both static and shared libraries as well. For the non-C++ module variants, makes the
# interface headers into precompiled headers. Only builds all of them if this is the topmost
# CMakeLists, else built only if something upstream is dependent on one of them.
include(QuickCppLibMakeLibrary)
# Make an interface only library so dependent CMakeLists can bring in this header-only library
include(QuickCppLibMakeHeaderOnlyLibrary)

# If we have concepts, enable those for both myself and all inclusions
apply_cxx_concepts_to(INTERFACE llfio_hl)
apply_cxx_concepts_to(PUBLIC llfio_sl llfio_dl)

# If we have coroutines, enable those for both myself and all inclusions
apply_cxx_coroutines_to(INTERFACE llfio_hl)
apply_cxx_coroutines_to(PUBLIC llfio_sl llfio_dl)

# Make preprocessed edition of this library target
if(NOT PROJECT_IS_DEPENDENCY)
  if(NOT PYTHONINTERP_FOUND)
    indented_message(WARNING "NOT rebuilding preprocessed edition of library due to python not being installed")
  elseif(FALSE)
    function(make_single_header target name)
      add_partial_preprocess(${target}
                            "${name}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/revision.hpp"
                            ${ARGN}
                            -I ../quickcpplib/include -I ../outcome/include
                            --passthru-defines --passthru-unfound-includes --passthru-unknown-exprs
                            --passthru-comments --line-directive --compress # --debug
                            -U QUICKCPPLIB_ENABLE_VALGRIND
                            -U DOXYGEN_SHOULD_SKIP_THIS -U DOXYGEN_IS_IN_THE_HOUSE
                            -U STANDARDESE_IS_IN_THE_HOUSE -U __cpp_modules
                            -D LLFIO_INCLUDE_ALL
                            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                            )
      if(NOT CMAKE_VERSION VERSION_LESS 3.3)
        add_dependencies(llfio_hl ${target})
      endif()
    endfunction()
    make_single_header(llfio_hl-pp
                       "${CMAKE_CURRENT_SOURCE_DIR}/single-header/llfio.hpp"
                       "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                       #-D QUICKCPPLIB_USE_STD_BYTE -D QUICKCPPLIB_USE_STD_OPTIONAL -D QUICKCPPLIB_USE_STD_SPAN
                       #-U gsl_COMPILER_MSVC_VERSION -U gsl_HAS_CPP0X -D gsl_CPLUSPLUS=201703 -D __cplusplus=201703
                       #-D LLFIO_LEAN_AND_MEAN -U _WIN32
                       #-D LLFIO_EXPERIMENTAL_STATUS_CODE=1
                       )
    make_single_header(llfio_hl-pp-abi
                       "${CMAKE_CURRENT_SOURCE_DIR}/single-header/abi.hpp"
                       "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                       -D LLFIO_EXPERIMENTAL_STATUS_CODE=1
                       -D LLFIO_DISABLE_ABI_PERMUTATION=1
                       -D OUTCOME_DISABLE_ABI_PERMUTATION=1
                       -D QUICKCPPLIB_DISABLE_ABI_PERMUTATION=1
                       -U LLFIO_UNSTABLE_VERSION
                       -U OUTCOME_UNSTABLE_VERSION)
  endif()

  # Create a custom doxygen generation target
  include(QuickCppLibMakeDoxygen)
endif()

# Set the standard definitions for these libraries and bring in the all_* helper functions
include(QuickCppLibApplyDefaultDefinitions)

# Set the C++ features this library requires
all_compile_features(PUBLIC
  # cxx_exceptions                        ## Annoyingly not supported by cmake 3.6
  cxx_alias_templates
  cxx_variadic_templates
  cxx_noexcept
  cxx_constexpr
  cxx_lambda_init_captures
  cxx_attributes
  cxx_generic_lambdas
)
if(NOT MSVC OR CMAKE_VERSION VERSION_GREATER 3.59)
  all_compile_features(PUBLIC
    cxx_variable_templates
  )
endif()
set(check_cxx_source_linkage_flags)
# If on VS2019 16.3 or later, or on Apple, we require C++ 17
if((MSVC AND MSVC_VERSION VERSION_GREATER_EQUAL 1923) OR APPLE)
  all_compile_features(PUBLIC
    cxx_std_17
  )
  if(NOT CMAKE_CXX_STANDARD)
    if(MSVC)
      set(check_cxx_source_linkage_flags /std:c++17)
    else()
      set(check_cxx_source_linkage_flags -std=c++17)
    endif()
  endif()
endif()
# Set the library dependencies this library has
all_link_libraries(PUBLIC Threads::Threads $<$<PLATFORM_ID:Linux>:rt>)
if(TARGET outcome::hl)
  all_link_libraries(PUBLIC outcome::hl)
endif()
if(TARGET quickcpplib::hl)
  all_link_libraries(PUBLIC quickcpplib::hl)
endif()

# Set the system dependencies this library has
include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)
function(check_cxx_source_linkage prog var)
  set(CMAKE_REQUIRED_FLAGS ${check_cxx_source_linkage_flags})
  if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /EHsc")
  endif()
  if(CMAKE_CXX_STANDARD)
    if(MSVC)
      set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /std:c++${CMAKE_CXX_STANDARD}")
    else()
      set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++${CMAKE_CXX_STANDARD}")
    endif()
  endif()
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL ${CMAKE_HOST_SYSTEM_PROCESSOR} AND NOT LLFIO_ASSUME_CROSS_COMPILING)
    check_cxx_source_runs("${prog}" ${var})
  else()
    check_cxx_source_compiles("${prog}" ${var})
  endif()
  set(${var} "${${var}}" PARENT_SCOPE)
endfunction()
# Do we have native <filesystem> that just works without any extra effort?
# We have to check if it runs, as binaries may link, but fail to run due to missing symbols
check_cxx_source_linkage("
#include <filesystem>
int main() {
  try { return std::filesystem::path(\"hi\").empty(); } catch(std::filesystem::filesystem_error) { return 1; }
}
" CXX_HAS_CXX17_FILESYSTEM)
if(NOT CXX_HAS_CXX17_FILESYSTEM)
  check_cxx_source_linkage("
#include <experimental/filesystem>
int main() {
  try { return std::experimental::filesystem::path(\"hi\").empty(); } catch(std::experimental::filesystem::filesystem_error) { return 1; }
}
" CXX_HAS_CXX_EXPERIMENTAL_FILESYSTEM)
endif()
if(NOT CXX_HAS_CXX17_FILESYSTEM AND NOT CXX_HAS_CXX_EXPERIMENTAL_FILESYSTEM)
  indented_message(STATUS "NOTE: Standard <filesystem> not found in the current compiler configuration (try forcing C++ 17 or later?), attempting to figure out what <experimental/filesystem> linker flags to use for this STL ...")
  # Are we on libstdc++ or libc++?
  check_cxx_source_compiles("
#include <iostream>
int main() {
#ifdef __GLIBCXX__
  std::cout << \"hi\";
  return 0;
#else
  return i am not a number;
#endif
}
" CXX_IS_USING_LIBSTDCXX)
  check_cxx_source_compiles("
#include <iostream>
int main() {
#ifdef _LIBCPP_VERSION
  std::cout << \"hi\";
  return 0;
#else
  return i am not a number;
#endif
}
" CXX_IS_USING_LIBCXX)
  if(NOT CXX_IS_USING_LIBSTDCXX AND NOT CXX_IS_USING_LIBCXX)
    indented_message(FATAL_ERROR "FATAL: <filesystem> is not available, and neither libstdc++ nor libc++ STLs will link a program")
  endif()
  set(stl_filesystem_link_flags)
  # If on libstdc++, we need to link in stdc++fs for experimental filesystem
  if(CXX_IS_USING_LIBSTDCXX)
    find_library(libstdcxx_stdcxxfs stdc++fs)
    if(libstdcxx_stdcxxfs MATCHES "NOTFOUND")
      set(libstdcxx_stdcxxfs -lstdc++fs)
    endif()
    all_link_libraries(PUBLIC ${libstdcxx_stdcxxfs})
    list(APPEND stl_filesystem_link_flags ${libstdcxx_stdcxxfs})
  endif()
  # If on libc++, we may need to link in c++fs or c++experimental for experimental filesystem
  if(CXX_IS_USING_LIBCXX)
    find_library(libcxx_cxxfs c++fs)
    find_library(libcxx_cxxexperimental c++experimental)
    if(libcxx_cxxfs MATCHES "NOTFOUND" AND libcxx_cxxexperimental MATCHES "NOTFOUND")
      # I guess default to forcing libc++experimental?
      set(libcxx_cxxexperimental -lc++experimental)
    endif()
    if(NOT libcxx_cxxfs MATCHES "NOTFOUND")
      all_link_libraries(PUBLIC ${libcxx_cxxfs})
      list(APPEND stl_filesystem_link_flags ${libcxx_cxxfs})
    endif()
    if(NOT libcxx_cxxexperimental MATCHES "NOTFOUND")
      all_link_libraries(PUBLIC ${libcxx_cxxexperimental})
      list(APPEND stl_filesystem_link_flags ${libcxx_cxxexperimental})
    endif()
    # Disable the irritating warnings
    all_compile_definitions(PUBLIC _LIBCPP_NO_EXPERIMENTAL_DEPRECATION_WARNING_FILESYSTEM=1)
  endif()
  function(check_stl_filesystem_link_flags)
    indented_message(STATUS "NOTE: Using STL link flags '${ARGN}'")
    set(CMAKE_REQUIRED_LIBRARIES ${ARGN})
    check_cxx_source_linkage("
#include <filesystem>
int main() {
  try { return std::filesystem::path(\"hi\").empty(); } catch(std::filesystem::filesystem_error) { return 1; }
}
" CXX_HAS_CXX_FILESYSTEM_AFTER_FLAGS)
    check_cxx_source_linkage("
#include <experimental/filesystem>
int main() {
  try { return std::experimental::filesystem::path(\"hi\").empty(); } catch(std::experimental::filesystem::filesystem_error) { return 1; }
}
" CXX_HAS_CXX_EXPERIMENTAL_FILESYSTEM_AFTER_FLAGS)
  if(NOT CXX_HAS_CXX_FILESYSTEM_AFTER_FLAGS AND NOT CXX_HAS_CXX_EXPERIMENTAL_FILESYSTEM_AFTER_FLAGS)
    indented_message(FATAL_ERROR "FATAL: After probing multiple configurations, still cannot compile and link a <filesystem> or <experimental/filesystem> based program! Please adjust the configuration and/or install missing dependencies.")
  endif()
  endfunction()
  check_stl_filesystem_link_flags(${stl_filesystem_link_flags})
  if(NOT CXX_HAS_CXX_FILESYSTEM_AFTER_FLAGS AND CXX_HAS_CXX_EXPERIMENTAL_FILESYSTEM_AFTER_FLAGS)
    all_compile_definitions(PUBLIC LLFIO_FORCE_EXPERIMENTAL_FILESYSTEM=1 KERNELTEST_FORCE_EXPERIMENTAL_FILESYSTEM=1)
  endif()
endif()
# Do we have Grand Central Dispatch on this platform?
if(LLFIO_USE_LIBDISPATCH)
  function(check_have_libdispatch postfix)
    set(CMAKE_REQUIRED_LIBRARIES ${ARGN})
    check_cxx_source_compiles("
#include <dispatch/dispatch.h>
int main() {
  return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0) != nullptr;
}
" LLFIO_HAS_LIBDISPATCH_${postfix})
  endfunction()
  check_have_libdispatch(BUILTIN)
  if(NOT LLFIO_HAS_LIBDISPATCH_BUILTIN)
    check_have_libdispatch(WITH_LIBDISPATCH dispatch)
    if(LLFIO_HAS_LIBDISPATCH_WITH_LIBDISPATCH)
      all_link_libraries(PUBLIC dispatch)
    endif()
  endif()
else()
  all_compile_definitions(PUBLIC LLFIO_DYNAMIC_THREAD_POOL_GROUP_USING_GCD=0)
endif()
if(NOT LLFIO_HAS_LIBDISPATCH_BUILTIN AND NOT LLFIO_HAS_LIBDISPATCH_WITH_LIBDISPATCH AND (CMAKE_SYSTEM_NAME MATCHES "FreeBSD" OR APPLE))
  indented_message(FATAL_ERROR "FATAL: Grand Central Dispatch as libdispatch was not found on this FreeBSD or Mac OS system. libdispatch is required for LLFIO to build on those systems.")
endif()

# Set any macros this library requires
all_compile_definitions(PRIVATE LLFIO_INCLUDE_STORAGE_PROFILE=1 LLFIO_ENABLE_TEST_IO_MULTIPLEXERS=1)
foreach(target ${llfio_EXAMPLE_TARGETS})
  target_compile_definitions(${target} PRIVATE LLFIO_INCLUDE_STORAGE_PROFILE=1)
endforeach()
if(LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
  all_compile_definitions(PUBLIC LLFIO_EXPERIMENTAL_STATUS_CODE=1)
endif()
if(WIN32)
  all_compile_definitions(PRIVATE _WIN32_WINNT=0x601)                ## Target Win7
  target_compile_definitions(llfio_hl INTERFACE _WIN32_WINNT=0x601)  ## Target Win7
  if(NOT LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
    target_link_libraries(llfio_hl INTERFACE ntkernel-error-category::hl)
    target_link_libraries(llfio_dl PUBLIC ntkernel-error-category::dl)
    target_link_libraries(llfio_sl PUBLIC ntkernel-error-category::sl)
  endif()
endif()
# Anyone using the static or dynamic libraries is not using the header only variant
target_compile_definitions(llfio_sl INTERFACE LLFIO_HEADERS_ONLY=0)
target_compile_definitions(llfio_dl INTERFACE LLFIO_HEADERS_ONLY=0)
target_compile_definitions(llfio_sl PRIVATE LLFIO_SOURCE=1 LLFIO_STATIC_LINK=1) 
target_compile_definitions(llfio_dl PRIVATE LLFIO_SOURCE=1 LLFIO_DYN_LINK=1) 
foreach(special ${SPECIAL_BUILDS})
  target_compile_definitions(llfio_sl-${special} INTERFACE LLFIO_HEADERS_ONLY=0)
  target_compile_definitions(llfio_dl-${special} INTERFACE LLFIO_HEADERS_ONLY=0)
  target_compile_definitions(llfio_sl-${special} PRIVATE LLFIO_SOURCE=1 LLFIO_STATIC_LINK=1) 
  target_compile_definitions(llfio_dl-${special} PRIVATE LLFIO_SOURCE=1 LLFIO_DYN_LINK=1) 
endforeach()
if(TARGET llfio-example_single-header)
  set(compiler_has_cxx_17 0)
  foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
    if(feature STREQUAL "cxx_std_17")
      set(compiler_has_cxx_17 1)
    endif()
  endforeach()
  # The single header test requires C++ 17
  if(compiler_has_cxx_17)
    target_compile_features(llfio-example_single-header PRIVATE cxx_std_17)
  else()
    set_target_properties(llfio-example_single-header PROPERTIES EXCLUDE_FROM_ALL ON EXCLUDE_FROM_DEFAULT_BUILD ON)
  endif()
endif()

if(NOT PROJECT_IS_DEPENDENCY)
  # For all possible configurations of this library, add each test
  set(llfio_TESTS_DISABLE_PRECOMPILE_HEADERS
    "llfio_hl--coroutines"
    "llfio_sl--coroutines"
    "llfio_dl--coroutines"
  )
  include(QuickCppLibMakeStandardTests)
  # For each test target, set definitions and linkage
  foreach(target ${llfio_COMPILE_TEST_TARGETS} ${llfio_TEST_TARGETS})
    target_compile_definitions(${target} PRIVATE LLFIO_INCLUDE_STORAGE_PROFILE=1 $<$<PLATFORM_ID:Windows>:LLFIO_ENABLE_TEST_IO_MULTIPLEXERS=1>)
  endforeach()
  find_quickcpplib_library(kerneltest
    GIT_REPOSITORY "https://github.com/ned14/kerneltest.git"
    REQUIRED
    IS_HEADER_ONLY
  )
  foreach(test_target ${llfio_TEST_TARGETS})
    target_link_libraries(${test_target} PRIVATE kerneltest::hl)
    if(test_target MATCHES coroutines)
      apply_cxx_coroutines_to(PRIVATE ${test_target})
    endif()
  endforeach()
  if(MSVC)
    foreach(test_target ${llfio_TEST_TARGETS})
      target_compile_options(${test_target} PRIVATE /wd4503)         ## decorated name length exceeded
      if(NOT CLANG)
        target_compile_options(${test_target} PRIVATE /permissive-)  ## future parsing
      endif()
    endforeach()
  endif()

  # Turn on latest C++ where possible for the test suite
  if(UNIT_TESTS_CXX_VERSION STREQUAL "latest")
    set(LATEST_CXX_FEATURE)
    foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
      if(feature STREQUAL "cxx_std_23")
        set(LATEST_CXX_FEATURE "cxx_std_23")
        indented_message(STATUS "NOTE: This compiler claims to support C++ 23, enabling for header-only unit test suite")
      endif()
    endforeach()
    if(NOT LATEST_CXX_FEATURE)
      foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
        if(feature STREQUAL "cxx_std_20")
          set(LATEST_CXX_FEATURE "cxx_std_20")
          indented_message(STATUS "NOTE: This compiler claims to support C++ 20, enabling for header-only unit test suite")
        endif()
      endforeach()
    endif()
    if(NOT LATEST_CXX_FEATURE)
      foreach(feature ${CMAKE_CXX_COMPILE_FEATURES})
        if(feature STREQUAL "cxx_std_17")
          if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
            set(LATEST_CXX_FEATURE "cxx_std_17")
            indented_message(STATUS "NOTE: This compiler claims to support C++ 17, enabling for header-only unit test suite")
          endif()
        endif()
      endforeach()
    endif()
  elseif(UNIT_TESTS_CXX_VERSION)
    set(LATEST_CXX_FEATURE "cxx_std_${UNIT_TESTS_CXX_VERSION}")
  endif()
  if(LATEST_CXX_FEATURE)
    # Turn on latest C++ where possible for the header only test suite
    foreach(test_target ${llfio_TEST_TARGETS} ${llfio_EXAMPLE_TARGETS})
      if(test_target MATCHES _hl)
        target_compile_features(${test_target} PUBLIC ${LATEST_CXX_FEATURE})
      endif()
    endforeach()
  endif()
endif()

if(LLFIO_ENABLE_DEPENDENCY_SMOKE_TEST)
  set(LLFIO_SMOKE_TESTS)
  add_executable(llfio-dependency-smoke-test "test-packaging/example.cpp")
  list(APPEND LLFIO_SMOKE_TESTS llfio-dependency-smoke-test)
  foreach(target ${LLFIO_SMOKE_TESTS})
    target_link_libraries(${target} PRIVATE llfio::dl)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(NAME ${target} CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel
      COMMAND $<TARGET_FILE:${target}> --reporter junit --out $<TARGET_FILE:${target}>.junit.xml
    )
  endforeach()
endif()

# Cache this library's auto scanned sources for later reuse
include(QuickCppLibCacheLibrarySources)

# Make available this library for install and export
include(QuickCppLibMakeInstall)
if(WIN32 AND NOT LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
  set_target_properties(ntkernel-error-category_hl PROPERTIES EXPORT_NAME ntkernel-error-category_hl)
  set_target_properties(ntkernel-error-category_sl PROPERTIES EXPORT_NAME ntkernel-error-category_sl)
  set_target_properties(ntkernel-error-category_dl PROPERTIES EXPORT_NAME ntkernel-error-category_dl)
  install(TARGETS ntkernel-error-category_hl ntkernel-error-category_sl ntkernel-error-category_dl
          EXPORT llfioExports
          INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endif()
include(QuickCppLibMakeExport)
