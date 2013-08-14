#include "test_functions.hpp"

BOOST_AUTO_TEST_CASE(async_io_works_1prime)
{
    BOOST_AFIO_TEST_CONFIG( "Tests that the async i/o implementation works (primes system)", 60);
    
    auto dispatcher=boost::afio::make_async_file_io_dispatcher(boost::afio::process_threadpool(), boost::afio::file_flags::None);
    std::cout << "\n\n1000 file opens, writes 1 byte, closes, and deletes (primes system):\n";
    _1000_open_write_close_deletes(dispatcher, 1);
}