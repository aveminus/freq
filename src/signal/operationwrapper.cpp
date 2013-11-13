#include "operationwrapper.h"

#include <boost/foreach.hpp>
#include <boost/weak_ptr.hpp>

namespace Signal {

/**
 * @brief The OperationWrapper class should wrap all calls to another Operation.
 *
 * It should be a transparent no-op operation if the other operation is null.
 */
class NoopOperationWrapper: public Operation {
public:
    virtual Signal::pBuffer process(Signal::pBuffer b) { return b; }
};


OperationDescWrapper::
        OperationDescWrapper(OperationDesc::Ptr wrap)
    :
      wrap_(wrap)
{
}


void OperationDescWrapper::
        setInvalidator(Processing::IInvalidator::Ptr invalidator)
{
    invalidator_ = invalidator;
}


void OperationDescWrapper::
        setWrappedOperationDesc(OperationDesc::Ptr wrap) volatile
{
    Processing::IInvalidator::Ptr invalidator;

    {
        WritePtr selfp(this);
        OperationDescWrapper* self = dynamic_cast<OperationDescWrapper*>(&*selfp);

        self->wrap_ = wrap;
        invalidator = self->invalidator_;
    }

    if (invalidator)
        read1(invalidator)->deprecateCache(Signal::Intervals::Intervals_ALL);
}


OperationDesc::Ptr OperationDescWrapper::
        getWrappedOperationDesc() const
{
    return wrap_;
}


Signal::Interval OperationDescWrapper::
        requiredInterval( const Signal::Interval& I, Signal::Interval* expectedOutput ) const
{
    if (wrap_)
        return read1(wrap_)->requiredInterval (I, expectedOutput);

    if (expectedOutput)
        *expectedOutput = I;
    return I;
}


Interval OperationDescWrapper::
        affectedInterval( const Interval& I ) const
{
    if (wrap_)
        return read1(wrap_)->affectedInterval (I);
    return I;
}


Intervals OperationDescWrapper::
        affectedInterval( const Intervals& I ) const
{
    if (wrap_)
        return read1(wrap_)->affectedInterval (I);
    return I;
}


OperationDesc::Ptr OperationDescWrapper::
        copy() const
{
    OperationDesc::Ptr od(new OperationDescWrapper(wrap_));
    return od;
}


Operation::Ptr OperationDescWrapper::
        createOperation(ComputingEngine* engine=0) const
{
    if (!wrap_)
        return Operation::Ptr(new NoopOperationWrapper);

    return read1(wrap_)->createOperation (engine);
}


OperationDesc::Extent OperationDescWrapper::
        extent() const
{
    if (wrap_)
        return read1(wrap_)->extent ();

    return Extent();
}


Operation::Ptr OperationDescWrapper::
        recreateOperation(Operation::Ptr, ComputingEngine*) const
{
    EXCEPTION_ASSERTX(false, "Not implemented");
    return Operation::Ptr();
}


QString OperationDescWrapper::
        toString() const
{
    if (wrap_)
        return read1(wrap_)->toString ();

    return vartype(*this).c_str ();
}


int OperationDescWrapper::
        getNumberOfSources() const
{
    EXCEPTION_ASSERTX(false, "Not implemented");
    return 0;
}


bool OperationDescWrapper::
        operator==(const OperationDesc& d) const
{
    const OperationDescWrapper* w = dynamic_cast<const OperationDescWrapper*>(&d);
    if (!w)
        return false;

    if (w == this)
        return true;

    if (!wrap_ && !w->wrap_)
        return true;

    if (wrap_ && w->wrap_)
        return *read1(wrap_) == *read1(w->wrap_);

    return false;
}


} // namespace Signal

#include "unused.h"
#include "timer.h"
#include "signal/computingengine.h"

#include <QSemaphore>
#include <QThread>

namespace Signal {

class EmptyOperationDesc : public OperationDesc
{
    Interval requiredInterval( const Interval& I, Interval* J) const {
        if (J) *J = I;
        return I;
    }

    Interval affectedInterval( const Interval& ) const {
        EXCEPTION_ASSERTX(false, "not implemented");
        return Interval();
    }

    OperationDesc::Ptr copy() const {
        EXCEPTION_ASSERTX(false, "not implemented");
        return OperationDesc::Ptr();
    }

    Operation::Ptr createOperation( ComputingEngine* ) const {
        return Operation::Ptr();
    }
};


class ExtentDesc : public EmptyOperationDesc
{
    Extent extent() const {
        Extent x;
        x.sample_rate = 17;
        return x;
    }
};

class WrappedOperationMock : public Signal::Operation {
    virtual Signal::pBuffer process(Signal::pBuffer b) {
        QSemaphore().tryAcquire (1, 2); // sleep 2 ms
        return b;
    }
};

class WrappedOperationDescMock : public EmptyOperationDesc
{
public:
    boost::shared_ptr<int> createOperationCount = boost::shared_ptr<int>(new int(0));

    Operation::Ptr createOperation( ComputingEngine* ) const {
        (*createOperationCount)++;
        return Operation::Ptr(new WrappedOperationMock);
    }
};

class WrappedOperationWorkerMock : public QThread
{
public:
    Operation::Ptr operation;
    Signal::pBuffer buffer;

    void run () {
        if (operation && buffer)
            buffer = write1(operation)->process(buffer);
    }
};

void OperationDescWrapper::
        test()
{
    // It should behave as another OperationDesc
    {
        OperationDescWrapper w(OperationDesc::Ptr(new ExtentDesc()));
        EXCEPTION_ASSERT_EQUALS( w.extent ().sample_rate.get_value_or (-1), 17);
    }

    // It should recreate instantiated operations when the wrapped operation is changed
    {
        OperationDesc::Ptr a(new WrappedOperationDescMock());
        OperationDesc::Ptr b(new WrappedOperationDescMock());
        OperationDescWrapper w(a);

        {
            EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 0 );
            EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(b))->createOperationCount, 0 );
            UNUSED(Signal::Operation::Ptr o1) = w.createOperation (0);
            EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 1 );
            EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(b))->createOperationCount, 0 );
            w.setWrappedOperationDesc (b);
            EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 1 );
            EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(b))->createOperationCount, 0 );
        }
        w.setWrappedOperationDesc (a);
        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 1 );
        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(b))->createOperationCount, 0 );
    }

    // It should behave as a transparent operation if no operation is wrapped
    {
        OperationDescWrapper w;
        OperationDesc::Ptr a(new WrappedOperationDescMock());

        Signal::Operation::Ptr o0 = w.createOperation (0);
        EXCEPTION_ASSERT(o0);
        EXCEPTION_ASSERT_EQUALS(vartype(*o0.get ()), "Signal::NoopOperationWrapper");

        w.setWrappedOperationDesc (a);
        Signal::Operation::Ptr o1 = w.createOperation (0);
        EXCEPTION_ASSERT(o1);
        EXCEPTION_ASSERT_EQUALS(vartype(*o1.get ()), "Signal::WrappedOperationMock");

        Signal::pBuffer b1(new Signal::Buffer(Signal::Interval(0,1),1,1));
        Signal::pBuffer b2(new Signal::Buffer(Signal::Interval(0,1),1,1));
        *b1->getChannel (0)->waveform_data ()->getCpuMemory () = 1234;
        *b2->getChannel (0)->waveform_data () = *b1->getChannel (0)->waveform_data ();
        Signal::pBuffer r1 = write1(o1)->process(b1);
        EXCEPTION_ASSERT_EQUALS(r1, b1);
        EXCEPTION_ASSERT(*r1 == *b2);
    }

    // It should allow new opertions without blocking while processing existing operations
    {
        OperationDescWrapper w;
        OperationDesc::Ptr a(new WrappedOperationDescMock());
        OperationDesc::Ptr b(new WrappedOperationDescMock());
        OperationDesc::Ptr c(new WrappedOperationDescMock());

        WrappedOperationWorkerMock wowm;
        wowm.operation = w.createOperation (0);
        wowm.buffer = Signal::pBuffer(new Signal::Buffer(Signal::Interval(0,1),1,1));

        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 0 );
        w.setWrappedOperationDesc (a);
        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 0 );
        wowm.operation = w.createOperation (0);
        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 1 );
        ComputingEngine::Ptr ce(new ComputingCpu);
        wowm.operation = w.createOperation (ce.get());
        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 2 );
        w.setWrappedOperationDesc (b);
        EXCEPTION_ASSERT_EQUALS( *dynamic_cast<WrappedOperationDescMock*>(&*write1(a))->createOperationCount, 2 );

        wowm.start ();
        QSemaphore().tryAcquire (1, 1); // sleep 1 ms to make sure the operation is locked for writing
        {
            Timer t;
            w.setWrappedOperationDesc (c); // this should not be blocking
            float T = t.elapsed ();
            EXCEPTION_ASSERT_LESS( T, 60e-6 );
        }
        {
            Timer t;
            EXCEPTION_ASSERT( wowm.wait (2) );
            float T = t.elapsed ();
            EXCEPTION_ASSERT_LESS( 0.5e-3, T ); // Should have waited at least 0.5 ms for processing to complete
        }
    }
}

} // namespace Signal