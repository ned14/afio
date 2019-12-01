/* Integration test kernel for file_handle create and close
(C) 2016-2017 Niall Douglas <http://www.nedproductions.biz/> (22 commits)
File Created: Apr 2016


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

#include "kernel_file_handle.cpp.hpp"

template <class U> inline void file_handle_create_close_creation(U &&f)
{
  using namespace KERNELTEST_V1_NAMESPACE;
  using LLFIO_V2_NAMESPACE::result;
  using file_handle = LLFIO_V2_NAMESPACE::file_handle;
  static const il_result<void> no_such_file_or_directory = LLFIO_V2_NAMESPACE::errc::no_such_file_or_directory;
  static const il_result<void> file_exists = LLFIO_V2_NAMESPACE::errc::file_exists;

  /* Set up a permuter which for every one of these parameter values listed,
  tests with the value using the input workspace which should produce outcome
  workspace. Workspaces are:
    * non-existing: no files
    *    existing0: single zero length file
    *    existing1: single one byte length file
  */
  // clang-format off
  static const auto permuter(mt_permute_parameters<  // This is a multithreaded parameter permutation test
    il_result<void>,                                  // The output outcome/result/option type. Type void means we don't care about the return type.
    parameters<                                      // The types of one or more input parameters to permute/fuzz the kernel with.
      typename file_handle::mode,
      typename file_handle::creation,
      typename file_handle::flag
    >,
    // Any additional per-permute parameters not used to invoke the kernel
    precondition::filesystem_setup_parameters,
    postcondition::filesystem_comparison_structure_parameters
  >(
    { // Initialiser list of output value expected for the input parameters, plus any precondition/postcondition parameters

      // Does the mode parameter have the expected side effects?
      {                 success(), { file_handle::mode::none,       file_handle::creation::if_needed, file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      {                 success(), { file_handle::mode::attr_read,  file_handle::creation::if_needed, file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      {                 success(), { file_handle::mode::attr_write, file_handle::creation::if_needed, file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      {                 success(), { file_handle::mode::write,      file_handle::creation::if_needed, file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {                 success(), { file_handle::mode::write,      file_handle::creation::if_needed, file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      {                 success(), { file_handle::mode::append,     file_handle::creation::if_needed, file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {                 success(), { file_handle::mode::append,     file_handle::creation::if_needed, file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      {                 success(), { file_handle::mode::none,       file_handle::creation::if_needed, file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {                 success(), { file_handle::mode::attr_read,  file_handle::creation::if_needed, file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {                 success(), { file_handle::mode::attr_write, file_handle::creation::if_needed, file_handle::flag::none }, { "non-existing" }, { "existing0"    }},

      // Does the creation parameter have the expected side effects?
      { no_such_file_or_directory, { file_handle::mode::write, file_handle::creation::open_existing    , file_handle::flag::none }, { "non-existing" }, { "non-existing" }},
      {                 success(), { file_handle::mode::write, file_handle::creation::open_existing    , file_handle::flag::none }, { "existing0"    }, { "existing0"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::open_existing    , file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::only_if_not_exist, file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {               file_exists, { file_handle::mode::write, file_handle::creation::only_if_not_exist, file_handle::flag::none }, { "existing0"    }, { "existing0"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::if_needed        , file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::if_needed        , file_handle::flag::none }, { "existing1"    }, { "existing1"    }},
      { no_such_file_or_directory, { file_handle::mode::write, file_handle::creation::truncate_existing, file_handle::flag::none }, { "non-existing" }, { "non-existing" }},
      {                 success(), { file_handle::mode::write, file_handle::creation::truncate_existing, file_handle::flag::none }, { "existing0"    }, { "existing0"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::truncate_existing, file_handle::flag::none }, { "existing1"    }, { "existing0"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::always_new       , file_handle::flag::none }, { "non-existing" }, { "existing0"    }},
      {                 success(), { file_handle::mode::write, file_handle::creation::always_new       , file_handle::flag::none }, { "existing1"    }, { "existing0"    }},

      // Does the flag parameter have the expected side effects?
      {                 success(), { file_handle::mode::write, file_handle::creation::open_existing, file_handle::flag::unlink_on_first_close }, { "existing1" }, { "non-existing" }}
    },
    // Any parameters from now on are called before each permutation and the object returned is
    // destroyed after each permutation. The callspec is (parameter_permuter<...> *parent, outcome<T> &testret, size_t, pars)
    precondition::filesystem_setup(),                 // Configure this filesystem workspace before the test
    postcondition::filesystem_comparison_structure()  // Do a structural comparison of the filesystem workspace after the test
  ));
  // clang-format on

  // Have the permuter permute callable f with all the permutations, returning outcomes
  auto results = permuter(std::forward<U>(f));
  // Check these permutation results, pretty printing the outcomes and also informing Boost.Test via BOOST_CHECK().
  check_results_with_boost_test(permuter, results);
}

KERNELTEST_TEST_KERNEL(unit, llfio, file_handle_create_close, file_handle, "Tests that llfio::file_handle::file()'s parameters with absolute paths work as expected", file_handle_create_close_creation(file_handle_create_close::test_kernel_file_handle_absolute))
KERNELTEST_TEST_KERNEL(unit, llfio, file_handle_create_close, file_handle, "Tests that llfio::file_handle::file()'s parameters with relative paths work as expected", file_handle_create_close_creation(file_handle_create_close::test_kernel_file_handle_relative))
