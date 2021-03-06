#include "purge.h"

#include <boost/foreach.hpp>

namespace Signal {
namespace Processing {

Purge::
        Purge(Dag::ptr::weak_ptr dag)
    :
      dag(dag)
{
}


size_t recursive_purge(const Graph& g, const GraphVertex& v, Signal::Intervals out_of_date, bool aggressive, bool purge_vertex=false)
{
    size_t purged = 0;
    Step::ptr step = g[v];
    if (purge_vertex)
        purged += step->purge(out_of_date, aggressive);

    Signal::Intervals required_input;
    Signal::OperationDesc::ptr operation_desc = Step::operation_desc(step);

    if (operation_desc)
    {
        auto o = operation_desc.read ();
        while (out_of_date)
        {
            Signal::Interval expected_output;
            required_input |= o->requiredInterval (out_of_date.fetchFirstInterval (), &expected_output);
            out_of_date -= expected_output;

            if (!expected_output)
                break;
        }
    }
    else
    {
        required_input = out_of_date;
    }

    BOOST_FOREACH(GraphEdge e, in_edges(v, g))
        purged += recursive_purge(g, source(e,g), required_input, aggressive, true);

    return purged;
}


size_t Purge::
        purge(TargetNeeds::ptr needs, bool aggressive)
{
    Dag::ptr dag = this->dag.lock ();
    Step::ptr step = needs->step ().lock ();
    Signal::Intervals out_of_date = needs->out_of_date();

    if (!dag || !step || !out_of_date)
        return 0;

    auto rdag = dag.read ();
    return recursive_purge(rdag->g(), rdag->getVertex(step), out_of_date, aggressive);
}


size_t Purge::
        cache_size()
{
    Dag::ptr dag = this->dag.lock ();
    if (!dag)
        return 0;

    auto rdag = dag.read ();
    const Signal::Processing::Graph& g = rdag->g();

    size_t sz = 0;

    BOOST_FOREACH( GraphVertex u, vertices(g))
    {
        Step::ptr step = g[u];
        sz += Step::cache(step)->cache_size ();
    }

    return sz;
}

} // namespace Processing
} // namespace Signal
