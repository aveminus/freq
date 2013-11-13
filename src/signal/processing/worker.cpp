#include "worker.h"
#include "task.h"
#include "detectgdb.h"
#include "expectexception.h"
#include "prettifysegfault.h"

namespace Signal {
namespace Processing {

bool enable_lockfailed_print = true;

Worker::
        Worker (Signal::ComputingEngine::Ptr computing_eninge, ISchedule::WeakPtr schedule)
    :
      computing_eninge_(computing_eninge),
      schedule_(schedule)
{
}


void Worker::
        run()
    {
    setTerminationEnabled ();

    try
        {
        int consecutive_lock_failed_count = 0;
        for (;;)
            {
            try
                {
                ISchedule::Ptr schedule = schedule_.lock ();
                if (!schedule)
                    break;

                Task::Ptr task = schedule->getTask(computing_eninge_);
                if (!task)
                    break;

                // Abort if the worker was asked to stop
                if (!schedule_.lock ())
                    break;

                write1(task)->run(computing_eninge_);

                consecutive_lock_failed_count = 0;
                }

            catch (const LockFailed& x)
                {
                if (enable_lockfailed_print)
                    {
                    TaskInfo("");
                    TaskInfo(boost::format("Lock failed\n%s") % boost::diagnostic_information(x));
                    TaskInfo("");
                    }

                if (consecutive_lock_failed_count < 1)
                    {
                    if (enable_lockfailed_print)
                        TaskInfo("Trying again %d", consecutive_lock_failed_count);
                    consecutive_lock_failed_count++;
                    }
                else
                    throw;
                }
            }

            deleteLater ();
        }
    catch (const std::exception&)
        {
        exception_ = boost::current_exception ();
        }
    }


void Worker::
        exit_nicely_and_delete()
{
    schedule_.reset ();
}


boost::exception_ptr Worker::
        caught_exception() const
{
    return exception_;
}


class GetTaskMock: public ISchedule {
public:
    GetTaskMock() : get_task_count(0) {}

    int get_task_count;

    virtual Task::Ptr getTask(Signal::ComputingEngine::Ptr) volatile {
        get_task_count++;
        return Task::Ptr();
    }
};


class GetTaskSegFaultMock: public ISchedule {
public:
    virtual Task::Ptr getTask(Signal::ComputingEngine::Ptr) volatile {
        if (DetectGdb::was_started_through_gdb ())
            BOOST_THROW_EXCEPTION(segfault_exception());

        // Causing deliberate segfault to test that the worker handles it correctly
        // The test verifies that it works to instantiate a TaskInfo works
        TaskInfo("testing instantiated TaskInfo");
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnull-dereference"
        *(int*)0 = 0; // cause segfault
#pragma clang diagnostic pop

        // unreachable code
        return Task::Ptr();
    }
};


class GetTaskExceptionMock: public ISchedule {
public:
    virtual Task::Ptr getTask(Signal::ComputingEngine::Ptr) volatile {
        EXCEPTION_ASSERTX(false, "testing that worker catches exceptions from a scheduler");

        // unreachable code
        return Task::Ptr();
    }
};


class DeadLockMock: public GetTaskMock {
public:
    virtual Task::Ptr getTask(Signal::ComputingEngine::Ptr engine) volatile {
        GetTaskMock::getTask (engine);

        // cause dead lock
        volatile DeadLockMock m;
        WritePtr(&m, 1).get() && WritePtr(&m, 1).get();

        // unreachable code
        return Task::Ptr();
    }
};


void Worker::
        test()
{
    // It should run the next task as long as there is one
    {
        ISchedule::Ptr gettask(new GetTaskMock());

        Worker worker(Signal::ComputingEngine::Ptr(), gettask);
        worker.start ();
        bool finished = worker.wait (3); // Stop running within 3 ms, equals !worker.isRunning ()
        EXCEPTION_ASSERT_EQUALS( true, finished );

        EXCEPTION_ASSERT_EQUALS( 1, dynamic_cast<GetTaskMock*>(&*write1(gettask))->get_task_count );
        // Verify that tasks execute properly in Task::test.
    }

    // It should store information about a crashed task (segfault)
    {
        PrettifySegfault::EnableDirectPrint (false);

        ISchedule::Ptr gettask(new GetTaskSegFaultMock());

        Worker worker(Signal::ComputingEngine::Ptr(), gettask);
        worker.run ();
        bool finished = worker.wait (2);
        EXCEPTION_ASSERT_EQUALS( true, finished );
        EXCEPTION_ASSERT( worker.caught_exception () );

        EXPECT_EXCEPTION(segfault_exception, rethrow_exception(worker.caught_exception ()));

        PrettifySegfault::EnableDirectPrint (true);
    }

    // It should store information about a crashed task (C++ exception)
    {
        ISchedule::Ptr gettask(new GetTaskExceptionMock());

        Worker worker(Signal::ComputingEngine::Ptr(), gettask);
        worker.run ();
        bool finished = worker.wait (2);
        EXCEPTION_ASSERT_EQUALS( true, finished );
        EXCEPTION_ASSERT( worker.caught_exception () );

        try {
            rethrow_exception(worker.caught_exception ());
            BOOST_THROW_EXCEPTION(boost::unknown_exception());
        } catch (const ExceptionAssert& x) {
            const std::string* message = boost::get_error_info<ExceptionAssert::ExceptionAssert_message>(x);
            EXCEPTION_ASSERT_EQUALS( "testing that worker catches exceptions from a scheduler", message?*message:"" );
        }
    }

    // It should swallow one LockFailed without aborting the thread but abort if
    // several consecutive LockFailed are thrown.
    {
        enable_lockfailed_print = false;

        ISchedule::Ptr gettask(new DeadLockMock());

        Worker worker(Signal::ComputingEngine::Ptr(), gettask);
        worker.run ();
        bool finished = worker.wait (2);
        EXCEPTION_ASSERT_EQUALS( true, finished );
        EXCEPTION_ASSERT( worker.caught_exception () );

        EXPECT_EXCEPTION(LockFailed, rethrow_exception(worker.caught_exception ()));

        EXCEPTION_ASSERT_EQUALS( 2, dynamic_cast<GetTaskMock*>(&*write1(gettask))->get_task_count );

        enable_lockfailed_print = true;
    }
}


} // namespace Processing
} // namespace Signal