#include "microphonerecorder.h"

#include <iostream>
#include <memory.h>
#include <boost/foreach.hpp>

//#define TIME_MICROPHONERECORDER
#define TIME_MICROPHONERECORDER if(0)

using namespace std;

namespace Adapters {

/**
  TODO document why this class is needed.
  */
class OperationProxy: public Signal::Operation
{
public:
    OperationProxy(Signal::Operation* p)
        :
        Operation(Signal::pOperation()),
        p(p)
    {}

    virtual Signal::pBuffer read( const Signal::Interval& I ) { return p->read( I ); }
    virtual float sample_rate() { return p->sample_rate(); }

private:
    Signal::Operation*p;
};

MicrophoneRecorder::MicrophoneRecorder(int inputDevice)
{
    portaudio::System &sys = portaudio::System::instance();

    if (0>inputDevice || inputDevice>sys.deviceCount()) {
        inputDevice = sys.defaultInputDevice().index();
    } else if ( sys.deviceByIndex(inputDevice).isOutputOnlyDevice() ) {
        cout << "Requested device '" << sys.deviceByIndex(inputDevice).name() << "' can only be used for output." << endl;
        inputDevice = sys.defaultInputDevice().index();
    } else {
        inputDevice = inputDevice;
    }

    cout << "Using device '" << sys.deviceByIndex(inputDevice).name() << "' for audio input." << endl << endl;

    portaudio::Device& device = sys.deviceByIndex(inputDevice);

    cout << "Opening recording input stream on " << device.name() << endl;
    portaudio::DirectionSpecificStreamParameters inParamsRecord(
            device,
            1, // channels
            portaudio::FLOAT32,
            false, // interleaved
#ifdef __APPLE__
            device.defaultHighInputLatency(),
#else
            device.defaultLowInputLatency(),
#endif
            NULL);

    portaudio::StreamParameters paramsRecord(
            inParamsRecord,
            portaudio::DirectionSpecificStreamParameters::null(),
            device.defaultSampleRate(),
            paFramesPerBufferUnspecified,
            paNoFlag);

    _stream_record.reset( new portaudio::MemFunCallbackStream<MicrophoneRecorder>(
            paramsRecord,
            *this,
            &MicrophoneRecorder::writeBuffer));

    Signal::pOperation proxy(new OperationProxy(&_data));
    _postsink.source( proxy );
}

MicrophoneRecorder::~MicrophoneRecorder()
{
    if (_stream_record) {
        _stream_record->isStopped()? void(): _stream_record->stop();
        _stream_record->isStopped()? void(): _stream_record->abort();
        _stream_record->close();
    }

    if (0<_data.length()) {
        TaskTimer tt(TaskTimer::LogVerbose, "Releasing recorded data");
        _data.reset();
    }
}

void MicrophoneRecorder::startRecording()
{
    _stream_record->start();
}

void MicrophoneRecorder::stopRecording()
{
    _stream_record->stop();
}

bool MicrophoneRecorder::isStopped()
{
    return _stream_record->isStopped();
}

Signal::pBuffer MicrophoneRecorder::
        read( const Signal::Interval& I )
{
    return _data.readFixedLength( I );
}

float MicrophoneRecorder::
        sample_rate()
{
    return _stream_record->sampleRate();
}

long unsigned MicrophoneRecorder::
        number_of_samples()
{
    return _data.number_of_samples();
}


int MicrophoneRecorder::
        writeBuffer(const void *inputBuffer,
                 void * /*outputBuffer*/,
                 unsigned long framesPerBuffer,
                 const PaStreamCallbackTimeInfo * /*timeInfo*/,
                 PaStreamCallbackFlags /*statusFlags*/)
{
    TIME_MICROPHONERECORDER TaskTimer tt("MicrophoneRecorder::writeBuffer(%u new samples)", framesPerBuffer);
    BOOST_ASSERT( inputBuffer );
    const float **in = (const float **)inputBuffer;
    const float *buffer = in[0];

    Signal::pBuffer b( new Signal::Buffer(0, framesPerBuffer, sample_rate() ) );
    memcpy ( b->waveform_data()->getCpuMemory(), buffer, framesPerBuffer*sizeof(float) );

    b->sample_offset = number_of_samples();
    b->sample_rate = sample_rate();

    TIME_MICROPHONERECORDER TaskTimer("Interval: %s", b->getInterval().toString().c_str()).suppressTiming();

    _data.put( b );

    _postsink.readFixedLength( b->getInterval() );

    return paContinue;
}

} // namespace Adapters
