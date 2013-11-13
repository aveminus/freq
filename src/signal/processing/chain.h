#ifndef SIGNAL_PROCESSING_CHAIN_H
#define SIGNAL_PROCESSING_CHAIN_H

#include "volatileptr.h"
#include "targets.h"
#include "targetmarker.h"
#include "dag.h"
#include "workers.h"
#include "iinvalidator.h"

namespace Signal {
namespace Processing {

/**
 * @brief The Chain class should make the signal processing namespace easy to
 * use with a clear and simple interface.
 *
 * It should add signal processing operation steps to the Dag.
 * It should remove steps from the Dag.
 *
 * It should provide means to deprecate caches when the an added operation
 * changes (such as settings or contained data).
 */
class Chain: public VolatilePtr<Chain>
{
public:
    static Chain::Ptr createDefaultChain();

    ~Chain();

    /**
     * @brief addTarget
     * @param desc
     * @param at
     * @return A marker to keep track of the target. The Target is removed from the Dag when TargetMarker is deleted.
     */
    TargetMarker::Ptr addTarget(Signal::OperationDesc::Ptr desc, TargetMarker::Ptr at=TargetMarker::Ptr());

    /**
     * @brief addOperation
     * @param desc
     * @param at
     * @return
     *
     * Call IInvalidator::deprecateCache to update the chain with the samples that
     * were affected by this definition. A generic call would be
     * 'read1(invalidator)->deprecateCache(chain->extent(at));'
     */
    IInvalidator::Ptr addOperationAt(Signal::OperationDesc::Ptr desc, TargetMarker::Ptr at);
    void removeOperationsAt(TargetMarker::Ptr at);
    Signal::OperationDesc::Extent extent(TargetMarker::Ptr at) const;

    Workers::Ptr workers() const;
    Targets::Ptr targets() const;

    // Add jumping around with targets later.

private:
    Dag::Ptr dag_;
    Targets::Ptr targets_;
    Workers::Ptr workers_;
    Bedroom::Ptr bedroom_;

    Chain(Dag::Ptr, Targets::Ptr targets, Workers::Ptr workers, Bedroom::Ptr bedroom);

    Step::WeakPtr createBranchStep (Dag& dag, Signal::OperationDesc::Ptr desc, TargetMarker::Ptr at);
    Step::WeakPtr insertStep (Dag& dag, Signal::OperationDesc::Ptr desc, TargetMarker::Ptr at);

public:
    static void test();
};

} // namespace Processing
} // namespace Signal

#endif // SIGNAL_PROCESSING_CHAIN_H