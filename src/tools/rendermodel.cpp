#include "rendermodel.h"
#include "sawe/project.h"

#include "heightmap/collection.h"
#include "heightmap/renderer.h"

#include "tfr/filter.h"

#include <GlTexture.h>

namespace Tools
{



RenderModel::
        RenderModel(Sawe::Project* p)
        :
        renderSignalTarget(new Signal::Target(&p->layers, "Heightmap", true, true)),
        _qx(0), _qy(0), _qz(0),
        _px(0), _py(0), _pz(0),
        _rx(0), _ry(0), _rz(0),
        xscale(0),
        zscale(0),
        orthoview(1),
        _project(p),
        tfr_mapping_(Heightmap::BlockSize(1<<8,1<<8), 1)
{
    resetSettings();

    Signal::PostSink* o = renderSignalTarget->post_sink();

    collections.resize(o->num_channels());
    for (unsigned c=0; c<o->num_channels(); ++c)
        collections[c].reset( new Heightmap::Collection(renderSignalTarget->source()));

    renderer.reset( new Heightmap::Renderer( collections[0].get() ));

    for (unsigned c=0; c<o->num_channels(); ++c)
        collections[c]->renderer = renderer.get();

    tfr_mapping( Heightmap::TfrMapping(tfr_mapping_.block_size (), o->sample_rate()) );


//    setTestCamera();
}


RenderModel::
        ~RenderModel()
{
    TaskInfo ti(__FUNCTION__);
    renderer.reset();
    collections.clear();
    renderSignalTarget.reset();
}


void RenderModel::
        resetSettings()
{
    _qx = 0;
    _qy = 0;
    _qz = .5f;
    _px = 0;
    _py = 0;
    _pz = -10.f;
    _rx = 91;
    _ry = 180;
    _rz = 0;
    xscale = -_pz*0.1f;
    zscale = -_pz*0.75f;

#ifdef TARGET_hast
    _pz = -6;
    xscale = 0.1f;

    float L = _project->worker.length();
    if (L)
    {
        xscale = 14/L;
        _qx = 0.5*L;
    }
#endif
}


void RenderModel::
        setTestCamera()
{
    renderer->y_scale = 0.01f;
    _qx = 63.4565;
    _qy = 0;
    _qz = 0.37;
    _px = 0;
    _py = 0;
    _pz = -10;
    _rx = 46.2;
    _ry = 253.186;
    _rz = 0;

    orthoview.reset( _rx >= 90 );
}


Tfr::FreqAxis RenderModel::
        display_scale() const
{
    return tfr_mapping_.display_scale();
}


void RenderModel::
        display_scale(Tfr::FreqAxis x)
{
    tfr_mapping_.display_scale( x );

    for (unsigned c=0; c<collections.size(); ++c)
        collections[c]->tfr_mapping( tfr_mapping_ );
}


Heightmap::AmplitudeAxis RenderModel::
        amplitude_axis() const
{
    return tfr_mapping_.amplitude_axis();
}


void RenderModel::
        amplitude_axis(Heightmap::AmplitudeAxis x)
{
    tfr_mapping_.amplitude_axis( x );
    for (unsigned c=0; c<collections.size(); ++c)
        collections[c]->tfr_mapping( tfr_mapping_ );
}


Heightmap::TfrMapping RenderModel::
        tfr_mapping() const
{
    return tfr_mapping_;
}


void RenderModel::
        tfr_mapping(Heightmap::TfrMapping new_config)
{
    tfr_mapping_ = new_config;

    for (unsigned c=0; c<collections.size(); ++c)
        collections[c]->tfr_mapping( tfr_mapping_ );
}


Tfr::Filter* RenderModel::
        block_filter()
{
    std::vector<Signal::pOperation> s = renderSignalTarget->post_sink ()->sinks ();
    Tfr::Filter* f = dynamic_cast<Tfr::Filter*>(s[0]->source().get());

#ifdef _DEBUG
    Tfr::Filter* f2 = dynamic_cast<Tfr::Filter*>(collections[0]->block_filter().get());
    EXCEPTION_ASSERT( f == f2 );
#endif

    return f;
}


const Tfr::TransformDesc* RenderModel::
        transform()
{
    Tfr::Filter* filter = block_filter();
    if (filter)
        return filter->transform()->transformDesc();
    return 0;
}


float RenderModel::
        effective_ry()
{
    return fmod(fmod(_ry,360)+360, 360) * (1-orthoview) + (90*(int)((fmod(fmod(_ry,360)+360, 360)+45)/90))*orthoview;
}


} // namespace Tools
