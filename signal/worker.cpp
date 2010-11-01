#include "worker.h"
#include "intervals.h"
#include "postsink.h"

#include "tfr/cwt.h" // hack to make chunk sizes adapt to computer speed and memory

#include <QTime>
#include <QMutexLocker>
#include <boost/foreach.hpp>
#include <CudaException.h>

#define TIME_WORKER
//#define TIME_WORKER if(0)

#define TESTING_PERFORMANCE false

using namespace std;
using namespace boost::posix_time;

namespace Signal {

Worker::
        Worker(Signal::pOperation s)
:   work_chunks(0),
    work_time(0),
    _source(s),
    _samples_per_chunk( 1<<12 ),
    _max_samples_per_chunk( (unsigned)-1 ),
    _requested_fps( 20 ),
    _caught_exception( "" ),
    _caught_invalid_argument("")
{
    source( s );
    // Could create an first estimate of _samples_per_chunk based on available memory
    // unsigned mem = CudaProperties::getCudaDeviceProp( CudaProperties::getCudaCurrentDevice() ).totalGlobalMem;
    // but 1<< 12 works well on most GPUs
}

Worker::
        ~Worker()
{
    TaskTimer(__FUNCTION__).suppressTiming();

    this->quit();
    todo_list( Intervals() );
}

///// OPERATIONS


bool Worker::
        workOne()
{
    if (todo_list().empty())
        return false;

    // todo_list().print(__FUNCTION__);

    if (TESTING_PERFORMANCE) _samples_per_chunk = _max_samples_per_chunk;
    work_chunks++;

    unsigned center_sample = source()->sample_rate() * center;

    stringstream ss;
    TIME_WORKER TaskTimer tt("Working %u samples from %s, center=%u",
         _samples_per_chunk,
         ((std::stringstream&)(ss<<todo_list())).str().c_str(),
         center_sample );

    ptime startTime = microsec_clock::local_time();

    Interval interval = todo_list().getInterval( _samples_per_chunk, center_sample );

    pBuffer b;

    try
    {
        CudaException_CHECK_ERROR();

        {
            stringstream ss;
            TIME_WORKER TaskTimer tt("Reading source and calling callbacks %s",
                ((std::stringstream&)(ss<<interval)).str().c_str() );

            b = callCallbacks( interval );
            _samples_per_chunk = b->number_of_samples();

            work_time += b->length();

            TIME_WORKER {
                stringstream ss;
                TaskTimer("Worker got %s, %u samples",
                    ((std::stringstream&)(ss<<b->getInterval())).str().c_str(),
                    b->number_of_samples() ).suppressTiming();
            }
        }

        CudaException_CHECK_ERROR();
    } catch (const CudaException& e ) {
        if (cudaErrorMemoryAllocation == e.getCudaError() && 1<_samples_per_chunk) {
            _samples_per_chunk = Tfr::Cwt::Singleton().prev_good_size(
                    _samples_per_chunk, _source->sample_rate());
            _max_samples_per_chunk = _samples_per_chunk;
            TaskTimer("Worker caught cudaErrorMemoryAllocation. Setting max samples per chunk to %u\n%s", _samples_per_chunk, e.what()).suppressTiming();
        } else {
            throw;
        }
    }

    time_duration diff = microsec_clock::local_time() - startTime;

    unsigned milliseconds = diff.total_milliseconds();
    if (0==milliseconds) milliseconds=1;

    if (b) if (!TESTING_PERFORMANCE) {
        float current_fps = 1000.f/milliseconds;
        if (current_fps < _requested_fps &&
            _samples_per_chunk >= _min_samples_per_chunk)
        {
            _samples_per_chunk = Tfr::Cwt::Singleton().prev_good_size(
                    b->number_of_samples(), _source->sample_rate());
            TIME_WORKER TaskTimer("Low framerate (%.1f fps). Decreased samples per chunk to %u", 1000.f/milliseconds, _samples_per_chunk).suppressTiming();
        }
        else if (current_fps > 2.5f*_requested_fps)
        {
            _samples_per_chunk = Tfr::Cwt::Singleton().next_good_size(
                    b->number_of_samples(), _source->sample_rate());
            if (_samples_per_chunk>_max_samples_per_chunk)
                _samples_per_chunk=_max_samples_per_chunk;
            else
                TIME_WORKER TaskTimer("High framerate (%.1f fps). Increased samples per chunk to %u", 1000.f/milliseconds, _samples_per_chunk).suppressTiming();
        }

        _requested_fps = 99*_requested_fps/100;
        //if (1>_requested_fps)_requested_fps=1;
    }

    return true;
}


///// PROPERTIES

void Worker::
        todo_list( const Signal::Intervals& v )
{
    {
        QMutexLocker l(&_todo_lock);
        _todo_list = v;
    }

    //todo_list().print(__FUNCTION__);

    if (v)
        _todo_condition.wakeAll();
}


Signal::Intervals Worker::
        todo_list()
{
    QMutexLocker l(&_todo_lock);
    Signal::Intervals c = _todo_list;
    return c;
}


Signal::pOperation Worker::
        source() const
{
    return _source;
}


void Worker::
        source(Signal::pOperation value)
{
    _source = value;
    if (_source)
        _min_samples_per_chunk = Tfr::Cwt::Singleton().next_good_size( 1, _source->sample_rate());
    else
        _min_samples_per_chunk = 1;
    _max_samples_per_chunk = (unsigned)-1;
    _post_sink.invalidate_samples( Intervals::Intervals_ALL );
    // todo_list.clear();
}


void Worker::
        appendOperation(Signal::pOperation s)
{
    s->source( _source );
    _source = s;
    _post_sink.invalidate_samples( s->affected_samples() );
    return;
}


unsigned Worker::
        samples_per_chunk() const
{
    return _samples_per_chunk;
}


void Worker::
		samples_per_chunk_hint(unsigned value)
{
    _samples_per_chunk = max(_min_samples_per_chunk, value);
}


unsigned Worker::
        requested_fps() const
{
    return _requested_fps;
}


void Worker::
        requested_fps(unsigned value)
{
    if (0==value) value=1;

    if (value>_requested_fps) {
        _requested_fps = value;
    }
}


void Worker::
		checkForErrors()
{
	if (_caught_invalid_argument.what() && 0 != *_caught_invalid_argument.what()) {
		invalid_argument x = _caught_invalid_argument;
		_caught_invalid_argument = invalid_argument("");
		throw x;
	}
	
	if (_caught_exception.what() && 0 != *_caught_exception.what()) {
                runtime_error x = _caught_exception;
                _caught_exception = runtime_error("");
		throw x;
	}
}


PostSink* Worker::
        postSink()
{
    return &_post_sink;
}


// TODO remove
/*std::vector<pOperation> Worker::
        callbacks()
{
    QMutexLocker l(&_callbacks_lock);
    std::vector<pOperation> c = _callbacks;
    return c;
}*/


///// PRIVATE


void Worker::
        run()
{
	while (true)
	{
		try {
            while (todo_list())
			{
				workOne();
				msleep(1);
			}
		} catch ( const std::invalid_argument& x ) {
			if (0 == *_caught_invalid_argument.what())
				_caught_invalid_argument = x;
		} catch ( const std::exception& x ) {
			if (0 == _caught_exception.what())
                                _caught_exception = runtime_error(x.what());
		} catch ( ... ) {
			if (0 == _caught_exception.what())
				_caught_exception = std::runtime_error("Unknown exception");
		}

		try {
			{ TIME_WORKER TaskTimer tt("Worker is waiting for more work to do");
            QMutexLocker l(&_todo_lock);
			_todo_condition.wait( &_todo_lock );}
		} catch ( const std::exception& x ) {
                        _caught_exception = runtime_error(x.what());
			return;
		}
	}
}


void Worker::
        addCallback( pOperation c )
{
    c->source(source());

    std::vector<pOperation> callbacks = postSink()->sinks();
    callbacks.push_back( c );
    postSink()->sinks(callbacks);
}


void Worker::
        removeCallback( pOperation c )
{
    c->source(pOperation());

    QMutexLocker l( &_callbacks_lock );
    std::vector<pOperation> callbacks = postSink()->sinks();
    callbacks.resize( std::remove( callbacks.begin(), callbacks.end(), c ) - callbacks.begin() );
    postSink()->sinks(callbacks);
}


pBuffer Worker::
        callCallbacks( Interval i )
{
    _post_sink.source( source() );
    return _post_sink.read( i );
}

} // namespace Signal
