#include "heightmap-collection.h"
#include "heightmap-slope.cu.h"
#include "heightmap-block.cu.h"
#include "signal-filteroperation.h"
#include "tfr-cwt.h"
#include <boost/foreach.hpp>
#include <CudaException.h>
#include <GlException.h>
#include <string>

#ifdef _MSC_VER
#include <msc_stdc.h>
#endif

namespace Heightmap {

///// HEIGHTMAP::BLOCK
float Block::
sample_rate()
{
    Position a, b;
    ref.getArea( a, b );
    return pow(2, -ref.log2_samples_size[0]) - 1/(b.time-a.time);
}

float Block::
nFrequencies()
{
    return pow(2, -ref.log2_samples_size[1]);
}


///// HEIGHTMAP::COLLECTION

Collection::
Collection( Signal::pWorker worker )
:   worker( worker ),
    _samples_per_block( 1<<7 ),
    _scales_per_block( 1<<8 )
{

}

void Collection::
    put( Signal::pBuffer b, Signal::pSource s)
{
    try {
        TaskTimer tt(TaskTimer::LogVerbose, "%s: Putting buffer [%u,%u]", __FUNCTION__, b->sample_offset, b->sample_offset+b->number_of_samples());

        { // If b extends source
            static unsigned L = 0;
            unsigned nL = s->number_of_samples();
            if (nL > L)
            {
                unsigned std = Tfr::CwtSingleton::instance()->wavelet_std_samples( worker->source()->sample_rate());
                //std;
                if (L > std*8)
                {
                    // Invalidate previous samples
                    Signal::SamplesIntervalDescriptor sid(
                            L-std*4, L-std*0);
                    this->updateInvalidSamples( sid );
                }

                L = nL;
            }
        }

        // Get a chunk for this block
        Tfr::pChunk chunk = getChunk( b, s );

        // Update all blocks with this new chunk
        BOOST_FOREACH( pBlock& pb, _cache ) {
            Position p1, p2;
            pb->ref.getArea(p1, p2);

            if (p2.time > b->start() && p1.time < b->start()+b->length())
            {
                mergeBlock( pb, chunk, 0 );
                computeSlope( pb, 0 );
            }
            pb->glblock->unmap();
        }
    } catch (const CudaException &) {
        // silently catch but don't bother to do anything
    } catch (const GlException &) {
        // silently catch but don't bother to do anything
    }
}


void Collection::
scales_per_block(unsigned v)
{
    _cache.clear();
    _scales_per_block=v;
}

void Collection::
samples_per_block(unsigned v)
{
    _cache.clear();
    _samples_per_block=v;
}

unsigned Collection::
read_unfinished_count()
{
    unsigned t = _unfinished_count;
    _unfinished_count = 0;
    _frame_counter++;

    BOOST_FOREACH( pBlock& b, _cache ) {
        b->glblock->unmap();
    }

    return t;
}

Position Collection::
min_sample_size()
{
    unsigned FS = worker->source()->sample_rate();
    return Position( 1.f/FS,
                     1.f/Tfr::CwtSingleton::instance()->nScales( FS ) );
}

Position Collection::
max_sample_size()
{
    Signal::pSource wf = worker->source();
    float length = wf->length();
    Position minima=min_sample_size();

    return Position( std::max(minima.time, 2.f*length/_samples_per_block),
                     std::max(minima.scale, 1.f/_scales_per_block) );
}

/*Reference Collection::findReferenceCanonical( Position p, Position sampleSize )
{
    // doesn't ASSERT(r.containsSpectrogram() && !r.toLarge())
    Reference r(this);

    if (p.time < 0) p.time=0;
    if (p.scale < 0) p.scale=0;

    r.log2_samples_size = tvector<2,int>( floor(log2( sampleSize.time )), floor(log2( sampleSize.scale )) );
    r.block_index = tvector<2,unsigned>(p.time / _samples_per_block * pow(2, -r.log2_samples_size[0]),
                                        p.scale / _scales_per_block * pow(2, -r.log2_samples_size[1]));

    return r;
}*/

Reference Collection::
findReference( Position p, Position sampleSize )
{
    Reference r(this);

    // make sure the reference becomes valid
    Signal::pSource wf = worker->source();
    float length = wf->length();

    // Validate requested sampleSize
    sampleSize.time = fabs(sampleSize.time);
    sampleSize.scale = fabs(sampleSize.scale);

    Position minSampleSize = min_sample_size();
    Position maxSampleSize = max_sample_size();
    if (sampleSize.time > maxSampleSize.time)
        sampleSize.time = maxSampleSize.time;
    if (sampleSize.scale > maxSampleSize.scale)
        sampleSize.scale = maxSampleSize.scale;
    if (sampleSize.time < minSampleSize.time)
        sampleSize.time = minSampleSize.time;
    if (sampleSize.scale < minSampleSize.scale)
        sampleSize.scale = minSampleSize.scale;

    // Validate requested poistion
    if (p.time < 0) p.time=0;
    if (p.time > length) p.time=length;
    if (p.scale < 0) p.scale=0;
    if (p.scale > 1) p.scale=1;

    // Compute sample size
    r.log2_samples_size = tvector<2,int>( floor(log2( sampleSize.time )), floor(log2( sampleSize.scale )) );
    r.block_index = tvector<2,unsigned>(0,0);
    //printf("%d %d\n", r.log2_samples_size[0], r.log2_samples_size[1]);

    // Validate sample size
    Position a,b; r.getArea(a,b);
    if (b.time < minSampleSize.time*_samples_per_block )                r.log2_samples_size[0]++;
    if (b.scale < minSampleSize.scale*_scales_per_block )               r.log2_samples_size[1]++;
    if (b.time > maxSampleSize.time*_samples_per_block && 0<length )    r.log2_samples_size[0]--;
    if (b.scale > maxSampleSize.scale*_scales_per_block )               r.log2_samples_size[1]--;
    //printf("%d %d\n", r.log2_samples_size[0], r.log2_samples_size[1]);

    // Compute chunk index
    r.block_index = tvector<2,unsigned>(p.time / _samples_per_block * pow(2.f, -r.log2_samples_size[0]),
                                        p.scale / _scales_per_block * pow(2.f, -r.log2_samples_size[1]));

    // Validate chunk index
    r.getArea(a,b);
    if (a.time >= length && 0<length)   r.block_index[0]--;
    if (a.scale == 1)                   r.block_index[1]--;

    // Test result
    // ASSERT(r.containsSpectrogram() && !r.toLarge());

    return r;
}

pBlock Collection::
        getBlock( Reference ref )
{
    // Look among cached blocks for this reference
    pBlock block;
    BOOST_FOREACH( pBlock& b, _cache ) {
        if (b->ref == ref) {
            block = b;
            b->frame_number_last_used = _frame_counter;

            break;
        }
    }

    pBlock result = block;
    try {
        if (0 == block.get()) {
            block = createBlock( ref );
        }

        if (0 != block.get()) 
        {
            Signal::SamplesIntervalDescriptor refInt = block->ref.getInterval();
            if (!(refInt-=block->valid_samples).isEmpty())
                _unfinished_count++;
        }

        result = block;
    } catch (const CudaException &) {
    } catch (const GlException &) {
    }

    return result;
}

void Collection::
        gc()
{
    if (_cache.empty())
        return;

    unsigned latestFrame = _cache[0]->frame_number_last_used;
    BOOST_FOREACH( pBlock& b, _cache ) {
        if (latestFrame < b->frame_number_last_used)
            latestFrame = b->frame_number_last_used;
    }

    for (std::vector<pBlock>::iterator itr = _cache.begin(); itr!=_cache.end(); itr++)
    {
        if ((*itr)->frame_number_last_used < latestFrame) {
            Position a,b;
            (*itr)->ref.getArea(a,b);
            TaskTimer tt("Release block [%g, %g]", a.time, b.time);
            itr = _cache.erase(itr);
        }
    }
}


void Collection::
        updateInvalidSamples( Signal::SamplesIntervalDescriptor sid )
{
    BOOST_FOREACH( const Signal::SamplesIntervalDescriptor::Interval &i, sid.intervals() )
    {
        TaskTimer tt(TaskTimer::LogVerbose, "Invalidating buffer [%u,%u]", i.first, i.last);
    }

    // canonical
    // BOOST_FOREACH( pBlock& b, _cache )
    // {
    //    b->valid_samples -= sid;
    // }

    for (std::vector<pBlock>::iterator i = _cache.begin(); i!=_cache.end(); )
    {
        (*i)->valid_samples -= sid;

        // Remove cached blocks which are completely invalid
/*        if ((*i)->valid_samples.isEmpty())
            i = _cache.erase( i );
        else*/
            i++;
    }
}

Signal::SamplesIntervalDescriptor Collection::
        getMissingSamples()
{
    Signal::SamplesIntervalDescriptor r;

    BOOST_FOREACH( pBlock& b, _cache )
    {
        Signal::SamplesIntervalDescriptor i ( b->ref.getInterval() );

        i -= b->valid_samples;
        r |= i;
    }

    return r;
}

////// private


pBlock Collection::
attempt( Reference ref )
{
    try {
        pBlock attempt( new Block(ref));
        attempt->glblock.reset( new GlBlock( this ));
        GlBlock::pHeight h = attempt->glblock->height();
        GlBlock::pSlope sl = attempt->glblock->slope();
        attempt->glblock->unmap();

        GlException_CHECK_ERROR();
        CudaException_CHECK_ERROR();

        return attempt;
    }
    catch (const CudaException& )
    { }
    catch (const GlException& )
    { }
    return pBlock();
}


pBlock Collection::
createBlock( Reference ref )
{
    Position a,b;
    ref.getArea(a,b);
    TaskTimer tt("Creating a new block [%g, %g]",a.time,b.time);
    // Try to allocate a new block
    pBlock block = attempt( ref );

    if ( 0 == block.get() && !_cache.empty()) {
        tt.info("Memory allocation failed, overwriting some older block");
        gc();
        block = attempt( ref );
    }

    if ( 0 == block.get()) {
        tt.info("Failed");
        return pBlock(); // return null-pointer
    }

    pBlock result;
    try {
        // set to zero
        GlBlock::pHeight h = block->glblock->height();
        cudaMemset( h->data->getCudaGlobal().ptr(), 0, h->data->getSizeInBytes1D() );

        if ( 1 /* create from others */ ) {
            TaskTimer tt(TaskTimer::LogVerbose, "Stubbing new block");

            // fill block by STFT
            {
                TaskTimer tt(TaskTimer::LogVerbose, "stft");
                prepareFillStft( block );
            }

            /*if (0) {
                TaskTimer tt(TaskTimer::LogVerbose, "Preventing wavelet transform");
                Position a,b;
                block->ref.getArea(a,b);
                unsigned start = a.time * worker()->source()->sample_rate();
                unsigned end = b.time * worker()->source()->sample_rate();

                for (Transform::ChunkIndex n = _transform->getChunkIndex(start);
                     n <= _transform->getChunkIndex(end);
                     n++)
                {
                    block->valid_chunks.insert( n );
                }
            }*/

            if (1) {
                TaskTimer tt(TaskTimer::LogVerbose, "Fetching details");
                // start with the blocks that are just slightly more detailed
                BOOST_FOREACH( pBlock& b, _cache ) {
                    if (block->ref.log2_samples_size[0] == b->ref.log2_samples_size[0]+1 ||
                        block->ref.log2_samples_size[1] == b->ref.log2_samples_size[1]+1)
                    {
                        mergeBlock( block, b, 0 );
                    }
                }
            }

            if (0) {
                TaskTimer tt(TaskTimer::LogVerbose, "Fetching more details");
                // then try using the blocks that are even more detailed
                BOOST_FOREACH( pBlock& b, _cache ) {
                    if (block->ref.log2_samples_size[0] > b->ref.log2_samples_size[0] +1 ||
                        block->ref.log2_samples_size[1] > b->ref.log2_samples_size[1] +1)
                    {
                        mergeBlock( block, b, 0 );
                    }
                }
            }

            if (1) {
                TaskTimer tt(TaskTimer::LogVerbose, "Fetching low resolution");
                // then try to upscale other blocks
                BOOST_FOREACH( pBlock& b, _cache ) {
                    if (block->ref.log2_samples_size[0] < b->ref.log2_samples_size[0] ||
                        block->ref.log2_samples_size[1] < b->ref.log2_samples_size[1] )
                    {
                        mergeBlock( block, b, 0 );
                    }
                }
            }

        } else if ( 0 /* set dummy values */ ) {
            GlBlock::pHeight h = block->glblock->height();
            float* p = h->data->getCpuMemory();
            for (unsigned s = 0; s<_samples_per_block; s++) {
                for (unsigned f = 0; f<_scales_per_block; f++) {
                    p[ f*_samples_per_block + s] = sin(s*10./_samples_per_block)*cos(f*10./_scales_per_block);
                }
            }
        }

        computeSlope( block, 0 );
        result = block;
    }
    catch (const CudaException& )
    { }
    catch (const GlException& )
    { }

    if ( 0 == result.get())
        return pBlock(); // return null-pointer

    _cache.push_back( result );

    return result;
}

void Collection::
        computeSlope( pBlock block, unsigned cuda_stream )
{
    GlBlock::pHeight h = block->glblock->height();
    ::cudaCalculateSlopeKernel( h->data->getCudaGlobal().ptr(), block->glblock->slope()->data->getCudaGlobal().ptr(), _samples_per_block, _scales_per_block, cuda_stream );
}

void Collection::
        prepareFillStft( pBlock block )
{
    Position a, b;
    block->ref.getArea(a,b);
    float tmin = Tfr::CwtSingleton::instance()->min_hz();
    float tmax = Tfr::CwtSingleton::instance()->max_hz( worker->source()->sample_rate() );

    Tfr::Stft trans;
    Signal::pSource fast_source = Signal::Operation::fast_source( worker->source() );

    unsigned first_sample = (unsigned)(a.time*fast_source->sample_rate()),
             n_samples = (unsigned)((b.time-a.time)*fast_source->sample_rate());
    first_sample = ((first_sample-1)/trans.chunk_size+1)*trans.chunk_size;
    n_samples = ((n_samples-1)/trans.chunk_size+1)*trans.chunk_size;

    Signal::pBuffer buff = fast_source->readFixedLength( first_sample, n_samples );

    Signal::pBuffer stft = trans( buff );

    float out_min_hz = exp(log(tmin) + (a.scale*(log(tmax)-log(tmin)))),
          out_max_hz = exp(log(tmin) + (b.scale*(log(tmax)-log(tmin)))),
          in_max_hz = tmax;
    float in_min_hz = in_max_hz / 4/trans.chunk_size;

    float out_stft_size = (trans.chunk_size/(float)stft->sample_rate)*block->sample_rate();

    float out_offset = (a.time - (stft->sample_offset/(float)stft->sample_rate)) * block->sample_rate();

    ::expandCompleteStft( stft->waveform_data->getCudaGlobal(),
                  block->glblock->height()->data->getCudaGlobal(),
                  out_min_hz,
                  out_max_hz,
                  out_stft_size,
                  out_offset,
                  in_min_hz,
                  in_max_hz,
                  trans.chunk_size,
                  0);
}


void Collection::
        updateSlope( pBlock block, unsigned cuda_stream )
{
    GlBlock::pHeight h = block->glblock->height();
    cudaCalculateSlopeKernel( h->data->getCudaGlobal().ptr(), block->glblock->slope()->data->getCudaGlobal().ptr(), _samples_per_block, _scales_per_block, cuda_stream );
}

void Collection::
        mergeBlock( pBlock outBlock, Tfr::pChunk inChunk, unsigned cuda_stream, bool save_in_prepared_data)
{
    boost::shared_ptr<GpuCpuData<float> > outData;

    // Find out what intervals that match
    Signal::SamplesIntervalDescriptor::Interval outInterval =  outBlock->ref.getInterval();
    Signal::SamplesIntervalDescriptor::Interval inInterval =  inChunk->getInterval();

    Signal::SamplesIntervalDescriptor transferDesc = inInterval;
    transferDesc &= outInterval;

    // Remove already computed intervals
    transferDesc -= outBlock->valid_samples;

    // If block is already up to date, abort merge
    if (transferDesc.isEmpty())
        return;

    // If mergeBlock is called by a separate worker thread it will also have a separate cuda context
    // and thus cannot write directly to the cuda buffer that is mapped to the rendering thread's
    // OpenGL buffer. Instead merge inChunk to a prepared_data buffer in outBlock now, move data to CPU
    // and update the OpenGL block the next time it is rendered.
    if (!save_in_prepared_data)
        outData = outBlock->glblock->height()->data;
    else {
        if (outBlock->prepared_data)
            outData = outBlock->prepared_data;
        else
            outData.reset( new GpuCpuData<float>(0, make_cudaExtent( _samples_per_block, _scales_per_block, 1 ), GpuCpuVoidData::CudaGlobal ) );
    }

    float in_sample_rate = inChunk->sample_rate;
    float out_sample_rate = outBlock->sample_rate();
    float in_frequency_resolution = inChunk->nScales();
    float out_frequency_resolution = outBlock->nFrequencies();

    BOOST_FOREACH( Signal::SamplesIntervalDescriptor::Interval transfer, transferDesc.intervals())
    {
        // Find resolution and offest for the two blocks to be merged
        float in_offset = transfer.first - inInterval.first;
        float out_offset = transfer.first - outInterval.first;

        // Add offset for redundant samples in inChunk
        in_offset += inChunk->first_valid_sample;

        // Rescale out_offset to out_sample_rate (samples are originally
        // described in the inChunk sample rate)
        out_offset *= out_sample_rate / in_sample_rate;

        // Invoke CUDA kernel execution to merge blocks
        ::blockMergeChunk( inChunk->transform_data->getCudaGlobal(),
                           outData->getCudaGlobal(),

                           in_sample_rate,
                           out_sample_rate,
                           in_frequency_resolution,
                           out_frequency_resolution,
                           in_offset,
                           out_offset,
                           transfer.last - transfer.first,
                           cuda_stream);

        outBlock->valid_samples |= transfer;

        TaskTimer tt(TaskTimer::LogVerbose, "Inserting chunk [%u,%u]", transfer.first, transfer.last);
    }

    if (save_in_prepared_data) {
        outData->getCpuMemory();
        outData->freeUnused();
        outBlock->prepared_data = outData;
    }
}

void Collection::
        mergeBlock( pBlock outBlock, pBlock inBlock, unsigned cuda_stream )
{
    Signal::SamplesIntervalDescriptor::Interval inInterval = inBlock->ref.getInterval();
    Signal::SamplesIntervalDescriptor::Interval outInterval = outBlock->ref.getInterval();

    // Find out what intervals that match
    Signal::SamplesIntervalDescriptor transferDesc = inBlock->valid_samples;
    transferDesc &= outInterval;

    // Remove already computed intervals
    transferDesc -= outBlock->valid_samples;

    // If block is already up to date, abort merge
    if (transferDesc.isEmpty()) {
        return;
    }

    float in_sample_rate = inBlock->sample_rate();
    float out_sample_rate = outBlock->sample_rate();
    unsigned signal_sample_rate = worker->source()->sample_rate();
    float in_frequency_resolution = inBlock->nFrequencies();
    float out_frequency_resolution = outBlock->nFrequencies();

    GlBlock::pHeight out_h = outBlock->glblock->height();
    GlBlock::pHeight in_h = inBlock->glblock->height();

    BOOST_FOREACH( const Signal::SamplesIntervalDescriptor::Interval& transfer, transferDesc.intervals())
    {
        float in_offset = transfer.first - inInterval.first;
        float out_offset = transfer.first - outInterval.first;
        float in_valid_samples = transfer.last - transfer.first;

        // Rescale to proper sample rates (samples are originally
        // described in the signal_sample_rate)
        in_offset *= in_sample_rate / signal_sample_rate;
        out_offset *= out_sample_rate / signal_sample_rate;
        in_valid_samples *= in_sample_rate / signal_sample_rate;

        ::blockMerge( in_h->data->getCudaGlobal(),
                      out_h->data->getCudaGlobal(),

                      in_sample_rate,
                      out_sample_rate,
                      in_frequency_resolution,
                      out_frequency_resolution,
                      in_offset,
                      out_offset,
                      in_valid_samples,
                      cuda_stream);

        // Validate region of block if inBlock was source of higher resolution than outBlock
        if (inBlock->ref.log2_samples_size[0] <= outBlock->ref.log2_samples_size[0] &&
            inBlock->ref.log2_samples_size[1] <= outBlock->ref.log2_samples_size[1])
        {
            outBlock->valid_samples |= transfer;
            TaskTimer tt(TaskTimer::LogVerbose, "Using block [%u,%u]", transfer.first, transfer.last);
        }
    }

    // These inblocks won't be rendered and thus unmapped very soon. outBlock will however be unmapped
    // very soon as it was requested for rendering.
    inBlock->glblock->unmap();
}

} // namespace Heightmap
