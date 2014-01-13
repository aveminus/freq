#ifndef TFRFILTER_H
#define TFRFILTER_H

#include "signal/operation.h"

namespace Tfr {

class Transform;
typedef boost::shared_ptr<Transform> pTransform;
class TransformDesc;
typedef boost::shared_ptr<TransformDesc> pTransformDesc;

class Chunk;
typedef boost::shared_ptr<Chunk> pChunk;

/// @see ChunkAndInverse::inverse
struct ChunkAndInverse
{
    /**
     * The transform used to compute the chunk.
     */
    pTransform t;

    /**
      The Chunk as computed by readChunk(), or source()->readChunk()
      if transform() == source()->transform().
      */
    pChunk chunk;


    /**
      The variable 'inverse' _may_ be set by readChunk if
      this->source()->readFixed(chunk->getInterval()) is identical to
      this->transform()->inverse(chunk). In that case the inverse won't be
      computed again.
      */
    Signal::pMonoBuffer inverse;


    /**
     * The input buffer used to create 'chunk' with the transform 't'.
     */
    Signal::pMonoBuffer input;


    /**
     * Which channel the monobuffer comes from.
     */
    int channel;
};


/**
 * @brief The ChunkFilter class should implement a filter of a signal in the
 * frequency domain created by a Transform.
 */
class ChunkFilter
{
public:
    typedef boost::shared_ptr<ChunkFilter> Ptr;


    /**
     * @brief The ChunkFilterNoInverse class describes that the inverse shall never
     * be computed from the transformed data in 'ChunkFilter::operator ()'.
     *
     * Inherit from this class as well as from ChunkFilter.
     */
    class NoInverseTag
    {
    public:
        virtual ~NoInverseTag() {}
    };


    virtual ~ChunkFilter() {}

    /**
      Apply the filter to a computed Chunk. Return true if it makes sense
      to compute the inverse afterwards.
      */
    virtual void operator()( ChunkAndInverse& chunk ) = 0;

    /**
      Set the number of channels that will get this filter applied.
      May be ignored by the filter if it doesn't matter.
      */
    virtual void set_number_of_channels( unsigned ) {}
};
typedef ChunkFilter::Ptr pChunkFilter;


/**
 * @brief The ChunkFilterDesc class should be used by TransformOperationDesc to
 * create instances of ChunkFilter.
 */
class ChunkFilterDesc: public VolatilePtr<ChunkFilterDesc>
{
public:
    virtual ~ChunkFilterDesc() {}

    virtual pChunkFilter            createChunkFilter(Signal::ComputingEngine* engine=0) const = 0;
    virtual void                    transformDesc(pTransformDesc d) { transform_desc_ = d; }
    //virtual ChunkFilterDesc::Ptr    copy() const = 0;

    pTransformDesc                  transformDesc() const { return transform_desc_; }

protected:
    pTransformDesc transform_desc_;
};


/**
 * @brief The TransformOperationDesc class should wrap all generic functionality
 * in Signal::Operation and Tfr::Transform so that ChunkFilters can explicilty do
 * only the filtering.
 */
class TransformOperationDesc: public Signal::OperationDesc
{
public:
    TransformOperationDesc(pTransformDesc, ChunkFilterDesc::Ptr);
    ~TransformOperationDesc() {}

    // OperationDesc
    OperationDesc::Ptr copy() const;
    Signal::Operation::Ptr createOperation(Signal::ComputingEngine* engine=0) const;
    Signal::Interval requiredInterval(const Signal::Interval&, Signal::Interval*) const;
    Signal::Interval affectedInterval(const Signal::Interval&) const;
    QString toString() const;
    bool operator==(const Signal::OperationDesc&d) const;

    pTransformDesc transformDesc() const;
    virtual void transformDesc(pTransformDesc d);

protected:
    ChunkFilterDesc::Ptr chunk_filter_;

public:
    static void test();
};

} // namespace Tfr

#endif // TFRFILTER_H
