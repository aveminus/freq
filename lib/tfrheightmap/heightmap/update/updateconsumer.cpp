#include "updateconsumer.h"
#include "updatequeue.h"

#include "waveformblockupdater.h"
#include "tfrblockupdater.h"
#include "heightmap/uncaughtexception.h"
#include "tfr/chunk.h"

#include "tasktimer.h"
#include "timer.h"
#include "log.h"
#include "gl.h"
#include "logtickfrequency.h"

#include <QGLWidget>
#include <QGLContext>
#include <QOpenGLContext>
#include <QOffscreenSurface>

#include <numeric>

//#define INFO
#define INFO if(0)

using namespace std;

namespace Heightmap {
namespace Update {

template <class Q>
    typename Q::container_type& Container(Q& q) {
        struct HackedQueue : private Q {
            static typename Q::container_type& Container(Q& q) {
                return q.*&HackedQueue::c;
            }
        };
    return HackedQueue::Container(q);
}


class UpdateConsumerPrivate {
public:
    UpdateQueue::ptr update_queue;
    TfrBlockUpdater block_updater;
    WaveformBlockUpdater waveform_updater;
    LogTickFrequency ltf_work;
    LogTickFrequency ltf_tasks;

    Timer start_timer;
    double work_time = 0.;

    // return true if any work was done, always true if blocking=true.
    bool work1(bool blocking) {
        if (ltf_work.tick(false)) {
            Log("updateconsumer: %g wakeups/s, %g tasks/s, activity %.0f%%") % ltf_work.hz () % ltf_tasks.hz () % (100*work_time / start_timer.elapsedAndRestart ());
            work_time = 0;
        }

        bool rval = false, foundjob;

        try
          {
            unique_ptr<TaskTimer> tt;
            UpdateQueue::Job j;
            if (blocking)
              {
                INFO if (update_queue->empty ())
                    tt.reset (new TaskTimer("updateconsumer: Waiting for updates"));
                j = std::move(update_queue->pop ());
                tt.reset ();
              }

            Timer work_timer;

            do
              {
                auto jobqueue = update_queue->clear ();

                // Force a push_front to the std::queue
                if (j)
                    Container(jobqueue).push_front (std::move(j));

                size_t num_jobs = jobqueue.size ();
                Timer t;

                for (size_t i=0; i<num_jobs; i++)
                    ltf_tasks.tick (false);

                foundjob = !jobqueue.empty ();
                rval |= foundjob;

                while (!jobqueue.empty ())
                {
                    size_t s = jobqueue.size ();
                    block_updater.processJobs (jobqueue);
                    waveform_updater.processJobs (jobqueue);
                    EXCEPTION_ASSERT_LESS(jobqueue.size (), s);
                }

                INFO Log("updateconsumer: did %d jobs in %s")
                         % num_jobs % TaskTimer::timeToString (t.elapsed ());

        /*            INFO
                {
                    Signal::Intervals span = accumulate(jobs.begin (), jobs.end (), Signal::Intervals(),
                            [](Signal::Intervals& I, const UpdateQueue::Job& j) {
                                if (!j.updatejob)
                                    return I;
                                return I|=j.updatejob->getCoveredInterval();
                            });

                    std::set<pBlock> blocks;
                    for (auto& j : jobs)
                        if (j.updatejob)
                            for (pBlock b : j.intersecting_blocks)
                                blocks.insert(b);

                    Log("Updated %d chunks -> %d blocks. %s. %.0f samples/s")
                             % jobs.size () % blocks.size ()
                             % span % (span.count ()/t.elapsed ());
                }
                */

              } while(blocking && foundjob);

            work_time += work_timer.elapsed();
          }
        catch (UpdateQueue::skip_job_exception&)
          {
          }

        return rval;
    }
};


UpdateConsumer::UpdateConsumer(UpdateQueue::ptr update_queue)
    :
      p(new UpdateConsumerPrivate)
{
    p->update_queue = update_queue;
}


UpdateConsumer::~UpdateConsumer()
{

}


bool UpdateConsumer::
        workIfAny()
{
    return p->work1(false);
}


void UpdateConsumer::
        blockUntilWork()
{
    p->work1(true);
}


UpdateConsumerThread::
        UpdateConsumerThread(QGLWidget* shared_opengl_widget,
                       UpdateQueue::ptr update_queue)
    :
      UpdateConsumerThread( shared_opengl_widget->context ()->contextHandle (),
                      update_queue,
                      shared_opengl_widget)
{
}


UpdateConsumerThread::
        UpdateConsumerThread(QOpenGLContext* shared_opengl_context,
                       UpdateQueue::ptr update_queue,
                       QObject* parent)
    :
      QThread(parent),
      shared_opengl_context(shared_opengl_context),
      update_queue(update_queue)
{
    EXCEPTION_ASSERT(shared_opengl_context);

    // Check for clean exit
    connect(this, SIGNAL(finished()), SLOT(threadFinished()));

    surface = new QOffscreenSurface;
    surface->setParent (this);
    surface->setFormat (shared_opengl_context->format ());
    surface->create ();

    // Start this worker thread as a background thread
    start (LowPriority);
}



UpdateConsumerThread::
        ~UpdateConsumerThread()
{
    requestInterruption ();
    update_queue->close (); // make the thread detect the requestInterruption ()
    QThread::wait ();
}


void UpdateConsumerThread::
        threadFinished()
{
    TaskInfo ti("updateconsumerthread: threadFinished");

    try {
        EXCEPTION_ASSERTX(isInterruptionRequested (), "Thread quit unexpectedly");
        EXCEPTION_ASSERTX(update_queue->empty(), "Thread quit with jobs left");
    } catch (...) {
        try{
            Heightmap::UncaughtException::handle_exception(boost::current_exception());
        } catch (...) {}
    }
}


void UpdateConsumerThread::
        run()
{
    Log("updateconsumerthread: started");

    try
      {
        QOpenGLContext context;
        context.setShareContext (shared_opengl_context);
        context.setFormat (shared_opengl_context->format ());
        context.create ();

        if (!context.shareContext ()) {
            Log("!!! Couldn't share contexts. UpdateConsumer thread is stopped.");
            return;
        }

        context.makeCurrent (surface);

        UpdateConsumer uc(update_queue);
        Timer last_wakeup;
        while (!isInterruptionRequested ())
          {
            // when there is nothing to work on, wait until the next frame to check again
            static const double cap_wakeup_interval = 1./60;
            double force_sleep_interval = cap_wakeup_interval - last_wakeup.elapsedAndRestart ();
            if (force_sleep_interval > 0)
                std::this_thread::sleep_for (std::chrono::microseconds((long)(force_sleep_interval*1e6)));

            QCoreApplication::processEvents();

            uc.blockUntilWork ();
            emit didUpdate ();
          }
      }
    catch (UpdateQueue::abort_exception&)
      {
        requestInterruption ();
      }
    catch (...)
      {
        try {
            Heightmap::UncaughtException::handle_exception(boost::current_exception());
        } catch (...) {}

        requestInterruption ();
      }
}


} // namespace Update
} // namespace Heightmap
