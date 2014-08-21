#include "tfrblockupdater.h"

#include "tfr/chunk.h"
#include "opengl/blockupdater.h"
#include "float16.h"
#include "cpumemorystorage.h"

#include "neat_math.h"

namespace Heightmap {
namespace Update {

class JobChunk : public Tfr::Chunk
{
public:
    JobChunk(int w, int h, Signal::Interval coveredInterval)
        :
          Chunk(Order_row_major),
          w(w),
          h(h),
          coveredInterval(coveredInterval)
    {
    }

    unsigned nSamples() const override { return order==Order_row_major ? w : h; }
    unsigned nScales()  const override { return order==Order_row_major ? h : w; }

    // used by Texture2Fbo
    Signal::Interval getCoveredInterval() const override { return coveredInterval; }

private:
    int w, h;
    Signal::Interval coveredInterval;
};


float* computeNorm(Tfr::ChunkElement* cp, int n) {
    float *p = (float*)cp; // Overwrite 'cp'
    // This takes a while, simply because p is large so that a lot of memory has to be copied.
    // This has to be computed in sequence, no parallelization allowed.
    for (int i = 0; i<n; ++i)
        p[i] = norm(cp[i]); // Compute norm here and square root in shader.
    return p;
}


uint16_t* compress(float* inp, int w, int h, int &out_stride)
{
    uint16_t* p = (uint16_t*)(inp);
    int stride = (w+1)/2*2; // end each row on a multiple of 4 bytes
    for (int y=0; y<h; y++)
        for (int x=0; x<w; x++)
            p[y*stride + x] = Float16Compressor::compress (inp[y*w+x]);
    out_stride = stride;
    return p;
}


TfrBlockUpdater::Job::Job(Tfr::pChunk chunk, float normalization_factor, float largest_fs)
    :
      chunk(chunk),
      p(0),
      normalization_factor(normalization_factor),
      memory(chunk->transform_data)
{
    Tfr::ChunkElement *cp = chunk->transform_data->getCpuMemory ();
    int n = chunk->transform_data->numberOfElements ();
    // Compute the norm of the complex elements in the chunk prior to resampling and interpolating
    float* fp = computeNorm(cp, n);

    int data_height, data_width, data_stride, org_height, org_width;
    int stepx = 1;

    data_height = org_height = chunk->transform_data->size ().height;
    data_width = org_width = chunk->transform_data->size ().width;

    if (0 < largest_fs)
        stepx = chunk->sample_rate / largest_fs / 4;
    if (stepx < 1)
        stepx = 1;

    int offs_y = 0, offs_x = 0;
    switch (chunk->order)
    {
    case Tfr::Chunk::Order_row_major:
        org_width = chunk->nSamples ();
        org_height = chunk->nScales ();
        data_width = int_div_ceil (chunk->n_valid_samples, stepx);
        data_height = org_height;
        offs_x = chunk->first_valid_sample;
        break;

    case Tfr::Chunk::Order_column_major:
        org_width = chunk->nScales ();
        org_height = chunk->nSamples ();
        data_width = org_width;
        data_height = int_div_ceil (chunk->n_valid_samples, stepx);
        offs_y = chunk->first_valid_sample;

//        data_height = int_div_ceil (org_height, stepx);
//        offs_y = 0;
        break;

    default:
        EXCEPTION_ASSERT_EQUALS(chunk->order, Tfr::Chunk::Order_row_major);
    }

    bool resample = stepx > 1 || offs_y > 0 || offs_x > 0;
    if (resample)
        for (int y=0; y<data_height; ++y)
            for (int x=0; x<data_width; ++x)
              {
                float v = 0;
                int i = (y + offs_y)*org_width;
                for (int j = 0; j<stepx; ++j)
                    v = std::max(v, fp[std::min(org_width-1, i + offs_x + x*stepx + j)]);
                fp[y*data_width + x] = v;
              }


    // want linear interpolation which is not supported on gl_es with R32F but it is supported on iOS with R16F
    // besides, float16 if twice as fast to transfer and work with, and it has enough precision
    // note R32F is supported on iOS, but linear interpolation with R32F is not supported.
    if (type == Data_F16) {
        this->p = compress(fp, data_width, data_height, data_stride);
    } else {
        data_stride = data_width;
        this->p = fp;
    }

    this->chunk.reset (new JobChunk(data_width, data_height, chunk->getCoveredInterval ()));
    this->chunk->order = chunk->order;
    this->chunk->freqAxis = chunk->freqAxis;
    this->chunk->transform_data = CpuMemoryStorage::BorrowPtr<Tfr::ChunkElement>( DataStorageSize(data_stride, data_height), chunk->transform_data->getCpuMemory ());
    this->chunk->chunk_offset = (chunk->chunk_offset + chunk->first_valid_sample)/(double)stepx;
    this->chunk->first_valid_sample = 0;
    this->chunk->n_valid_samples = chunk->order==Tfr::Chunk::Order_row_major?data_width:data_height;
    this->chunk->sample_rate = chunk->sample_rate / stepx;
    this->chunk->original_sample_rate = chunk->original_sample_rate;
}


Signal::Interval TfrBlockUpdater::Job::
        getCoveredInterval() const
{
    return chunk->getCoveredInterval ();
}


class TfrBlockUpdaterPrivate: public OpenGL::BlockUpdater {};


TfrBlockUpdater::TfrBlockUpdater()
    : p(new TfrBlockUpdaterPrivate)
{
}


TfrBlockUpdater::~TfrBlockUpdater()
{
    delete p;
}


void TfrBlockUpdater::
        processJobs( std::queue<UpdateQueue::Job>& jobs )
{
    return p->processJobs(jobs);
}

} // namespace Update
} // namespace Heightmap
