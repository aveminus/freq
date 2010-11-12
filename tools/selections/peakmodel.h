#ifndef PEAKMODEL_H
#define PEAKMODEL_H

#include "support/splinefilter.h"
#include "signal/operation.h"
#include "heightmap/reference.h"
#include <boost/unordered_map.hpp>

namespace Tools { namespace Selections
{

enum PropagationState {
    PS_Increasing,
    PS_Decreasing,
    PS_Out
};

class PeakModel
{
public:
    PeakModel();

    /**
      Get the SplineFilter
      */
    Support::SplineFilter* peak_filter();

    Signal::pOperation filter;

    void findAddPeak( Heightmap::Reference ref, Heightmap::Position pos );

private:
    typedef boost::shared_ptr< GpuCpuData<bool> > PeakAreaP;
    typedef boost::unordered_map<Heightmap::Reference, PeakAreaP> PeakAreas;
    PeakAreas classifictions;


    void findBorder();
    bool anyBorderPixel( uint2&, unsigned w, unsigned h );
    uint2 nextBorderPixel( uint2, unsigned w, unsigned h );

    PeakAreaP getPeakArea(Heightmap::Reference);
    bool classifiedVal(unsigned x, unsigned y, unsigned w, unsigned h);
    void recursivelyClassify( Heightmap::Reference ref,
                              unsigned w, unsigned h,
                              unsigned x, unsigned y,
                              PropagationState prevState, float prevVal );
    void recursivelyClassify( Heightmap::Reference ref,
                              float *data, bool* classification,
                              unsigned w, unsigned h,
                              unsigned x, unsigned y,
                              PropagationState prevState, float prevVal );

    //PeakAreas gaussed_classifictions;
    //PeakAreaP getPeakAreaGauss(Heightmap::Reference);
    /*float& gaussedVal(unsigned x, unsigned y, unsigned w, unsigned h);
    void smearGauss();*/
};

}} // Tools::Selections

#endif // PEAKMODEL_H
