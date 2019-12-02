# LLFIO cmake
# (C) 2016-2019 Niall Douglas <http://www.nedproductions.biz/>
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

cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
include(cmake/QuickCppLibBootstrap.cmake)
include(QuickCppLibRequireOutOfSourceBuild)
include(QuickCppLibUtils)
include(QuickCppLibPolicies)

option(LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE "Whether to use SG14 status_code for failure handling" OFF)

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
  GIT_TAG "develop"
  REQUIRED
  IS_HEADER_ONLY
)
if(WIN32)
  add_subdirectory("include/llfio/ntkernel-error-category" EXCLUDE_FROM_ALL)
endif()

set(UNIT_TESTS_CXX_VERSION "latest" CACHE STRING "The version of C++ to use in the header-only unit tests")

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

# If we have coroutines, enable those for the shared library build only
apply_cxx_coroutines_to(PRIVATE llfio_dl)

# Make preprocessed edition of this library target
if(NOT PROJECT_IS_DEPENDENCY)
  if(NOT PYTHONINTERP_FOUND)
    indented_message(WARNING "NOT rebuilding preprocessed edition of library due to python not being installed")
  elseif(FALSE)
    # See if the ply package is installed so pcpp can run
    execute_process(COMMAND python -c "import ply" RESULT_VARIABLE python_has_ply)
    if(NOT python_has_ply EQUAL 0)
      indented_message(WARNING "NOT rebuilding preprocessed edition of library due to installed python not having the ply package installed. "
        "Do 'pip install ply' to fix. NOTE that doxygen docs will NOT build without the precompiled edition.")
    else()
      function(make_single_header target name)
        add_partial_preprocess(${target}
                              "${name}"
                              ${ARGN}
                              -I ..
                              --passthru-defines --passthru-unfound-includes --passthru-unknown-exprs
                              --line-directive #--passthru-comments --debug
                              -U QUICKCPPLIB_ENABLE_VALGRIND
                              -U DOXYGEN_SHOULD_SKIP_THIS -U DOXYGEN_IS_IN_THE_HOUSE
                              -U STANDARDESE_IS_IN_THE_HOUSE
                              -U __has_include -U __has_feature -U __has_cpp_attribute
                              -U __cpp_modules
                             -U gsl_FEATURE_WITH_CONTAINER_TO_STD -U gsl_FEATURE_MAKE_SPAN_TO_STD -U gsl_FEATURE_BYTE_SPAN_TO_STD
                             -U gsl_FEATURE_HAVE_IMPLICIT_MACRO -U gsl_FEATURE_HAVE_OWNER_MACRO -U gsl_FEATURE_EXPERIMENTAL_RETURN_GUARD
                              WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                              )
        if(NOT CMAKE_VERSION VERSION_LESS 3.3)
          add_dependencies(llfio_hl ${target})
        endif()
      endfunction()
      make_single_header(llfio_hl-pp-posix
                         "${CMAKE_CURRENT_SOURCE_DIR}/single-header/llfio-posix.hpp"
                         "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                         #-D QUICKCPPLIB_USE_STD_BYTE -D QUICKCPPLIB_USE_STD_OPTIONAL -D QUICKCPPLIB_USE_STD_SPAN
                         -U gsl_COMPILER_MSVC_VERSION -U gsl_HAS_CPP0X -D gsl_CPLUSPLUS=201703 -D __cplusplus=201703
                         -D LLFIO_LEAN_AND_MEAN -U _WIN32
                         -D LLFIO_EXPERIMENTAL_STATUS_CODE=1)
      make_single_header(llfio_hl-pp-win
                         "${CMAKE_CURRENT_SOURCE_DIR}/single-header/llfio-win.hpp"
                         "${CMAKE_CURRENT_SOURCE_DIR}/include/llfio/v2.0/llfio.hpp"
                         #-D QUICKCPPLIB_USE_STD_BYTE -D QUICKCPPLIB_USE_STD_OPTIONAL -D QUICKCPPLIB_USE_STD_SPAN
                         -D gsl_CPLUSPLUS=201703 -D __cplusplus=201703
                         -D LLFIO_LEAN_AND_MEAN -D _WIN32
                         -D LLFIO_EXPERIMENTAL_STATUS_CODE=1)
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
  endif()
endif()

# Create a custom doxygen generation target
include(QuickCppLibMakeDoxygen)
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
# If on VS2019 16.3 or later, we require C++ 17
if(MSVC AND MSVC_VERSION VERSION_GREATER_EQUAL 1923)
  all_compile_features(PUBLIC
    cxx_std_17
  )
endif()
# Set the library dependencies this library has
all_link_libraries(PUBLIC quickcpplib::hl outcome::hl Threads::Threads)
# Set the system dependencies this library has
set(USING_LIBCXX_ON_LINUX 0)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  find_library(libstdcxx_stdcxxfs stdc++fs)
  if(libstdcxx_stdcxxfs MATCHES "NOTFOUND")
    set(libstdcxx_stdcxxfs -lstdc++fs)
  endif()
  get_target_property(val llfio_dl USING_LIBCXX_ON_LINUX)
  if(val MATCHES "NOTFOUND")
    all_link_libraries(PUBLIC ${libstdcxx_stdcxxfs} rt)
  else()
    set(USING_LIBCXX_ON_LINUX 1)
    # We had to use libc++ to get coroutines on Linux, so don't link the filesystem TS
    # into the _dl edition, which is the coroutines-enabled edition
    target_link_libraries(${PROJECT_NAME}_hl INTERFACE ${libstdcxx_stdcxxfs} rt)
    target_link_libraries(${PROJECT_NAME}_sl PUBLIC ${libstdcxx_stdcxxfs} rt)
    foreach(special ${SPECIAL_BUILDS})
      target_link_libraries(${PROJECT_NAME}_sl-${special} PUBLIC ${libstdcxx_stdcxxfs} rt)
    endforeach()
    # Do make libc++ a requirement for the _dl edition
    target_compile_definitions(${PROJECT_NAME}_dl PUBLIC _LIBCPP_NO_EXPERIMENTAL_DEPRECATION_WARNING_FILESYSTEM=1)
    target_compile_options(${PROJECT_NAME}_dl PUBLIC -stdlib=libc++)
    target_link_libraries(${PROJECT_NAME}_dl PUBLIC -stdlib=libc++ -lc++abi)
    foreach(special ${SPECIAL_BUILDS})
      target_compile_options(${PROJECT_NAME}_dl-${special} PUBLIC -stdlib=libc++)
      target_link_libraries(${PROJECT_NAME}_dl-${special} PUBLIC -stdlib=libc++ -lc++abi)
    endforeach()
  endif()
endif()
if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD" OR APPLE)
  find_library(libcxx_cxxexperimental c++experimental)
  all_link_libraries(PUBLIC ${libcxx_cxxexperimental})
endif()
# Set any macros this library requires
all_compile_definitions(PRIVATE LLFIO_INCLUDE_STORAGE_PROFILE=1)
foreach(target ${llfio_EXAMPLE_TARGETS})
  target_compile_definitions(${target} PRIVATE LLFIO_INCLUDE_STORAGE_PROFILE=1)
endforeach()
if(LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
  all_compile_definitions(PUBLIC LLFIO_EXPERIMENTAL_STATUS_CODE=1)
endif()
if(WIN32)
  all_compile_definitions(PRIVATE _WIN32_WINNT=0x600)  ## Target WinVista
  if(NOT LLFIO_USE_EXPERIMENTAL_SG14_STATUS_CODE)
    target_link_libraries(llfio_hl INTERFACE ntkernel-error-category::hl)
    target_link_libraries(llfio_dl PUBLIC ntkernel-error-category::dl)
    target_link_libraries(llfio_sl PUBLIC ntkernel-error-category::sl)
  endif()
endif()
# Anyone using the static or dynamic libraries is not using the header only variant
foreach(lib llfio_sl llfio_dl)
  target_compile_definitions(${lib} INTERFACE LLFIO_HEADERS_ONLY=0) 
  target_compile_definitions(${lib} PRIVATE LLFIO_SOURCE=1) 
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
  include(QuickCppLibMakeStandardTests)
  # For each test target, set definitions and linkage
  foreach(target ${llfio_COMPILE_TEST_TARGETS} ${llfio_TEST_TARGETS})
    target_compile_definitions(${target} PRIVATE LLFIO_INCLUDE_ASYNC_FILE_HANDLE=1 LLFIO_INCLUDE_STORAGE_PROFILE=1)
  endforeach()
  find_quickcpplib_library(kerneltest
    GIT_REPOSITORY "https://github.com/ned14/kerneltest.git"
    REQUIRED
    IS_HEADER_ONLY
  )
  foreach(test_target ${llfio_TEST_TARGETS})
    target_link_libraries(${test_target} PRIVATE kerneltest::hl)
    if(test_target MATCHES "coroutines|pipe_handle")
      if(USING_LIBCXX_ON_LINUX)
        if(NOT test_target MATCHES _sl)
          apply_cxx_coroutines_to(PRIVATE ${test_target})
        endif()
      else()
        apply_cxx_coroutines_to(PRIVATE ${test_target})
      endif()
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

# Cache this library's auto scanned sources for later reuse
include(QuickCppLibCacheLibrarySources)

# Make available this library for install and export
include(QuickCppLibMakeInstall)
include(QuickCppLibMakeExport)
