#ifndef HEIGHTMAPBLOCKFILTER_H
#define HEIGHTMAPBLOCKFILTER_H

#include "tfr/cwtfilter.h"
#include "tfr/stftfilter.h"
#include "heightmap/collection.h"
#include <iostream>

namespace Heightmap
{


class BlockFilter
{
public:
    BlockFilter( Collection* collection );

    /// @overload Tfr::Filter::operator ()(Tfr::Chunk&)
    virtual void operator()( Tfr::Chunk& chunk );

protected:
    virtual void mergeChunk( pBlock block, Tfr::Chunk& chunk, Block::pData outData ) = 0;

    Collection* _collection;
};


template<typename FilterKind>
class BlockFilterImpl: public FilterKind, public BlockFilter
{
public:
    BlockFilterImpl( Collection* collection )
        :
        BlockFilter(collection)
    {
    }


    BlockFilterImpl( std::vector<boost::shared_ptr<Collection> > collections )
        :
        BlockFilter(collections[0].get()),
        _collections(collections)
    {
    }


    /// @overload Signal::Operation::fetch_invalid_samples()
    Signal::Intervals fetch_invalid_samples()
    {
        FilterKind::_invalid_samples.clear();

        //if (FilterKind::_invalid_samples)
        //    TaskInfo("%s %s had %s", vartype(*this).c_str(), __FUNCTION__, FilterKind::_invalid_samples.toString().c_str());

        foreach ( boost::shared_ptr<Collection> c, _collections)
        {
            Signal::Intervals inv_coll = c->invalid_samples();
            //TaskInfo("inv_coll = %s", inv_coll.toString().c_str());
            FilterKind::_invalid_samples |= inv_coll;
        }

        //TaskInfo ti("%s %s %s", vartype(*this).c_str(), __FUNCTION__, FilterKind::_invalid_samples.toString().c_str());

        Signal::Intervals inv_samples = FilterKind::_invalid_samples;
        Signal::Intervals r = Tfr::Filter::fetch_invalid_samples();
        FilterKind::_invalid_samples = inv_samples;
        return r;
    }


    virtual void operator()( Tfr::Chunk& chunk )
    {
        BlockFilter::operator()(chunk);
    }


    /// @overload Signal::Operation::affecting_source(const Signal::Interval&)
    Signal::Operation* affecting_source( const Signal::Interval& I)
    {
        if (FilterKind::_invalid_samples & I)
            return this;

        return FilterKind::_source->affecting_source( I );
    }


    /**
        To prevent anyone from optimizing away a read because it's known to
        result in zeros. BlockFilter wants to be run anyway, even with zeros.
        */
    Signal::Intervals zeroed_samples_recursive() { return Signal::Intervals(); }

    void applyFilter( Tfr::pChunk pchunk )
    {
        Signal::FinalSource * fs = dynamic_cast<Signal::FinalSource*>(FilterKind::root());
        BOOST_ASSERT( fs );

        _collection = _collections[fs->get_channel()].get();

        // A bit overkill to do every chunk, but it doesn't cost much.
        // Most of 'update_sample_size' is only needed the very first chunk,
        // and '_max_sample_size.time' is updated in 'invalidate_samples'.
        _collection->update_sample_size(pchunk.get());

        FilterKind::applyFilter( pchunk );
    }


    /// @overload Signal::Operation::affected_samples()
    virtual Signal::Intervals affected_samples()
    {
        return Signal::Intervals::Intervals();
    }

protected:
    std::vector<boost::shared_ptr<Collection> > _collections;
};


class CwtToBlock: public BlockFilterImpl<Tfr::CwtFilter>
{
public:
    CwtToBlock( Collection* collection );
    CwtToBlock( std::vector<boost::shared_ptr<Collection> > collections );

    /**
      Tells the "chunk-to-block" what information to extract from the complex
      time-frequency-representation. Such as phase, amplitude or weighted
      amplitude. The weighted ampltidue mode is default for the morlet
      transform to accommodate for low frequencies being smoothed out and
      appear low in amplitude even though they contain frequencies of high
      amplitude.
      */
    ComplexInfo complex_info;

    virtual void mergeChunk( pBlock block, Tfr::Chunk& chunk, Block::pData outData );
};


class StftToBlock: public BlockFilterImpl<Tfr::StftFilter>
{
public:
    StftToBlock( Collection* collection );
    StftToBlock( std::vector<boost::shared_ptr<Collection> > collections );

    virtual void mergeChunk( pBlock block, Tfr::Chunk& chunk, Block::pData outData );
};

} // namespace Heightmap
#endif // HEIGHTMAPBLOCKFILTER_H