#include "targetneeds.h"
#include "step.h"

#include <boost/date_time/posix_time/posix_time.hpp>

using namespace boost::posix_time;

namespace Signal {
namespace Processing {

TargetNeeds::
        TargetNeeds(boost::shared_ptr<volatile Step> step, Bedroom::Ptr bedroom)
    :
      step_(step),
      bedroom(bedroom)
{
    EXCEPTION_ASSERT(step);
    EXCEPTION_ASSERT(bedroom);
}


void TargetNeeds::
        updateNeeds(Signal::Intervals needed_samples, int prio, Signal::IntervalType center, Signal::Intervals invalidate)
{
    needed_samples_ = needed_samples;

    ptime now = microsec_clock::local_time();
    last_request_ = now + time_duration(0,0,prio);

    work_center_ = center;

    write1(step_)->deprecateCache(invalidate);

    bedroom->wakeup();
}


const boost::shared_ptr<volatile Step> TargetNeeds::
        step() const
{
    return step_;
}


boost::posix_time::ptime TargetNeeds::
        last_request() const
{
    return last_request_;
}


Signal::IntervalType TargetNeeds::
        work_center() const
{
    return work_center_;
}


Signal::Intervals TargetNeeds::
        not_started() const
{
    return needed_samples_ & read1(step_)->not_started();
}


Signal::Intervals TargetNeeds::
        out_of_date() const
{
    return needed_samples_ & read1(step_)->out_of_date();
}


} // namespace Processing
} // namespace Signal

#include "dag.h"
#include "targets.h"

namespace Signal {
namespace Processing {

void TargetNeeds::
        test()
{
    Bedroom::Ptr bedroom(new Bedroom);
    Step::Ptr step(new Step(Signal::OperationDesc::Ptr()));

    TargetNeeds::Ptr target_needs( new TargetNeeds(step, bedroom) );

    write1(step)->setInvalid(Signal::Interval(-10,10));
    EXCEPTION_ASSERT_EQUALS( read1(step)->out_of_date(), Signal::Interval(-10,10) );
    EXCEPTION_ASSERT_EQUALS( read1(target_needs)->out_of_date(), Signal::Interval() );
    write1(target_needs)->updateNeeds(Signal::Interval(-15,5), 0, 0);
    EXCEPTION_ASSERT_EQUALS( read1(step)->out_of_date(), Signal::Interval(-10,10) );
    EXCEPTION_ASSERT_EQUALS( read1(target_needs)->out_of_date(), Signal::Interval(-10,5) );
}

} // namespace Processing
} // namespace Signal
