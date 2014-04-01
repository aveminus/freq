#ifndef FANTRACKERMODEL_H
#define FANTRACKERMODEL_H

#include "rendermodel.h"
#include "support/fantrackerfilter.h"

namespace Tools {

class FanTrackerModel
{
public:
    FanTrackerModel(RenderModel*);
    static const Support::FanTrackerFilter* selected_filter(Signal::OperationDesc::Ptr::read_ptr& w);
    Signal::OperationDesc::Ptr filter;

private:
    RenderModel* render_model_;
};

} // namespace Tools

#endif // FANTRACKERMODEL_H
