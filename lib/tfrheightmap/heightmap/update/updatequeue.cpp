#include "updatequeue.h"

using namespace std;

namespace Heightmap {
namespace Update {

UpdateQueue::Job::
        operator bool() const
{
    return updatejob && !intersecting_blocks.empty ();
}


future<void> UpdateQueue::
        push (IUpdateJob::ptr updatejob, vector<pBlock> intersecting_blocks)
{
    Job j;
    j.updatejob = updatejob;
    j.intersecting_blocks = intersecting_blocks;
    future<void> f = j.promise.get_future ();
    q_.push(move(j));
    return f;
}


UpdateQueue::Job UpdateQueue::
        pop ()
{
    return q_.pop ();
}


UpdateQueue::queue UpdateQueue::
        clear ()
{
    return q_.clear ();
}


bool UpdateQueue::
        empty ()
{
    return q_.empty ();
}


void UpdateQueue::
        close ()
{
    return q_.close ();
}

} // namespace Update
} // namespace Heightmap
