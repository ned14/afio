cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
include(../cmake/QuickCppLibBootstrap.cmake)
include(QuickCppLibUtils)

project(llfio-programs VERSION 1.0 LANGUAGES CXX)

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
find_quickcpplib_library(kerneltest
  GIT_REPOSITORY "https://github.com/ned14/kerneltest.git"
  REQUIRED
  IS_HEADER_ONLY
)
if(NOT TARGET llfio::hl)
  add_subdirectory(.. llfio EXCLUDE_FROM_ALL)
endif()

# Looks like cmake's toolset for LLVM-vs* has some serious problems
if(CMAKE_GENERATOR_TOOLSET MATCHES "LLVM-vs.*")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Z7")
endif()

function(make_program program)
  add_executable(${program} "${program}/main.cpp")
  if(WIN32)
    target_compile_definitions(${program} PRIVATE _UNICODE UNICODE)
    # cmake's support for LLVM clang is shocking :(
    if(CMAKE_GENERATOR_TOOLSET MATCHES "LLVM-vs.*")
      target_compile_options(${program} PRIVATE /EHsc)
    endif()
  endif()
  target_link_libraries(${program} ${ARGN})
  set_target_properties(${program} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    POSITION_INDEPENDENT_CODE ON
  )
endfunction()

make_program(benchmark-async llfio::hl)
make_program(benchmark-iostreams llfio::hl)
make_program(benchmark-locking llfio::hl kerneltest::hl)
make_program(fs-probe llfio::hl)
make_program(illegal-codepoints llfio::hl)
make_program(key-value-store llfio::hl)

if(MSVC)
  target_compile_options(illegal-codepoints PUBLIC /utf-8)
endif()

if(NOT WIN32)
  add_subdirectory(collision-check)
endif()
