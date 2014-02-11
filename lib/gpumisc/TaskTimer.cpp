#include "TaskTimer.h"

#include "cva_list.h"

#include <iostream>
#include <time.h>
#include <exception>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string.hpp>

#ifndef NO_TASKTIMER_MUTEX // TODO use GPUMISC_NO_THREAD
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#endif

#ifndef _MSC_VER
#define MICROSEC_TIMESTAMPS
#endif

#ifdef _MSC_VER
#include <Windows.h>
#endif

#define THREADNUMBERS
//#define THREADNUMBERS if(0)

#define TIMESTAMPS
//#define TIMESTAMPS if(0)

using namespace boost::posix_time;

const int thread_column_width = 4;

#ifndef NO_TASKTIMER_MUTEX
QMutex staticLock(QMutex::Recursive);
#endif

struct ThreadInfo {
    const int threadNumber;
    void* qthread;
    unsigned counter[3];

    ThreadInfo(int threadNumber=0)
        :
        threadNumber(threadNumber),
        qthread(0)
    {
        memset(counter, 0, sizeof(counter));
    }
};

bool writeNextOnNewRow[3] = {false, false, false};
TaskTimer* lastTimer[3] = {0,0,0};
std::ostream* logLevelStream[] = {
    logLevelStream[0] = &std::cout,  // &std::cerr,  // LogVerbose
    logLevelStream[1] = &std::cout,
    logLevelStream[2] = &std::cout
};


std::map<void*,ThreadInfo> thread_info_map;

ThreadInfo& T() {
    void* threadid = 0;
#ifndef NO_TASKTIMER_MUTEX
    threadid = QThread::currentThreadId ();
#endif

    if (!thread_info_map.count (threadid))
        thread_info_map.insert (
                    std::pair<void*,ThreadInfo>(threadid,
                    ThreadInfo(thread_info_map.size ())));

    ThreadInfo& ti = thread_info_map[threadid];

    if (ti.qthread != QThread::currentThread ()) {
        // Thread was terminated. QThread::finished is emitted by TaskTimer shouldn't use slots.
        memset(ti.counter, 0, sizeof(ti.counter));
        ti.qthread = QThread::currentThread ();
    }

    return ti;
}


bool DISABLE_TASKTIMER = false;

/*TaskTimer& TaskTimer::getCurrentTimer() {
#ifndef NO_TASKTIMER_MUTEX
        QMutexLocker scope(&staticLock);
#endif
    if (0==TaskTimer::lastTimer[ logLevel ]) {
      throw std::logic_error("No previous timer present at " + (std::string) __LOCATION__);
    }
    return *lastTimer[ logLevel ];
}*/

TaskTimer::TaskTimer() {
    init( LogSimple, "Unlabeled task", 0 );
}

TaskTimer::TaskTimer(const char* f, ...) {
        Cva_start(c,f);
        init( LogSimple, f, c );
}

TaskTimer::TaskTimer(LogLevel logLevel, const char* f, ...) {
        Cva_start(c,f);
        init( logLevel, f, c );
}

TaskTimer::TaskTimer(bool, const char* f, va_list args) {
    init( LogSimple, f, args );
}

TaskTimer::TaskTimer(bool, LogLevel logLevel, const char* f, va_list args) {
    init( logLevel, f, args );
}

TaskTimer::TaskTimer(const boost::format& fmt)
{
    initEllipsis (LogSimple, "%s", fmt.str ().c_str ());
}

void TaskTimer::initEllipsis(LogLevel logLevel, const char* f, ...) {
    Cva_start(c,f);
    init( logLevel, f, c );
}

void TaskTimer::init(LogLevel logLevel, const char* task, va_list args) {
    if (DISABLE_TASKTIMER)
        return;

#ifndef NO_TASKTIMER_MUTEX
    QMutexLocker scope(&staticLock);
#endif

    this->numPartlyDone = 0;
    this->upperLevel = 0;
    this->suppressTimingInfo = false;
    this->is_unwinding = std::uncaught_exception();

    while (0<logLevel) {
        if (logLevelStream[ logLevel-1 ] == logLevelStream[ logLevel ] ) {
                        logLevel = (LogLevel)((int)logLevel-1);
        } else {
            break;
        }
    }

    this->logLevel = logLevel;

    if( 0<logLevel ) {
        logLevel = (LogLevel)((int)logLevel-1);
        upperLevel = new TaskTimer( 0, logLevel, task, args );
    }

    ++T().counter[this->logLevel];

    printIndentation();
    std::vector<std::string> strs;

    writeNextOnNewRow[this->logLevel] = true;

    int c = vsnprintf( 0, 0, task, Cva_list(args) );
    std::vector<char> t( c+1 );
    vsnprintf( &t[0], c+1, task, Cva_list(args) );
    std::string s;
    s.append ( &t[0],&t[c] );

    if (strchr(s.c_str(), '\n'))
        boost::split(strs, s, boost::is_any_of("\n"), boost::algorithm::token_compress_off);

    if (!strs.empty())
    {   if (strs.back().size() == 0 && strs.size()>1)
            strs.pop_back();

        s = strs[0];
    }

    logprint( s.c_str() );

    for (unsigned i=1; i<strs.size(); i++)
        info("> %s", strs[i].c_str());

    this->startTime = microsec_clock::local_time();
#ifdef _MSC_VER
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    this->hpcStart = li.QuadPart;
#endif
}

void TaskTimer::logprint(const char* txt) {
    if (0 == logLevelStream[ logLevel ]) {
        ;
    } else {
        *logLevelStream[ logLevel ] << txt;

        if (strchr(txt, '\n'))
            *logLevelStream[ logLevel ] << std::flush;
    }
}

void TaskTimer::info(const char* taskInfo, ...) {
        Cva_start(c, taskInfo);
        vinfo(taskInfo, c);
}

void TaskTimer::vinfo(const char* taskInfo, va_list args) {
    // The constructor creates new instances for other log levels.
    TaskTimer myTask( 0, logLevel, taskInfo, args );

    myTask.suppressTiming();
}

void TaskTimer::suppressTiming() {
    if (DISABLE_TASKTIMER)
        return;

#ifndef NO_TASKTIMER_MUTEX
    QMutexLocker scope(&staticLock);
#endif
    for( TaskTimer* p = this; 0 != p; p = p->upperLevel ) {
        p->suppressTimingInfo = true;
    }
}

bool TaskTimer::printIndentation() {
#ifndef NO_TASKTIMER_MUTEX
    QMutexLocker scope(&staticLock);
#endif
    ThreadInfo& t = T();
    TaskTimer* ltll = lastTimer[logLevel];

    if (ltll == this) {
        return false;
    } else {
        if (writeNextOnNewRow[logLevel])
            logprint("\n");

        TIMESTAMPS { // Print timestamp
            std::stringstream ss;

            ptime now = microsec_clock::local_time();
            time_duration t = now.time_of_day();

#ifndef MICROSEC_TIMESTAMPS
            ss  << std::setiosflags(std::ios::fixed)
                << std::setfill('0') << std::setw(2)
                << t.hours() << ":" << std::setw(2) << t.minutes() << ":"
                << std::setprecision(3) << std::setw(6)
                << t.fractional_seconds()/(float)t.ticks_per_second() + t.seconds() << " ";
#else
            ss  << std::setiosflags(std::ios::fixed)
                << std::setfill('0') << std::setw(2)
                << t.hours() << ":" << std::setw(2) << t.minutes() << ":"
                << std::setprecision(6) << std::setw(9)
                << t.fractional_seconds()/(float)t.ticks_per_second() + t.seconds() << " ";
#endif

            logprint( ss.str().c_str() );
        }

        THREADNUMBERS { // Print thread numbers
            std::stringstream ss;

            int width = 1;
            int N = thread_info_map.size ();
            int number = t.threadNumber;

            while ((N/=10) > 1)
                width++;

            if (number > 0)
                ss  << std::setfill(' ') << std::setw(width)
                    << number << " ";
            else
                ss  << std::setfill(' ') << std::setw(width)
                    << "" << " ";

            // different columns
            ss << std::setfill(' ') << std::setw (number*thread_column_width) << "";

            logprint( ss.str ().c_str () );
        }

        const char* separators[] = { "|", "-" };
        for (unsigned i=1; i<t.counter[logLevel]; i++)
                logprint(separators[i%(sizeof(separators)/sizeof(separators[0]))]);

        if (1<t.counter[logLevel])
                logprint(" ");

        lastTimer[logLevel] = this;

        return true;
    }
}

void TaskTimer::partlyDone() {
    if (DISABLE_TASKTIMER)
        return;

#ifndef NO_TASKTIMER_MUTEX
    QMutexLocker scope(&staticLock);
#endif
    ThreadInfo& t = T();

    ++t.counter[logLevel];
    writeNextOnNewRow[logLevel] = false;

    bool didprint = printIndentation();

    if (didprint) {
        logprint("> ");
    }

    --t.counter[logLevel];

    do {
        numPartlyDone++;
        logprint(".");
    } while (numPartlyDone<3);

    writeNextOnNewRow[logLevel] = true;

    if (0 != logLevelStream[ logLevel ])
        *logLevelStream[ logLevel ] << std::flush;

    // for all public methods, do the same action for the parent TaskTimer
    if (0 != upperLevel) {
        upperLevel->partlyDone();
    }
}


float TaskTimer::elapsedTime()
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
    return double(li.QuadPart-hpcStart)/PCfreq;
#else
    time_duration diff = microsec_clock::local_time() - startTime;
    return diff.total_microseconds() * 1e-6;
#endif
}


TaskTimer::~TaskTimer() {
    if (DISABLE_TASKTIMER)
        return;

#ifndef NO_TASKTIMER_MUTEX
    QMutexLocker scope(&staticLock);
#endif

    float d = elapsedTime();
    time_duration diff = microseconds(d*1e6);

    bool didIdent = printIndentation();

    if (didIdent) {
//		logprintf(": ");
    }

    bool exception_message = !is_unwinding && std::uncaught_exception();
    std::string finish_message = exception_message ? "aborted, exception thrown" : "done";

    if (!suppressTimingInfo) {
        finish_message += exception_message ? " after" : " in";

        if (!didIdent) {
            while (numPartlyDone<3) {
                numPartlyDone++;
                logprint(".");
            }
            logprint(" ");
        }

//        if ( diff.total_nanoseconds()<1500 && diff.total_nanoseconds() != 1000) {
//            logprint(str(boost::format("%s %u ns.\n") % finish_message % (unsigned)diff.total_nanoseconds()).c_str());
//        } else
        if (diff.total_microseconds() <1500 && diff.total_microseconds() != 1000) {
            logprint(str(boost::format("%s %.0f us.\n") % finish_message % (float)(diff.total_nanoseconds()/1000.0f)).c_str());
        } else if (diff.total_milliseconds() <1500 && diff.total_milliseconds() != 1000) {
            logprint(str(boost::format("%s %.1f ms.\n") % finish_message % (float)(diff.total_microseconds()/1000.0f)).c_str());
        } else if (diff.total_seconds()<90) {
            logprint(str(boost::format("%s %.1f s.\n") % finish_message % (float)(diff.total_milliseconds()/1000.f)).c_str());
        } else {
            logprint(str(boost::format("%s %.1f min.\n") % finish_message % (float)(diff.total_seconds()/60.f)).c_str());
        }
    } else {
        if (didIdent) {
            logprint(finish_message.c_str());
        } else {
            while (numPartlyDone<1) {
                numPartlyDone++;
                logprint(".");
            }

            if (exception_message)
            {
                while (numPartlyDone<3) {
                    numPartlyDone++;
                    logprint(".");
                }
                logprint(" ");
                logprint(finish_message.c_str());
            }
        }
        logprint("\n");
    }

    ThreadInfo& t = T();
    writeNextOnNewRow[logLevel] = false;
    --t.counter[logLevel];

    if (didIdent && 0==t.counter[logLevel]) {
                logprint("\n");
    }

    lastTimer[logLevel] = 0;


    if (upperLevel) {
        delete upperLevel;
        upperLevel = 0;
    }
}

void TaskTimer::setLogLevelStream( LogLevel logLevel, std::ostream* str ) {
#ifndef NO_TASKTIMER_MUTEX
    QMutexLocker scope(&staticLock);
#endif

    switch (logLevel) {
        case LogVerbose:
        case LogDetailed:
        case LogSimple:
            logLevelStream[ logLevel ] = str;
            break;

        default:
            throw std::logic_error((boost::format("Muse be one "
                "of LogVerbose {%u}, LogDetailed {%u} or LogSimple {%u}.")
                % LogVerbose % LogDetailed % LogSimple ).str());
    }
}

bool TaskTimer::
        isEnabled(LogLevel logLevel)
{
    return 0!=logLevelStream[logLevel];
}

bool TaskTimer::
        enabled()
{
    return !DISABLE_TASKTIMER;
}

void TaskTimer::
        setEnabled( bool enabled )
{
    DISABLE_TASKTIMER = !enabled;
}

TaskInfo::
        TaskInfo(const char* taskInfo, ...)
{
    Cva_start(args, taskInfo);

    tt_ = new TaskTimer( 0, taskInfo, args );
    tt_->suppressTiming ();
}

TaskInfo::
        TaskInfo(const boost::format& fmt)
{
    tt_ = new TaskTimer(fmt);
    tt_->suppressTiming ();
}

TaskInfo::
        ~TaskInfo()
{
    delete tt_;
}
