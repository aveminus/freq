#include <resamplecpu.h>

#include <complex>

template<>
float ConverterAmplitude::
operator()( std::complex<float> w, DataPos const& /*dataPosition*/ )
{
    // slightly faster than sqrtf(f) unless '--use_fast_math' is specified
    // to nvcc
    // return f*rsqrtf(f);
    return abs(w);
}


#include "blockkerneldef.h"

// that's it, blockkerneldef contains the definitions
