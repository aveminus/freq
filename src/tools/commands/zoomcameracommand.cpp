#include "zoomcameracommand.h"

#include "tools/rendermodel.h"
#include "navigationcommand.h"
#include "heightmap/collection.h"
#include "sawe/project.h"
#include "tfr/transform.h"

namespace Tools {
namespace Commands {

ZoomCameraCommand::
        ZoomCameraCommand(Tools::RenderModel* model, float dt, float ds, float dz)
            :
            NavigationCommand(model),
            dt(dt),
            ds(ds),
            dz(dz)
{
}


std::string ZoomCameraCommand::
        toString()
{
    return "Zoom camera";
}


void ZoomCameraCommand::
        executeFirst()
{
    Tools::Support::RenderCamera c = *model->camera.read ();

    if (dt) if (!zoom( c, dt, ScaleX ))
    {
        float L = model->tfr_mapping().read ()->length();
        float d = std::min( 0.5 * fabsf(dt), fabs(c.q[0] - L/2));
        c.q[0] += c.q[0]>L*.5f ? -d : d;
    }

    if (ds) if (!zoom( c, ds, ScaleZ ))
    {
        float d = std::min( 0.5 * fabsf(ds), fabs(c.q[2] - .5f));
        c.q[2] += c.q[2]>.5f ? -d : d;
    }

    if (dz) zoom( c, dz, Zoom );

    *model->camera.write () = c;
}


bool ZoomCameraCommand::
        zoom(Tools::Support::RenderCamera& c, float delta, ZoomMode mode)
{
    float L = model->tfr_mapping ().read ()->length();
    float fs = model->tfr_mapping ().read ()->targetSampleRate();
    float min_xscale = 4.f/std::max(L,10/fs);
    float max_xscale = 0.05f*fs;


    // Could use VisualizationParams::detail_info instead
    const Tfr::FreqAxis& tfa = model->transform_desc ()->freqAxis(fs);
    unsigned maxi = tfa.getFrequencyScalar(fs/2);

    float hza = tfa.getFrequency(0u);
    float hza2 = tfa.getFrequency(1u);
    float hzb = tfa.getFrequency(maxi - 1);
    float hzb2 = tfa.getFrequency(maxi - 2);

    const Heightmap::FreqAxis& ds = model->display_scale();
    float scalara = ds.getFrequencyScalar( hza );
    float scalara2 = ds.getFrequencyScalar( hza2 );
    float scalarb = ds.getFrequencyScalar( hzb );
    float scalarb2 = ds.getFrequencyScalar( hzb2 );

    float minydist = std::min(fabsf(scalara2 - scalara), fabsf(scalarb2 - scalarb));

    float min_yscale = 4.f;
    float max_yscale = 1.f/minydist;

    if (delta > 0)
    {
        switch(mode)
        {
        case ScaleX: if (c.xscale == min_xscale)
                return false;
            break;
        case ScaleZ: if (c.zscale == min_yscale)
                return false;
            break;
        default:
            break;
        }
    }

    switch(mode)
    {
    case Zoom: doZoom( delta, 0, 0, 0, c.p[2] ); break;
    case ScaleX: doZoom( delta, &c.xscale, &min_xscale, &max_xscale, c.p[2]); break;
    case ScaleZ: doZoom( delta, &c.zscale, &min_yscale, &max_yscale, c.p[2]); break;
    }

    return true;
}


void ZoomCameraCommand::
        doZoom(float d, float* scale, float* min_scale, float* max_scale, double& p2)
{
    //TaskTimer("NavigationController wheelEvent %s %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
    if (d>0.1)
        d=0.1;
    if (d<-0.1)
        d=-0.1;

    if(scale)
    {
        *scale *= (1-d);

        if (d > 0 )
        {
            if (min_scale && *scale<*min_scale)
                *scale = *min_scale;
        }
        else
        {
            if (max_scale && *scale>*max_scale)
                *scale = *max_scale;
        }
    }
    else
    {
        p2 *= (1+d);

        if (p2<-100) p2 = -100;
        if (p2>-.1) p2 = -.1;
    }
}


} // namespace Commands
} // namespace Tools
