#include "tfrmapping.h"
#include "exceptionassert.h"
#include "collection.h"

namespace Heightmap {

TfrMapping::
        TfrMapping( BlockSize block_size, float fs )
    :
      block_layout( block_size, fs ),
      //length( 0 ),
      visualization_params_( new VisualizationParams )
{
    Tfr::FreqAxis f;
    f.setLinear( fs );
    visualization_params_->display_scale(f);
}


bool TfrMapping::
        operator==(const TfrMapping& b)
{
    return block_layout == b.block_layout &&
           //length == b.length &&
           *visualization_params_ == *b.visualization_params_;
}


bool TfrMapping::
        operator!=(const TfrMapping& b)
{
    return !(*this == b);
}


VisualizationParams::Ptr TfrMapping::
        visualization_params() const
{
    return visualization_params_;
}


BlockSize TfrMapping::
        block_size() const
{
    return block_layout.block_size ();
}


float TfrMapping::
        targetSampleRate() const
{
    return block_layout.targetSampleRate ();
}


Tfr::FreqAxis TfrMapping::
        display_scale() const
{
    return visualization_params ()->display_scale();
}


AmplitudeAxis TfrMapping::
        amplitude_axis() const
{
    return visualization_params ()->amplitude_axis();
}


Tfr::TransformDesc::Ptr TfrMapping::
        transform_desc() const
{
    return visualization_params ()->transform_desc();
}


TfrMap::
        TfrMap( TfrMapping tfr_mapping, int channels )
    :
      tfr_mapping_( tfr_mapping ),
      length_( 0 )
{
    this->channels (channels);
}


TfrMap::
        ~TfrMap()
{
    collections_.clear();
}


BlockSize TfrMap::
    block_size() const
{
    return tfr_mapping_.block_layout.block_size ();
}


void TfrMap::
        block_size(BlockSize bs)
{
    if (bs == tfr_mapping_.block_layout.block_size())
        return;

    tfr_mapping_.block_layout = BlockLayout(bs, targetSampleRate());

    updateCollections();
}


Tfr::FreqAxis TfrMap::
        display_scale() const
{
    return tfr_mapping_.visualization_params ()->display_scale();
}


AmplitudeAxis TfrMap::
        amplitude_axis() const
{
    return tfr_mapping_.visualization_params ()->amplitude_axis();
}


void TfrMap::
        display_scale(Tfr::FreqAxis v)
{
    if (v == tfr_mapping_.visualization_params ()->display_scale())
        return;

    tfr_mapping_.visualization_params ()->display_scale( v );

    updateCollections();
}


void TfrMap::
        amplitude_axis(AmplitudeAxis v)
{
    if (v == tfr_mapping_.visualization_params ()->amplitude_axis())
        return;

    tfr_mapping_.visualization_params ()->amplitude_axis( v );

    updateCollections();
}


float TfrMap::
        targetSampleRate() const
{
    return tfr_mapping_.block_layout.targetSampleRate ();
}


void TfrMap::
        targetSampleRate(float v)
{
    if (v == tfr_mapping_.block_layout.targetSampleRate ())
        return;

    tfr_mapping_.block_layout = BlockLayout(block_size (), v);

    updateCollections();
}


Tfr::TransformDesc::Ptr TfrMap::
        transform_desc() const
{
    return tfr_mapping_.visualization_params ()->transform_desc();
}


void TfrMap::
        transform_desc(Tfr::TransformDesc::Ptr t)
{
    VisualizationParams::Ptr vp = tfr_mapping_.visualization_params ();
    if (t == vp->transform_desc())
        return;

    if (t && vp->transform_desc() && (*t == *vp->transform_desc()))
        return;

    tfr_mapping_.visualization_params ()->transform_desc( t );

    updateCollections();
}


const TfrMapping& TfrMap::
        tfr_mapping() const
{
    return tfr_mapping_;
}


float TfrMap::
        length() const
{
    return length_;
}


void TfrMap::
        length(float L)
{
    if (L == length_)
        return;

    length_ = L;

    for (unsigned c=0; c<collections_.size(); ++c)
        write1(collections_[c])->length( length_ );
}


int TfrMap::
        channels() const
{
    return collections_.size ();
}


void TfrMap::
        channels(int v)
{
    // There are several assumptions that there is at least one channel on the form 'collections()[0]'
    if (v < 1)
        v = 1;

    if (v == channels())
        return;

    collections_.clear ();
    collections_.resize(v);

    for (int c=0; c<v; ++c)
    {
        collections_[c].reset( new Heightmap::Collection(tfr_mapping_));
    }
}


TfrMap::Collections TfrMap::
        collections() const
{
    return collections_;
}


void TfrMap::
        updateCollections()
{
    for (unsigned c=0; c<collections_.size(); ++c)
        write1(collections_[c])->tfr_mapping( tfr_mapping_ );
}


void TfrMap::
        test()
{
    TfrMap::Ptr t = testInstance();
    write1(t)->block_size( BlockSize(123,456) );
    EXCEPTION_ASSERT_EQUALS( BlockSize(123,456), read1(t)->tfr_mapping().block_layout.block_size() );
}


} // namespace Heightmap

#include "tfr/stftdesc.h"

namespace Heightmap
{

TfrMap::Ptr TfrMap::
        testInstance()
{
    TfrMapping tfrmapping(BlockSize(1<<8, 1<<8), 10);
    TfrMap::Ptr tfrmap(new TfrMap(tfrmapping, 1));
    write1(tfrmap)->transform_desc( Tfr::StftDesc ().copy ());
    return tfrmap;
}

} // namespace Heightmap
