#include "timer.h"
#include "exceptionassert.h"
#include <limits>

#ifdef _MSC_VER
#include <Windows.h>
#endif

using namespace boost::posix_time;

Timer::Timer()
{
    restart();
}


void Timer::restart()
{
#ifdef _MSC_VER
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    start_ = li.QuadPart;
#else
    start_ = microsec_clock::local_time();
#endif
}


double Timer::elapsed() const
{
#ifdef _MSC_VER
    LARGE_INTEGER li;
    static double PCfreq = 1;
    for(static bool doOnce=true;doOnce;doOnce=false)
    {
        QueryPerformanceFrequency(&li);
        PCfreq = double(li.QuadPart);
    }
    QueryPerformanceCounter(&li);
    return double(li.QuadPart-start_)/PCfreq;
#else
    time_duration diff = microsec_clock::local_time() - start_;
    return diff.total_microseconds() * 1e-6;
#endif
}


double Timer::elapsedAndRestart()
{
#ifdef _MSC_VER
    LARGE_INTEGER li;
    static double PCfreq = 1;
    for(static bool doOnce=true;doOnce;doOnce=false)
    {
        QueryPerformanceFrequency(&li);
        PCfreq = double(li.QuadPart);
    }
    QueryPerformanceCounter(&li);
    __int64 now = li.QuadPart;
    double diff = double(now-start_)/PCfreq;
    start_ = now;
    return diff;
#else
    boost::posix_time::ptime now = microsec_clock::local_time();
    time_duration diff = now - start_;
    start_ = now;
    return diff.total_microseconds() * 1e-6;
#endif
}


void Timer::
        test()
{
    // It should measure time with a high accuracy
    // (at least 0.1-millisecond resolution)
    {
        Timer t;
        double f = 1.00000000001;
        for (int i=0;i<10000;i++)
            f*=f;
        double T1 = t.elapsedAndRestart ();
        for (int i=0;i<10000;i++)
            f*=f;
        double T2 = t.elapsedAndRestart ();
        for (int i=0;i<10000;i++)
            f*=f;
        double T3 = t.elapsed ();
        for (int i=0;i<10000;i++)
            f*=f;
        double T4 = t.elapsed ();
        t.restart ();
        for (int i=0;i<10000;i++)
            f*=f;
        double T5 = t.elapsed ();

        EXCEPTION_ASSERT_LESS(T1, 60e-6);
        EXCEPTION_ASSERT_LESS(T1, T2*1.5);
        EXCEPTION_ASSERT_LESS(T2, T1*1.5);
        EXCEPTION_ASSERT_LESS(T1, T3*1.6);
        EXCEPTION_ASSERT_LESS(T3, T1*1.5);
        EXCEPTION_ASSERT_LESS(T3*1.5, T4);
        EXCEPTION_ASSERT_LESS(T1, T5*1.5);
        EXCEPTION_ASSERT_LESS(T5, T1*1.5);
        EXCEPTION_ASSERT_EQUALS(std::numeric_limits<double>::infinity(),f);
    }
}
