#ifndef TRANSFORMCHUNK_H
#define TRANSFORMCHUNK_H

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include "GpuCpuData.h"

typedef boost::shared_ptr<class Transform_chunk> pTransform_chunk;

class Transform_chunk
{
public:
    Transform_chunk();

    /**
      transform_data contains the wavelet transform rowwise.
      use as transform_data[ sample + f_index*nSamples ];
      See getNearestCoeff for an example on how to find f_index.
    */
    boost::scoped_ptr<GpuCpuData<float2> > transform_data;

    float min_hz, max_hz, sample_rate;
    unsigned sample_offset;
    bool modified;

    float timeInterval() const {       return nSamples()/sample_rate; }
    float startTime() const {          return sample_offset/sample_rate; }
    float endTime() const {            return startTime() + timeInterval(); }
    unsigned nSamples() const {        return transform_data->getNumberOfElements().width; }
    unsigned nFrequencies() const {    return transform_data->getNumberOfElements().height; }
    unsigned nChannels() const {       return transform_data->getNumberOfElements().depth; }

    bool valid() const {
        return 0 != transform_data->getSizeInBytes1D() &&
               0 != sample_rate &&
               min_hz < max_hz;
    }

    float2 getNearestCoeff( float t, float f );
    unsigned getFrequencyIndex( float f ) const;
    float getFrequency( unsigned fi ) const;
};

#endif // TRANSFORMCHUNK_H
