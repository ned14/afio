#include "test_functions.hpp"

BOOST_AUTO_TEST_CASE(async_io_errors)
{
    BOOST_AFIO_TEST_CONFIG("Tests that the async i/o error handling works", 120);
    using namespace boost::afio;
    using namespace std;
    using boost::afio::future;

    {
        int hasErrorDirectly, hasErrorFromBarrier;
        auto dispatcher = make_async_file_io_dispatcher();
        auto mkdir(dispatcher->dir(async_path_op_req("testdir", file_flags::Create)));
        vector<async_path_op_req> filereqs;

        /* There was once a rare race condition in barrier() which took many, many days
         * to discover and solve, some of which involved much painful refactoring (which
         * was for the good anyway, but still painful). So, let's really hammer this API
         * such that it never, ever slightly fails to function ever again!
         */
#if defined(BOOST_MSVC) && BOOST_MSVC < 1700 /* <= VS2010 */
		// Throwing exceptions in VS2010 randomly causes segfaults :(
        for(size_t n=0; n<500; n++)
#elif defined(BOOST_MSVC) && BOOST_MSVC < 1800 /* <= VS2012 */ && (defined(DEBUG) || defined(_DEBUG))
		// Throwing exceptions is unbelievably slow on VS2012 and earlier if running inside a debugger
        for(size_t n=0; n<5000; n++)
#else
        for(size_t n=0; n<50000; n++)
#endif
        {
			// The following is a fundamentally unstable unit test - if manyfilecreates completes before
			// sync1, sync1 will throw on the spot
			// Similarly, if either manyfilecreates or sync1 completes before the first when_all(),
			// the nothrow_t when_all() will throw on the spot :)
			do
			{
				filereqs.clear();
				filereqs.push_back(async_path_op_req(mkdir, "testdir/a", file_flags::CreateOnlyIfNotExist));
				filereqs.push_back(async_path_op_req(mkdir, "testdir/a", file_flags::CreateOnlyIfNotExist));
				if(filesystem::exists("testdir/a"))
					filesystem::remove("testdir/a");
				try
				{
					auto manyfilecreates = dispatcher->file(filereqs); // One or both of these will error
					auto sync1 = dispatcher->barrier(manyfilecreates); // If barrier() doesn't throw due to errored input, barrier() will replicate errors for you
					auto future1 = when_all(std::nothrow_t(), sync1.begin(), sync1.end());
					auto future1e = when_all(sync1.begin(), sync1.end());
					auto future2 = when_all(std::nothrow_t(), sync1.begin(), sync1.end());
					auto future2e = when_all(sync1.begin(), sync1.end());
					// If any of the above threw due to context switches, they'll repeat

					BOOST_AFIO_CHECK_NO_THROW(future1.get()); // nothrow variant must never throw
					BOOST_AFIO_CHECK_THROWS(future1e.get()); // throw variant must always throw
					BOOST_AFIO_CHECK_NO_THROW(future2.get()); // nothrow variant must never throw
					BOOST_AFIO_CHECK_THROWS(future2e.get()); // throw variant must always throw
					hasErrorDirectly = 0;
					BOOST_FOREACH (auto &i, manyfilecreates)
					{
						// If we ask for has_exception() before the async thread has exited its packaged_task
						// this will fail, so no choice but to try { wait(); } catch { success }
						try
						{
							i.h->get();
						}
						catch (...)
						{
							hasErrorDirectly++;
						}
					}
					//std::cout << "hasErrorDirectly = " << hasErrorDirectly << std::endl;
					if(hasErrorDirectly != 1)
						BOOST_CHECK(hasErrorDirectly == 1);
					hasErrorFromBarrier = 0;
					BOOST_FOREACH (auto &i, sync1)
					{
						try
						{
							 i.h->get();
						}
						catch (...)
						{
							hasErrorFromBarrier++;
						}
					}
					//std::cout << "hasErrorFromBarrier = " << hasErrorFromBarrier << std::endl;
					if(hasErrorFromBarrier != 1)
						BOOST_CHECK(hasErrorFromBarrier == 1);
				}
				catch(...)
				{
					// Restore state and repeat until it works
					continue;
				}
			} while(false);
        }

		if(filesystem::exists("testdir/a"))
			filesystem::remove("testdir/a");
		if(filesystem::exists("testdir"))
			filesystem::remove("testdir");
    }
	// Add a single output to validate the test
	BOOST_CHECK(true);
}
