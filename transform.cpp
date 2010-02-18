#include "transform.h"
#include <cuda_runtime.h>
#include <cufft.h>
#include "CudaException.h"
#include <math.h>
#include "CudaProperties.h"
#include "throwInvalidArgument.h"
#include <iostream>
#include "Statistics.h"
#include "StatisticsRandom.h"
#include <string.h>
#include "wavelet.cu.h"
#include <msc_stdc.h>

using namespace std;

Transform::Transform( pWaveform waveform, unsigned channel, unsigned samples_per_chunk, unsigned scales_per_octave, float wavelet_std_t )
:   _original_waveform( waveform ),
    _channel( channel ),
    _scales_per_octave( scales_per_octave ),
    _samples_per_chunk( samples_per_chunk ),
    _wavelet_std_t( wavelet_std_t ),
    _min_hz(20),
    _max_hz( waveform->sample_rate()/2 ),
    _fft_many(-1),
    _fft_single(-1),
    _fft_width(0)
{
    /*
    Each chunk will then have the size
        chunk_byte_size = sizeof(float)*2 * _scales_per_octave
                        * _samples_per_chunk * number_of_octaves
    Which for default values
        scales_per_octave = 50,
        samples_per_chunk = 1<<14
        number_of_octaves=log2(20000)-log2(20) ~ 10
    is
        chunk_byte_size = 62.5 MB
    Which in turn means that a graphics card with 512 MB of memory will have
    space for roughly 6 chunks (leaving about 137 MB for other purposes).
    */

    _t1 = _f1 = _t2 = _f2 = 0;
    CudaProperties::printInfo(CudaProperties::getCudaDeviceProp());
}

Transform::~Transform()
{

    gc();
}


Transform::ChunkIndex Transform::getChunkIndex( unsigned including_sample ) {
    return including_sample / _samples_per_chunk;
}


/*
getChunk initializes asynchronous computation if 0!=stream (inherited
behaviour from cuda). Also note, from NVIDIA CUDA Programming Guide section
3.2.6.1, v2.3 Stream:

"Two commands from different streams cannot run concurrently if either a page-
locked host memory allocation, a device memory allocation, a device memory set,
a device ↔ device memory copy, or any CUDA command to stream 0 is called in-
between them by the host thread."

That is, a call to getChunk shall never result in any of the operations
mentioned above, which basically leaves kernel execution and page-locked
host ↔ device memory copy. For this purpose, a page-locked chunk of memory
is allocated beforehand and reserved for the Waveform_chunk. Previous copies
with the page-locked chunk is synchronized on beforehand.
*/
pTransform_chunk Transform::getChunk( ChunkIndex n, cudaStream_t stream ) {
    if (_oldChunks.find(n) != _oldChunks.end()) {
        if (!_oldChunks[n]->modified && _oldChunks[n].unique())
            /* only use old chunks if they're not modified and unused */
            return _oldChunks[n];
    }

    /* The waveform resides in CPU memory, beacuse in total we might have hours
       of data which would instantly waste the entire GPU memory. In those
       cases it is likely though that the OS chooses to page parts of the
       waveform. Therefore this takes in general an undefined amount of time
       before it returns.

       However, we optimize for the assumption that it does reside in readily
       available cpu memory.
    */
    pWaveform_chunk wave = _original_waveform->getChunk(
            (unsigned)std::max(0.f, _samples_per_chunk*n - _wavelet_std_t*_original_waveform->sample_rate()),
            (unsigned)(_samples_per_chunk + 2*_wavelet_std_t*_original_waveform->sample_rate()),
            _channel,
            Waveform_chunk::Interleaved_Complex);

    /*
    static cudaStream_t previousStream = -1;
    if (0 < previousStream) {
        cudaStreamSynchronize( previousStream );
    }
    previousStream = stream;
    */

    /* performing the transform might require allocating memory, */
    pTransform_chunk wt = computeTransform( wave, stream );

    /* allocateChunk is more prone to reuse old caches than to actually
       allocate any memory */
    pTransform_chunk clamped = allocateChunk( n );
    clampTransform( clamped, wt, stream );

    _oldChunks[n] = clamped;
    return clamped;
}

void Transform::setInverseArea(float t1, float f1, float t2, float f2) {
    _inverse_waveform.reset();
    switch(2)
    {
    case 1: // square
        _t1 = min(t1, t2);
        _f1 = min(f1, f2);
        _t2 = max(t1, t2);
        _f2 = max(f1, f2);
        break;
    case 2: // circle
        _t1 = t1;
        _f1 = f1;
        _t2 = t2;
        _f2 = f2;
        break;
    }
}

pWaveform Transform::get_inverse_waveform()
{
    if(0 == _inverse_waveform) {
        _inverse_waveform.reset(new Waveform());
        _inverse_waveform->setChunk( computeInverse(0, _original_waveform->length() ) );
    }
    return _inverse_waveform;
}

pWaveform_chunk Transform::computeInverse( float start, float end)
{
    unsigned n = original_waveform()->number_of_samples();

    if(start<0) start=0;
    pWaveform_chunk r( new Waveform_chunk());
    r->sample_rate = original_waveform()->sample_rate();
    r->sample_offset = min((float)n, r->sample_rate*start);
    n -= r->sample_offset;
    if (start<=end)
        n = min((float)n, r->sample_rate*(end-start));
    fprintf(stdout, "rate = %d, offs = %d, n = %d, orgn = %d\n", r->sample_rate, r->sample_offset, n, original_waveform()->number_of_samples());
    fflush(stdout);

    r->waveform_data.reset( new GpuCpuData<float>(0, make_cudaExtent(n, 1, 1), GpuCpuVoidData::CudaGlobal) );
    cudaMemset(r->waveform_data->getCudaGlobal().ptr(), 0, r->waveform_data->getSizeInBytes1D());

    for(Transform::ChunkIndex n = getChunkIndex(start);
         n*samples_per_chunk() < end*r->sample_rate;
         n++)
    {
        pWaveform_chunk chunk = computeInverse( getChunk(n), 0 );

        // merge
        unsigned out_offs = (chunk->sample_offset > r->sample_offset)? chunk->sample_offset - r->sample_offset : 0;
        unsigned in_offs = (r->sample_offset > chunk->sample_offset)? r->sample_offset - chunk->sample_offset : 0;
        unsigned count = chunk->waveform_data->getNumberOfElements().width;
        if (count>in_offs)
            count -= in_offs;
        else
            continue;

        fprintf(stdout, "out_offs = %d, in_offs = %d, count = %d\n", out_offs, in_offs, count);
        fflush(stdout);

        cudaMemcpy( &r->waveform_data->getCudaGlobal().ptr()[ out_offs ],
                    &chunk->waveform_data->getCudaGlobal().ptr()[ in_offs ],
                    count*sizeof(float), cudaMemcpyDeviceToDevice );
    }

    return r;
}

pWaveform_chunk Transform::computeInverse( pTransform_chunk chunk, cudaStream_t stream ) {
    cudaExtent sz = make_cudaExtent( chunk->nSamples(), 1, 1);

    pWaveform_chunk r( new Waveform_chunk());
    r->sample_offset = chunk->sample_offset;
    r->sample_rate = chunk->sample_rate;
    r->waveform_data.reset( new GpuCpuData<float>(0, sz, GpuCpuVoidData::CudaGlobal) );

    uint4 area = make_uint4(
            max(0.f, min((float)UINT_MAX, _t1 * _inverse_waveform->sample_rate())),
            max(0.f, min((float)UINT_MAX, _f1 * nScales())),
            max(0.f, min((float)UINT_MAX, _t2 * _inverse_waveform->sample_rate())),
            max(0.f, min((float)UINT_MAX, _f2 * nScales())));

    {
        TaskTimer tt(__FUNCTION__);

        // summarize them all
        ::wtInverse( chunk->transform_data->getCudaGlobal().ptr(),
                     r->waveform_data->getCudaGlobal().ptr(),
                     chunk->transform_data->getNumberOfElements(),
                     area, stream );
    }

/*    {
        TaskTimer tt("inverse corollary");

        size_t n = r->waveform_data->getNumberOfElements1D();
        float* data = r->waveform_data->getCpuMemory();
        pWaveform_chunk originalChunk = _original_waveform->getChunk(chunk->sample_offset, chunk->nSamples(), _channel);
        float* orgdata = originalChunk->waveform_data->getCpuMemory();

        double sum = 0, orgsum=0;
        for (size_t i=0; i<n; i++) {
            sum += fabsf(data[i]);
        }
        for (size_t i=0; i<n; i++) {
            orgsum += fabsf(orgdata[i]);
        }
        float scale = orgsum/sum;
        for (size_t i=0; i<n; i++)
            data[i] *= scale;
        tt.info("scales %g, %g, %g", sum, orgsum, scale);

        r->writeFile("outtest.wav");
    }*/
    return r;
}


void Transform::gc() {
    _oldChunks.clear();
    _intermediate_wt.reset();

            // Destroy CUFFT context
    if (_fft_many == (cufftHandle)-1)
        cufftDestroy(_fft_many);
    if (_fft_single == (cufftHandle)-1)
        cufftDestroy(_fft_single);
}


void Transform::channel( unsigned value ) {
    if (value==_channel) return;
    gc(); // invalidate allocated memory chunks
    _channel=value;
}


void Transform::scales_per_octave( unsigned value ) {
    if (value==_scales_per_octave) return;
    gc();
    _scales_per_octave=value;
}


void Transform::samples_per_chunk( unsigned value ) {
    if (value==_samples_per_chunk) return;
    gc();
    _samples_per_chunk=value;
}


void Transform::wavelet_std_t( float value ) {
    if (value==_wavelet_std_t) return;
    gc();
    _wavelet_std_t=value;
}


void Transform::original_waveform( pWaveform value ) {
    if (value==_original_waveform) return;
    gc();
    _original_waveform=value;
}

float Transform::number_of_octaves() const {
    return log2(_max_hz) - log2(_min_hz);
}

void Transform::min_hz(float value) {
    if (value == _min_hz) return;
    gc();
    _min_hz = value;
}
void Transform::max_hz(float value) {
    if (value == _max_hz) return;
    gc();
    _max_hz = value;
}


pTransform_chunk Transform::allocateChunk( ChunkIndex n )
{
    if (_oldChunks.find(n) != _oldChunks.end()) {
        if (!_oldChunks[n].unique())
            _oldChunks.erase(_oldChunks.find(n));
        else
            return _oldChunks[n];
    }

    // look for caches no longer in use
    pTransform_chunk chunk = releaseChunkFurthestAwayFrom( n );

    if (!chunk) {
        // or allocate a new chunk
        chunk = pTransform_chunk ( new Transform_chunk());
        chunk->transform_data.reset(new GpuCpuData<float2>(0,
                make_uint3(_samples_per_chunk, number_of_octaves()*_scales_per_octave, 1), GpuCpuVoidData::CudaGlobal ));
        chunk->min_hz = 20;
        chunk->max_hz = chunk->sample_rate/2;
        chunk->sample_rate = _original_waveform->sample_rate();
    }
    chunk->sample_offset = _samples_per_chunk*n;

    _oldChunks[n] = chunk;
    return chunk;
}


pTransform_chunk Transform::releaseChunkFurthestAwayFrom( ChunkIndex n )
{
    ChunkMap::iterator proposal;
    while (!_oldChunks.empty()) {
		ChunkMap::iterator last = _oldChunks.end();
		last--;
        proposal = abs((int)n-(int)_oldChunks.begin()->first)
                 < abs((int)n-(int)last->first)
                 ? _oldChunks.begin()
                 : last;

        if ( proposal->second.unique()) {
            pTransform_chunk r = proposal->second;
            _oldChunks.erase( proposal );
            return r;

        } else {
            _oldChunks.erase( proposal );
        }
    }
    return pTransform_chunk();
}


static void cufftSafeCall( cufftResult_t cufftResult) {
    if (cufftResult != CUFFT_SUCCESS) {
        ThrowInvalidArgument( cufftResult );
    }
}


pTransform_chunk Transform::computeTransform( pWaveform_chunk waveform_chunk, cudaStream_t stream )
{
    // Compute required size of transform
    {
        // Compute number of scales to use
        float octaves = number_of_octaves();
        unsigned nFrequencies = _scales_per_octave*octaves;

        // The waveform must be complex interleaved
        if (Waveform_chunk::Interleaved_Complex != waveform_chunk->interleaved())
            waveform_chunk = waveform_chunk->getInterleaved( Waveform_chunk::Interleaved_Complex );

        cudaExtent requiredFtSz = make_cudaExtent( waveform_chunk->waveform_data->getNumberOfElements().width/2, 1, 1 );
        // The in-signal is be padded to a power of 2 for faster calculations (or rather, "power of a small prime")
        // TODO: measure time differences with other sizes
        // TODO: test these different sizes
        //requiredFtSz.width = (1 << ((unsigned)ceil(log2(requiredFtSz.width))));
        requiredFtSz.width = (1 << ((unsigned)ceil(log2((float)requiredFtSz.width+1))));
        cudaExtent requiredWtSz = make_cudaExtent( requiredFtSz.width, nFrequencies, 1 );

        if (_fft_width != requiredFtSz.width)
        {
            gc();
            _fft_width = requiredFtSz.width;
        }

        if (_intermediate_wt && _intermediate_wt->transform_data->getNumberOfElements() != requiredWtSz)
            _intermediate_wt.reset();

        if (_intermediate_ft && _intermediate_ft->getNumberOfElements()!=requiredFtSz)
            _intermediate_ft.reset();

        if (!_intermediate_wt) {
            // allocate a new chunk
            pTransform_chunk chunk = pTransform_chunk ( new Transform_chunk());

            bool tryagain = true;
            while (tryagain) try {
                tryagain = !_oldChunks.empty();
                chunk->transform_data.reset(new GpuCpuData<float2>( 0, requiredWtSz, GpuCpuVoidData::CudaGlobal ));
                break;
            } catch ( const CudaException& x ) {
                if (x.getCudaError() == cudaErrorMemoryAllocation) {
                    unsigned chunkIndex = waveform_chunk->sample_offset /_samples_per_chunk;
                    releaseChunkFurthestAwayFrom( chunkIndex );
                }
                else
                    throw;
            }

            _intermediate_wt = chunk;
        }

        if (!_intermediate_ft) {
            bool tryagain = true;
            while (tryagain) try {
                tryagain = !_oldChunks.empty();
                _intermediate_ft.reset(new GpuCpuData<float2>( 0, requiredFtSz, GpuCpuVoidData::CudaGlobal ));
                break;
            } catch ( const CudaException& x ) {
                if (x.getCudaError() == cudaErrorMemoryAllocation) {
                    unsigned chunkIndex = waveform_chunk->sample_offset /_samples_per_chunk;
                    releaseChunkFurthestAwayFrom( chunkIndex );
                }
                else
                    throw;
            }
        }
    }  // Compute required size of transform, _intermediate_wt

    TaskTimer tt(__FUNCTION__);
    {
        cufftComplex* d = _intermediate_ft->getCudaGlobal().ptr();
        cudaMemset( d, 0, _intermediate_ft->getSizeInBytes1D() );
        cudaMemcpy( d+2, // TODO test the significance of this "2"
                    waveform_chunk->waveform_data->getCudaGlobal().ptr(),
                    waveform_chunk->waveform_data->getSizeInBytes().width,
                    cudaMemcpyDeviceToDevice);
        TaskTimer tt("start");

        // Transform signal
        if (_fft_single == (cufftHandle)-1)
            cufftSafeCall(cufftPlan1d(&_fft_single, _intermediate_ft->getNumberOfElements().width, CUFFT_C2C, 1));

        cufftSafeCall(cufftSetStream(_fft_single, stream));
        cufftSafeCall(cufftExecC2C(_fft_single, d, d, CUFFT_FORWARD));

//        CudaException_ThreadSynchronize();
    }

    {
        TaskTimer tt("computing");
        _intermediate_wt->sample_rate =  _original_waveform->sample_rate();
        _intermediate_wt->sample_offset = waveform_chunk->sample_offset;
        _intermediate_wt->min_hz = 20;
        _intermediate_wt->max_hz = waveform_chunk->sample_rate/2;

        ::wtCompute( _intermediate_ft->getCudaGlobal().ptr(),
                     _intermediate_wt->transform_data->getCudaGlobal().ptr(),
                     _intermediate_wt->sample_rate,
                     _intermediate_wt->min_hz,
                     _intermediate_wt->max_hz,
                     _intermediate_wt->transform_data->getNumberOfElements() );

//        CudaException_ThreadSynchronize();
    }

    {
        TaskTimer tt("inverse fft");

        // Transform signal back
        GpuCpuData<float2>* g = _intermediate_wt->transform_data.get();
        cudaExtent n = g->getNumberOfElements();
        cufftComplex *d = g->getCudaGlobal().ptr();

        if (_fft_many == (cufftHandle)-1)
            cufftSafeCall(cufftPlan1d(&_fft_many, n.width, CUFFT_C2C, n.height));

        cufftSafeCall(cufftSetStream(_fft_many, stream));
        cufftSafeCall(cufftExecC2C(_fft_many, d, d, CUFFT_INVERSE));

//        CudaException_ThreadSynchronize();
    }

    return _intermediate_wt;
}


void Transform::clampTransform( pTransform_chunk out_chunk, pTransform_chunk in_transform, cudaStream_t stream )
{
    out_chunk->sample_rate = in_transform->sample_rate;
    out_chunk->min_hz = in_transform->min_hz;
    out_chunk->max_hz = in_transform->max_hz;
    size_t in_offset = out_chunk->sample_offset - in_transform->sample_offset;
    BOOST_ASSERT( out_chunk->sample_offset >= in_transform->sample_offset );
    BOOST_ASSERT( in_transform->transform_data->getNumberOfElements().width >= out_chunk->transform_data->getNumberOfElements().width );
    BOOST_ASSERT( in_transform->transform_data->getNumberOfElements().height >= out_chunk->transform_data->getNumberOfElements().height );
    BOOST_ASSERT( in_transform->transform_data->getNumberOfElements().depth >= out_chunk->transform_data->getNumberOfElements().depth );
    cudaMemset( out_chunk->transform_data->getCudaGlobal().ptr(), 0, out_chunk->transform_data->getSizeInBytes1D());
    size_t last_sample = _original_waveform->number_of_samples()>out_chunk->sample_offset?
                         _original_waveform->number_of_samples()-out_chunk->sample_offset:0;

    ::wtClamp( in_transform->transform_data->getCudaGlobal().ptr(),
               in_transform->transform_data->getNumberOfElements(),
               in_offset,
               last_sample,
               out_chunk->transform_data->getCudaGlobal().ptr(),
               out_chunk->transform_data->getNumberOfElements(),
               stream );
}
