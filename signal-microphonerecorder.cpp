#include "signal-microphonerecorder.h"

namespace Signal {

MicrophoneRecorder::MicrophoneRecorder()
{
    _sample_rate = 44100;
    unsigned frames_per_buffer = 1<<14;

    portaudio::System &sys = portaudio::System::instance();
    unsigned iInputDevice = sys.defaultInputDevice().index();

    cout << "Opening recording input stream on " << sys.deviceByIndex(iInputDevice).name() << endl;
    portaudio::DirectionSpecificStreamParameters inParamsRecord(
            sys.deviceByIndex(iInputDevice),
            1,
            portaudio::INT16,
            false,
            sys.deviceByIndex(iInputDevice).defaultLowInputLatency(),
            NULL);

    portaudio::StreamParameters paramsRecord(
            inParamsRecord,
            portaudio::DirectionSpecificStreamParameters::null(),
            _sample_rate,
            frames_per_buffer,
            paClipOff);

    streamRecord.reset( new portaudio::MemFunCallbackStream<MicrophoneRecorder>(
            paramsRecord,
            *this,
            &MicrophoneRecorder::writeBuffer));
}


pBuffer MicrophoneRecorder::
        read( unsigned firstSample, unsigned numberOfSamples )
{
    pBuffer b( new Buffer() );
    b->waveform_data.reset( new GpuCpuData<float>( 0, make_cudaExtent( numberOfSamples, 1, 1) ) );

    // this code is close to identical to "Playback::readBuffer", they could both use a BufferSource instead.
    // TODO refactor
    int r = -1;
    float *buffer = b->waveform_data->getCpuMemory();
    unsigned iBuffer = 0;
    unsigned nAccumulated_samples = 0;

    while ( 0 < numberOfSamples )
    {
        // count previous samples
        for (; iBuffer<_cache.size(); iBuffer++) {
            if (_playback_itr < nAccumulated_samples + _cache[iBuffer]->number_of_samples() )
                break;
            nAccumulated_samples += _cache[iBuffer]->number_of_samples();
        }

        if (buffer>=_cache.size())
        {
            memset(outputBuffer, 0, numberOfSamples*sizeof(float));
            streamPlayback->stop();
            streamPlayback->close();
            streamPlayback.reset();
            numberOfSamples = 0;
        } else {
            r = 0;

            const BufferSlot& s = _cache[iBuffer];

            unsigned nBytes_to_copy = nAccumulated_samples + _cache[iBuffer]->number_of_samples() - _playback_itr;
            if (framesPerBuffer < bytesToCopy )
                nBytes_to_copy = framesPerBuffer;

            memcpy( outputBuffer,
                    &_cache[iBuffer]->getCpuMemory()[ _playback_itr - nAccumulated_samples ],
                    nBytes_to_copy);

            numberOfSamples -= nBytes_to_copy;
            _playback_itr += nBytes_to_copy;
        }
    }

    if (r==-1)
        return pBuffer();

    return b;
}

unsigned MicrophoneRecorder::
        sample_rate()
{
    return _sample_rate;
}

unsigned MicrophoneRecorder::
        number_of_samples()
{
    unsigned n = 0;

    BOOST_FOREACH( const pBuffer& s, _cache ) {
        n += s->number_of_samples();
    }

    return n;
}

void MicrophoneRecorder::
        setCallback( Callback* p )
{
    _callback = p;
}

int MicrophoneRecorder::
        writeBuffer(const void *inputBuffer,
                 void */*outputBuffer*/,
                 unsigned long framesPerBuffer,
                 const PaStreamCallbackTimeInfo *timeInfo,
                 PaStreamCallbackFlags statusFlags)
{
    BOOST_ASSERT( inputBuffer );
    float **in = static_cast<float **>(inputBuffer);
    float *buffer = in[0];

    pBuffer b( new Buffer() );
    b->waveform_data.reset( new GpuCpuData<float>( 0, make_cudaExtent( framesPerBuffer, 1, 1) ) );


    memcpy ( b->waveform_data->getCpuMemory(), buffer, framesPerBuffer*sizeof(float) );

    if (_callback)
        _callback->recievedData( this );

    return paContinue;
}

} // namespace Waveform
