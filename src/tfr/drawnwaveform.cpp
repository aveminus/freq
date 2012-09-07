#include "drawnwaveform.h"

#include "drawnwaveformkernel.h"
#include "computationkernel.h"
#include "signal/buffer.h"

// gpumisc
#include "neat_math.h"

// std
#include <math.h>
#include <stdexcept>

namespace Tfr {


DrawnWaveform::
        DrawnWaveform()
            :
            block_fs(0),
            signal_length(0),
            maxValue(0.0001f)
{}


pChunk DrawnWaveform::
        operator()( Signal::pBuffer b )
{
    float blobsize = blob(b->sample_rate);

    unsigned w = ((unsigned)(b->number_of_samples() / blobsize / drawWaveform_BLOCK_SIZE)) *drawWaveform_BLOCK_SIZE;

    if (0 == w)
        throw std::logic_error("DrawnWaveform::operator() Not enough data");

    size_t free = availableMemoryForSingleAllocation();

    free /= 2; // Don't even try to get close to use all memory
    // never use more than 64 MB
    if (free > 64<<20)
        free = 64<<20;

    unsigned wmax = free/(drawWaveform_YRESOLUTION*sizeof(Tfr::ChunkElement)*drawWaveform_BLOCK_SIZE ) * drawWaveform_BLOCK_SIZE;
    if (0==wmax)
        wmax = free/(drawWaveform_YRESOLUTION*sizeof(Tfr::ChunkElement));
    if (0==wmax)
        wmax = 1;
    if (wmax < w)
        w = wmax;

    pChunk c(new DrawnWaveformChunk(this->block_fs));
    c->transform_data.reset( new ChunkData(w, drawWaveform_YRESOLUTION, 1));

    unsigned readstop = b->number_of_samples();
    if (b->getInterval().last > signal_length)
    {
        if (b->getInterval().first > signal_length)
            readstop = 0;
        else
            readstop = signal_length - b->getInterval().first;
    }

    float writeposoffs = ((b->sample_offset / blobsize) - floorf((b->sample_offset / blobsize).asFloat())).asFloat();
    ::drawWaveform(
            b->waveform_data(),
            c->transform_data,
            blobsize,
            readstop,
            maxValue,
            writeposoffs);

    this->block_fs = 0;

    c->chunk_offset = b->sample_offset / blobsize + writeposoffs;
    c->freqAxis = freqAxis( b->sample_rate );

    c->first_valid_sample = 0;
    c->n_valid_samples = c->nSamples();

    c->original_sample_rate = b->sample_rate;
    c->sample_rate = b->sample_rate / blobsize;

    return c;
}


Signal::pBuffer DrawnWaveform::
        inverse( pChunk /*chunk*/ )
{
    throw std::logic_error("Not implemented");
}


pTransform DrawnWaveform::
        createTransform() const
{
    return pTransform(new DrawnWaveform());
}


float DrawnWaveform::
        displayedTimeResolution( float FS, float /*hz*/ ) const
{
    return .25f/(FS);
}


FreqAxis DrawnWaveform::
        freqAxis( float /*FS*/ ) const
{
    FreqAxis a;
    a.axis_scale = AxisScale_Linear;

    a.max_frequency_scalar = drawWaveform_YRESOLUTION - 1;
    a.min_hz = -maxValue;
    a.f_step = (1/a.max_frequency_scalar) * 2*maxValue;

    //a.setLinear(FS, drawWaveform_YRESOLUTION - 1);

    return a;
}


static const unsigned DrawnWaveform_step_constant = 1024;


unsigned DrawnWaveform::
        next_good_size( unsigned current_valid_samples_per_chunk, float /*sample_rate*/ ) const
{
    if (current_valid_samples_per_chunk<drawWaveform_BLOCK_SIZE)
        return drawWaveform_BLOCK_SIZE;

    return spo2g(align_up(current_valid_samples_per_chunk, (unsigned) drawWaveform_BLOCK_SIZE)/drawWaveform_BLOCK_SIZE)*drawWaveform_BLOCK_SIZE;
}


unsigned DrawnWaveform::
        prev_good_size( unsigned current_valid_samples_per_chunk, float /*sample_rate*/ ) const
{
    if (current_valid_samples_per_chunk<2*drawWaveform_BLOCK_SIZE)
        return drawWaveform_BLOCK_SIZE;

    return lpo2s(align_up(current_valid_samples_per_chunk, (unsigned) drawWaveform_BLOCK_SIZE)/drawWaveform_BLOCK_SIZE)*drawWaveform_BLOCK_SIZE;
}


std::string DrawnWaveform::
        toString() const
{
    std::stringstream ss;
    ss << "Tfr::DrawnWaveform, block_fs=" << block_fs << ", maxValue=" << maxValue;
    return ss.str();
}


bool DrawnWaveform::
        operator==(const TransformParams& b) const
{
    const DrawnWaveform* p = dynamic_cast<const DrawnWaveform*>(&b);
    if (!p)
        return false;

    return block_fs == p->block_fs &&
           signal_length == p->signal_length &&
           maxValue == p->maxValue;
}


float DrawnWaveform::
        blob(float FS)
{
    if (0 == this->block_fs)
        this->block_fs = FS;
    float b = FS / this->block_fs;
    if (b > 1)
        b = (unsigned)(b + 0.5f);
    return b;
}


} // namespace Tfr
