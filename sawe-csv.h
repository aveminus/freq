#ifndef SAWECSV_H
#define SAWECSV_H

#include "tfr-chunksink.h"

namespace Sawe {

/**
  Transforms a pBuffer into a pChunk with CwtSingleton and saves the chunk in a file called
  sonicawe-x.csv, where x is a number between 1 and 9, or 0 if all the other 9 files already
  exists. The file is saved with the csv-format comma separated values, but values are
  actually separated by spaces. One row of the csv-file corresponds to one row of the chunk.
*/
class Csv: public Tfr::ChunkSink
{
public:
    virtual void    put( Signal::pBuffer b ) { put(b, Signal::pSource()); }
    virtual void    put( Signal::pBuffer , Signal::pSource );
};

} // namespace Sawe

#endif // SAWECSV_H
