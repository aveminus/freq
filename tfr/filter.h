#ifndef TFRFILTER_H
#define TFRFILTER_H

#include "sawe/selection.h"
#include "signal/intervals.h"

#include <list>
#include <boost/shared_ptr.hpp>


namespace Tfr {
struct Chunk;

class Filter: Signal::
{
public:
    Filter();
    virtual ~Filter() {}

    virtual void operator()( Chunk& ) = 0;

    /**
      These samples are definitely set to 0 by the filter.
      */
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const = 0;

    /**
      These samples are definitely left as is by the filter.
      */
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const = 0;

    /**
      TODO Define how/when enabled should be used. Should all sources have an
      enabled property?
      */
    bool enabled;
};
typedef boost::shared_ptr<Filter> pFilter;


class FilterChain: public Filter, public std::list<pFilter>
{
public:
    virtual void operator()( Chunk& );
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;
};


class SelectionFilter: public Filter
{
public:
    SelectionFilter( Selection s );

    virtual void operator()( Chunk& );
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;

    Selection s;

private:
	// Why not copyable?
    SelectionFilter& operator=(const SelectionFilter& );
    SelectionFilter(const SelectionFilter& );
};


class EllipsFilter: public Filter
{
public:
    EllipsFilter(float t1, float f1, float t2, float f2, bool save_inside=false);

    virtual void operator()( Chunk& );
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;

    float _t1, _f1, _t2, _f2;
	bool _save_inside;
};


class SquareFilter: public Filter
{
public:
    SquareFilter(float t1, float f1, float t2, float f2, bool save_inside=false);

    virtual void operator()( Chunk& );
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;

    float _t1, _f1, _t2, _f2;
    bool _save_inside;
};


class MoveFilter: public Filter
{
public:
    MoveFilter(float df);

    virtual void operator()( Chunk& );
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;

    float _df;
};


class ReassignFilter: public Filter
{
public:
    ReassignFilter();

    virtual void operator()( Chunk& );

    // No zero samples, no untouched samples
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;
};


class TonalizeFilter: public Filter
{
public:
    TonalizeFilter();

    virtual void operator()( Chunk& );

    // No zero samples, no untouched samples
    virtual Signal::Intervals getZeroSamples( unsigned FS ) const;
    virtual Signal::Intervals getUntouchedSamples( unsigned FS ) const;
};

} // namespace Tfr

#endif // TFRFILTER_H
