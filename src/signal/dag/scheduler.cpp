#include "scheduler.h"

namespace Signal {
namespace Dag {


void Scheduler::
        run()
{
    for (;;)
    {
        QReadLocker l(&lock_);
     //   doOneTask();
    }

}

/*
void doOneTask()
{
    // Prioritera med round robin.
    for (std::set<DagHead::Ptr>::iterator itr = dag_heads_.begin ();
         itr != dag_heads_.end ();
         ++itr)
    {

    }
    read (const Node &node, Signal::Interval I)
}


Signal::pBuffer Processor::
        read (const Node &node, Signal::Interval I)
{
    Signal::SinkSource& cache = node.data ().cache ();
    Signal::Intervals missing = I - cache.samplesDesc ();

    Signal::Operation::Ptr operation = node.data ().operation (this, computing_engine_);

    while (!missing.empty ())
    {
        Signal::pBuffer r = readSkipCache (node, missing.fetchFirstInterval (), operation);
        if (cache.num_channels () != r->number_of_channels ())
            cache = Signal::SinkSource(r->number_of_channels ());
        cache.put (r);
        missing -= r->getInterval ();

        if (r->getInterval () == I)
            return r;
    }

    return cache.readFixedLength (I);
}


Signal::pBuffer Processor::
        readSkipCache (const Node &node, Signal::Interval I, Signal::Operation::Ptr operation)
{
    const Signal::Interval rI = operation->requiredInterval( I );

    // EXCEPTION_ASSERT(I.count () && node.data().output_buffer->number_of_samples ());

    int N = node.numChildren ();
    Signal::pBuffer b;
    switch(N)
    {
    case 0:
        {
            const OperationSourceDesc* osd = dynamic_cast<const OperationSourceDesc*>(&node.operationDesc ());
            EXCEPTION_ASSERTX( osd, boost::format(
                                   "The first node in the dag was not an instance of "
                                   "OperationSourceDesc but: %1 (%2)")
                               % node.operationDesc ()
                               % vartype(node.operationDesc ()));

            b = Signal::pBuffer( new Signal::Buffer(rI, osd->getSampleRate (), osd->getNumberOfChannels ()));
            b = operation->process (b);
            break;
        }
    case 1:
        b = read (node.getChild (), rI);
        b = operation->process (b);
        break;

    default:
        {
            for (int i=0; i<N; ++i)
            {
                Signal::pBuffer r = read (node.getChild (i), rI);
                if (!b)
                    b = r;
                else
                {
                    Signal::pBuffer n(new Signal::Buffer(b->getInterval (), b->sample_rate (), b->number_of_channels ()+r->number_of_channels ()));
                    unsigned j=0;
                    for (;j<b->number_of_channels ();++j)
                        *n->getChannel (j) |= *b->getChannel (j);
                    for (;j<n->number_of_channels ();++j)
                        *n->getChannel (j) |= *r->getChannel (j-b->number_of_channels ());
                    b = n;
                }
            }

            b = operation->process (b);
            break;
        }
    }

    EXCEPTION_ASSERT_EQUALS( b->getInterval (), I );

    return b;
}
*/

} // namespace Dag
} // namespace Signal
