#include "rectangle.h"
#include "rectanglekernel.h"
#include "tfr/chunk.h"
#include "tfr/cwtchunk.h"

// gpumisc
#include "computationkernel.h"
#include "TaskTimer.h"

// std
#include <iomanip>
#include <float.h> // FLT_MAX

using namespace Tfr;

//#define TIME_FILTER
#define TIME_FILTER if(0)

#ifdef min
#undef min
#undef max
#endif

namespace Filters {

RectangleKernel::RectangleKernel(float s1, float f1, float s2, float f2, bool save_inside) {
    _s1 = std::min(s1, s2);
    _f1 = std::min(f1, f2);
    _s2 = std::max(s1, s2);
    _f2 = std::max(f1, f2);
    _save_inside = save_inside;
}


void RectangleKernel::subchunk( Tfr::ChunkAndInverse& c ) {
    Chunk& chunk = *c.chunk;

    TIME_FILTER TaskTimer tt(boost::format("Rectangle %s") % chunk.getCoveredInterval ());

    Area area = {
            (float)(_s1 * chunk.sample_rate / chunk.original_sample_rate - chunk.chunk_offset.asFloat()),
            chunk.freqAxis.getFrequencyScalarNotClamped( _f1 ),
            (float)(_s2 * chunk.sample_rate / chunk.original_sample_rate - chunk.chunk_offset.asFloat()),
            chunk.freqAxis.getFrequencyScalarNotClamped( _f2 ) };

    ::removeRect( chunk.transform_data,
                  area,
                  _save_inside);

    TIME_FILTER ComputationSynchronize();
}


Rectangle::
        Rectangle(float t1, float f1, float t2, float f2, bool save_inside)
    :
      _s1(t1),
      _f1(f1),
      _s2(t2),
      _f2(f2),
      _save_inside(save_inside)
{
}


Tfr::pChunkFilter Rectangle::
        createChunkFilter(Signal::ComputingEngine* engine) const
{
    if (engine==0 || dynamic_cast<Signal::ComputingCpu*>(engine))
        return Tfr::pChunkFilter(new RectangleKernel(_s1, _f1, _s2, _f2, _save_inside));

    return Tfr::pChunkFilter();
}


Signal::OperationDesc::Extent Rectangle::
        extent() const
{
    Signal::OperationDesc::Extent x;
    if (_save_inside)
        x.interval = Signal::Interval(_s1, _s2);
    return x;
}


ChunkFilterDesc::Ptr Rectangle::
        copy() const
{
    return ChunkFilterDesc::Ptr(new Rectangle(_s1, _f1, _s2, _f2, _save_inside));
}


bool Rectangle::
        isInteriorSelected() const
{
    return _save_inside;
}


void Rectangle::
        selectInterior(bool v)
{
    _save_inside = v;
}


std::string Rectangle::
        name()
{
    std::stringstream ss;
    ss << std::setiosflags(std::ios::fixed);
    if (_s2 == FLT_MAX)
        ss << "Bandpass from ";
    else
        ss << "Rectangle from ";

    if (_s2 != FLT_MAX)
       ss << std::setprecision(1) << _s1 << " s, ";
    ss << std::setprecision(0) << _f1 << " Hz to ";
    if (_s2 != FLT_MAX)
       ss << std::setprecision(1) << _s2 << " s, ";
    ss << std::setprecision(0) << _f2 << " Hz";

    if (!this->_save_inside)
        ss << ", save outside";

    return ss.str();
}


Signal::Intervals Rectangle::
        zeroed_samples()
{
    return _save_inside ? outside_samples() : Signal::Intervals();
}


Signal::Intervals Rectangle::
        affected_samples()
{
    return (_save_inside ? Signal::Intervals() : outside_samples()).inverse();
}


Signal::Intervals Rectangle::
        outside_samples()
{
    long double
        start_time_d = std::max(0.f, _s1),
        end_time_d = std::max(0.f, _s2);

    Signal::IntervalType
        start_time = std::min((long double)Signal::Interval::IntervalType_MAX, start_time_d),
        end_time = std::min((long double)Signal::Interval::IntervalType_MAX, end_time_d);

    Signal::Intervals sid;
    if (start_time < end_time)
        sid = Signal::Intervals(start_time, end_time);

    return ~sid;
}


void Rectangle::
        test()
{
    // It should apply a bandpass and time filter between f1,t1 and f2,t2 to a signal.
    EXCEPTION_ASSERT(false);
}

} // namespace Filters
