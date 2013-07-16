#include "test_functions.h"


BOOST_AUTO_TEST_CASE(async_io_works_1_autoflush)
{
    BOOST_TEST_MESSAGE("Tests that the autoflush async i/o implementation works");
        auto dispatcher=boost::afio::async_file_io_dispatcher(boost::afio::process_threadpool(), boost::afio::file_flags::AutoFlush);
        std::cout << "\n\n1000 file opens, writes 1 byte, closes, and deletes with autoflush i/o:\n";
        _1000_open_write_close_deletes(dispatcher, 1);
}