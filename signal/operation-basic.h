#ifndef SIGNALOPERATIONBASIC_H
#define SIGNALOPERATIONBASIC_H

#include "signal/operation.h"

namespace Signal {

/**
  Example 1:
  start:  1234567
  OperationSetSilent( start, 1, 2 );
  result: 1004567
*/
class OperationSetSilent: public Operation {
public:
    OperationSetSilent( Signal::pOperation source, const Signal::Interval& section );

    virtual pBuffer read( const Interval& I );
    virtual std::string name();

    virtual Signal::Intervals zeroed_samples() { return affected_samples(); }
    virtual Signal::Intervals affected_samples() { return section_; }
private:
    Signal::Interval section_;


    friend class boost::serialization::access;
    OperationSetSilent():Operation(pOperation()),section_(0,0) {} // only used by deserialization

    template<class archive> void serialize(archive& ar, const unsigned int /*version*/)
    {
        using boost::serialization::make_nvp;

        ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Operation)
           & BOOST_SERIALIZATION_NVP(section_.first)
           & BOOST_SERIALIZATION_NVP(section_.last);
    }
};


class OperationRemoveSection: public Operation
{
public:
    OperationRemoveSection( pOperation source, Interval section );

    virtual pBuffer read( const Interval& I );
    virtual IntervalType number_of_samples();

    virtual Intervals affected_samples();
    virtual Intervals translate_interval(Intervals I);
    virtual Intervals translate_interval_inverse(Intervals I);

private:

    Interval section_;

    friend class boost::serialization::access;
    OperationRemoveSection():Operation(pOperation()),section_(0,0) {} // only used by deserialization

    template<class archive> void serialize(archive& ar, const unsigned int /*version*/)
    {
        using boost::serialization::make_nvp;

        ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Operation)
           & BOOST_SERIALIZATION_NVP(section_.first)
           & BOOST_SERIALIZATION_NVP(section_.last);
    }
};


/**
  Has no effect as long as source()->number_of_samples <= section.first.
  */
class OperationInsertSilence: public Operation
{
public:
    OperationInsertSilence( pOperation source, Interval section );

    virtual pBuffer read( const Interval& I );
    virtual IntervalType number_of_samples();

    virtual Intervals affected_samples();
    virtual Intervals translate_interval(Intervals I);
    virtual Intervals translate_interval_inverse(Intervals I);
private:
    Interval section_;
};

class OperationSuperposition: public Operation
{
public:
    OperationSuperposition( pOperation source, pOperation source2 );

    virtual pBuffer read( const Interval& I );
    virtual void set_channel(unsigned c);

    virtual Intervals zeroed_samples();
    virtual Intervals affected_samples();
    virtual Intervals zeroed_samples_recursive();

    virtual pOperation source2() const { return _source2; }

private:
    pOperation _source2;


    friend class boost::serialization::access;
    OperationSuperposition():Operation(pOperation()) {} // only used by deserialization

    template<class archive>
    void serialize(archive& ar, const unsigned int /*version*/)
    {
        using boost::serialization::make_nvp;

        ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Operation)
           & BOOST_SERIALIZATION_NVP(_source2);
    }
};

} // namespace Signal

#endif // SIGNALOPERATIONBASIC_H
