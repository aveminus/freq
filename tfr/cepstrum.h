#ifndef CEPSTRUM_H
#define CEPSTRUM_H

#include "transform.h"
#include "HasSingleton.h"

namespace Tfr {

class Cepstrum : public Tfr::Transform, public HasSingleton<Cepstrum,Transform>
{
public:
    Cepstrum();

    virtual pChunk operator()( Signal::pBuffer b );

    virtual Signal::pBuffer inverse( pChunk chunk );
    virtual FreqAxis freqAxis( float FS );
    virtual float displayedTimeResolution( float FS, float hz );

    unsigned chunk_size();
};

} // namespace Tfr

#endif // CEPSTRUM_H
