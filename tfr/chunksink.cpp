#include "chunksink.h"

#include "signal/filteroperation.h"
#include "tfr/wavelet.cu.h"

static const bool D = false;

namespace Tfr {

pChunk ChunkSink::
        getChunk( Signal::pBuffer b, Signal::pSource s )
{
    if ( 0 != dynamic_cast<Tfr::Cwt*>(_transform.get ()))
        return getChunkCwt( b, s );
    if ( 0 != dynamic_cast<Tfr::Stft*>(_transform.get ()))
        return getChunkStft( b, s );

    if ( !_transform )
        throw std::invalid_argument("'chunk_transform' is null, should be an instance of Tfr::Transform");

    throw std::invalid_argument("'chunk_transform' is neither an instance of Tfr::Stft or Tfr::Cwt");
}


pChunk ChunkSink::
        getChunkStft( Signal::pBuffer b, Signal::pSource )
{
    if ( 0 != _filter)
        throw std::invalid_argument("filter must be empty if transform is stft");

    return Tfr::Stft::Singleton ()( b );
}


pChunk ChunkSink::
        getChunkCwt( Signal::pBuffer b, Signal::pSource s )
{
    Tfr::pChunk chunk;

    // If buffer comes directly from a Signal::FilterOperation
    Signal::FilterOperation* filterOp = dynamic_cast<Signal::FilterOperation*>(s.get());

    if (filterOp->transform() != this->_transform )
        filterOp = 0;

    Signal::pSource s2; // Temp variable in function scope
    if (!filterOp) {
        // Not directly from a filterOp, do we have a source?
        if (s)
        {
            // Yes, rely on FilterOperation to read from the source and create the chunk
            // We'll apply our own filter (chunk_filter) by the end of this method
            s2.reset( filterOp = new Signal::FilterOperation( s, _transform, Tfr::pFilter()));
        }
    }

    if (filterOp) {
        // use the Cwt chunk still stored in FilterOperation
        chunk = filterOp->previous_chunk();
        if (D) if(chunk) Signal::SamplesIntervalDescriptor(chunk->getInterval()).print("ChunkSink::getChunk stole filterOp chunk");

        if (0 == chunk) {
            // try again
            filterOp->read( b->sample_offset, b->number_of_samples() );
            chunk = filterOp->previous_chunk();
            if(chunk) Signal::SamplesIntervalDescriptor(chunk->getInterval()).print("ChunkSink::getChunk stole on second try");
        }
    }

    if (0 == chunk) {
        // otherwise compute the Cwt of this block
        Signal::SamplesIntervalDescriptor(b->getInterval()).print("ChunkSink::getChunk compute raw chunk");
        chunk = Tfr::Cwt::Singleton()( b );
        Signal::SamplesIntervalDescriptor(chunk->getInterval()).print("ChunkSink::getChunk computed raw chunk");

        // Don't know anything about the nearby data, so assume it's all valid
        chunk->n_valid_samples = chunk->transform_data->getNumberOfElements().width;
        chunk->first_valid_sample = 0;
    }

    if (_filter) {
            (*_filter)(*chunk);
    }

    return chunk;
}

pChunk ChunkSink::
        cleanChunk( pChunk c )
{
    if ( 0 == c->first_valid_sample && c->nSamples() == c->n_valid_samples )
        return c;

    pChunk clamped( new Chunk );
    clamped->transform_data.reset( new GpuCpuData<float2>(0, make_cudaExtent( c->n_valid_samples, c->nScales(), c->nChannels() )));

    ::wtClamp( c->transform_data->getCudaGlobal(), c->first_valid_sample, clamped->transform_data->getCudaGlobal() );
    clamped->max_hz = c->max_hz;
    clamped->min_hz = c->min_hz;
    clamped->chunk_offset = c->chunk_offset + c->first_valid_sample;
    clamped->sample_rate = c->sample_rate;
    clamped->first_valid_sample = 0;
    clamped->n_valid_samples = c->n_valid_samples;

    return clamped;
}

} // namespace Tfr
