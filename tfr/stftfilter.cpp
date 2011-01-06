#include "stftfilter.h"
#include "stft.h"

#include <neat_math.h>
#include <stringprintf.h>
#include <CudaException.h>
#include <memory.h>

//#define TIME_StftFilter
#define TIME_StftFilter if(0)

using namespace Signal;

namespace Tfr {


StftFilter::
        StftFilter(pOperation source, pTransform t)
:   Filter(source),
    exclude_end_block(false)
{
    if (!t)
        t = Stft::SingletonP();

    BOOST_ASSERT( dynamic_cast<Stft*>(t.get()));

    _transform = t;
}


ChunkAndInverse StftFilter::
        computeChunk( const Signal::Interval& I )
{
    //((Stft*)transform().get())->set_approximate_chunk_size( 1 << 12 );
    unsigned chunk_size = ((Stft*)transform().get())->chunk_size();
    // Add a margin to make sure that the STFT is computed for one block before
    // and one block after the signal. This makes it possible to do proper
    // interpolations so that there won't be any edges between blocks

    unsigned first_chunk = 0,
             last_chunk = (I.last + 2.5*chunk_size)/chunk_size;

    if (I.first >= 1.5*chunk_size)
        first_chunk = (I.first - 1.5*chunk_size)/chunk_size;

    ChunkAndInverse ci;

    Interval chunk_interval (
                first_chunk*chunk_size,
                last_chunk*chunk_size);
    if (exclude_end_block)
    {
        if (chunk_interval.last>number_of_samples())
        {
            last_chunk = number_of_samples()/chunk_size;
            if (1+first_chunk<last_chunk)
                chunk_interval.last = last_chunk*chunk_size;
        }
    }
    ci.inverse = _source->readFixedLength( chunk_interval );

    // Compute the continous wavelet transform
    ci.chunk = (*transform())( ci.inverse );

    return ci;
}


pTransform StftFilter::
        transform() const
{
    return _transform ? _transform : Stft::SingletonP();
}


void StftFilter::
        transform( pTransform t )
{
    if (0 == dynamic_cast<Stft*>(t.get ()))
        throw std::invalid_argument("'transform' must be an instance of Cwt");

    // even if '_transform == t || _transform == transform()' the client
    // probably wants to reset everything when transform( t ) is called
    _invalid_samples = Intervals::Intervals_ALL;

    _transform = t;
}

} // namespace Signal
