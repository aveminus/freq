#ifndef SIGNAL_DAG_PROCESSOR_H
#define SIGNAL_DAG_PROCESSOR_H

#include "daghead.h"

#include <QReadWriteLock>
#include <QThread>

namespace Signal {

class ComputingEngine;

namespace Dag {

// Hur ska state i en nod göras implicit trådsäkert utan att ställa krav på att varje enskild 'process' tar hand om sina mutexar?
// Genom att klona operationen. Noden skulle kunna ha flera olika "data".
// Flera olika operationer. Då garanteras a grafen är densamma eftersom den beskrivs av noden som inte är redundant.

class Processor {
public:
    typedef boost::shared_ptr<Processor> Ptr;

private:
    friend class ProcessingScheduler;
    Processor (QReadWriteLock* lock, Node::Ptr* head_node, ComputingEngine* computing_engine=0);    ~Processor();

    Signal::pBuffer read (Signal::Interval I);

    Signal::pBuffer read (const Node &node, Signal::Interval I);
    Signal::pBuffer readSkipCache (const Node &node, Signal::Interval I, Signal::Operation::Ptr operation);

    QReadWriteLock* lock_;
    const Node::Ptr* head_node_;
    ComputingEngine* computing_engine_;

    // A bunch of of nodes that this processor should remove itself from
    // when the processor is destroyed if the node hasn't already been
    // destroyed.
    typedef std::set<boost::weak_ptr<Node> > OperationInstances_;
    OperationInstances_ operation_instances_;
};


class Processors: boost::noncopyable {
public:

    // Make Singleton? Later optimization
    std::list<Processor::Ptr> processors;

//public enable_shared_from_this<Y>
};




} // namespace Dag
} // namespace Signal

#endif // SIGNAL_DAG_PROCESSOR_H
