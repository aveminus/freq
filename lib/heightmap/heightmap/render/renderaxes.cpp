#include "renderaxes.h"
#include "frustumclip.h"
#include "shaderresource.h"

// gpumisc
#include "tasktimer.h"
#include "glPushContext.h"
#include "glstate.h"
#include "GLvector.h"
#include "gluperspective.h"
#include "log.h"
#include "GlException.h"

//#define TIME_RENDERER
#define TIME_RENDERER if(0)

namespace Heightmap {
namespace Render {

RenderAxes::
        RenderAxes()
    :
      glyphs_(0)
{
    GlException_SAFE_CALL( glyphs_ = GlyphFactory::makeIGlyphs () );
}


RenderAxes::
        ~RenderAxes()
{
    delete glyphs_;

    if (!QOpenGLContext::currentContext ()) {
        Log ("%s: destruction without gl context leaks vbos %d and %d") % __FILE__ % orthobuffer_ % vertexbuffer_;
        return;
    }

    if (orthobuffer_)
        glDeleteBuffers (1, &orthobuffer_);
    if (vertexbuffer_)
        glDeleteBuffers (1, &vertexbuffer_);
}


void RenderAxes::
        drawAxes( const RenderSettings* render_settings,
                  const glProjection* gl_projection,
                  FreqAxis display_scale, float T )
{
    this->render_settings = render_settings;
    this->gl_projection = gl_projection;
    this->display_scale = display_scale;

    ae_.glyphs.clear ();
    ae_.vertices.clear ();
    ae_.orthovertices.clear ();

    getElements(ae_, T);
    GlException_SAFE_CALL( drawElements(ae_) );
}


tvector<4,GLfloat> make4(const tvector<2,GLfloat>& v) {
    return tvector<4,GLfloat>(v[0], v[1], 0, 1);
}

tvector<4,GLfloat> make4(const vectord& v) {
    return tvector<4,GLfloat>(v[0], v[1], v[2], 1);
}

template<int N,typename T> void addVertices(
        std::vector<RenderAxes::Vertex>& vertices,
        const tvector<4,GLfloat>& color,
        const tvector<N,T>* V, int L)
{
    vertices.push_back (RenderAxes::Vertex{make4(V[0]),color}); // degenerate
    for (int i=0; i<L; i++)
        vertices.push_back (RenderAxes::Vertex{make4(V[i]),color});
    vertices.push_back (RenderAxes::Vertex{make4(V[L-1]),color}); // degenerate
}

void RenderAxes::
        getElements( RenderAxes::AxesElements& ae, float T )
{
    TIME_RENDERER TaskTimer tt("drawAxes(length = %g)", T);
    // Draw overlay borders, on top, below, to the right or to the left
    // default left bottom

    // 1 gray draw overlay
    // 2 clip entire sound to frustum
    // 3 decide upon scale
    // 4 draw axis
    const glProjecter gi(*gl_projection);
    const glProjecter* g = &gi;

    unsigned screen_width = g->viewport[2];
    unsigned screen_height = g->viewport[3];
    auto& render_settings = *this->render_settings;

    float borderw = 12.5*1.2;
    float borderh = 12.5*1.3;

    float scale = render_settings.dpifactor;
    borderw *= scale;
    borderh *= scale;

    float w = borderw/screen_width, h=borderh/screen_height;
    Render::FrustumClip frustum_clip(*g, w, h);

    if (render_settings.axes_border) { // 1 gray draw overlay
        typedef tvector<2,GLfloat> GLvector2F;
        GLvector2F v[] = {
            GLvector2F(0, 0),
            GLvector2F(w, h),
            GLvector2F(1, 0),
            GLvector2F(1-w, h),
            GLvector2F(1, 1),
            GLvector2F(1-w, 1-h),
            GLvector2F(0, 1),
            GLvector2F(w, 1-h),
            GLvector2F(0, 0),
            GLvector2F(w, h),
        };

        addVertices(ae.orthovertices, tvector<4,GLfloat>(1.0f, 1.0f, 1.0f, .4f), v, 10);
    }

    // 2 clip entire sound to frustum
    std::vector<vectord> clippedFrustum;

    vectord closest_i;
    {   //float T = collection->worker->source()->length();
        vectord corner[4]=
        {
            vectord( 0, 0, 0),
            vectord( 0, 0, 1),
            vectord( T, 0, 1),
            vectord( T, 0, 0),
        };

        clippedFrustum = frustum_clip.clipFrustum (corner, &closest_i);
    }


    // 3 find inside
    vectord inside;
    {
        for (unsigned i=0; i<clippedFrustum.size(); i++)
            inside = inside + clippedFrustum[i];

        // as clippedFrustum is a convex polygon, the mean position of its vertices will be inside
        inside = inside * (1./clippedFrustum.size());
    }


    // 3.1 compute units per pixel
    std::vector<vectord::T> timePerPixel;
    std::vector<vectord::T> scalePerPixel;
    vectord::T timePerPixel_closest, scalePerPixel_closest;
    vectord::T timePerPixel_inside, scalePerPixel_inside;
    {
        g->computeUnitsPerPixel( inside, timePerPixel_inside, scalePerPixel_inside );
        timePerPixel_inside *= scale; scalePerPixel_inside *= scale;

        g->computeUnitsPerPixel( closest_i, timePerPixel_closest, scalePerPixel_closest );
        timePerPixel_closest *= scale; scalePerPixel_closest *= scale;

        timePerPixel.resize (clippedFrustum.size());
        scalePerPixel.resize (clippedFrustum.size());

        for (unsigned i=0; i<clippedFrustum.size(); i++)
        {
            const vectord& p = clippedFrustum[i];
            vectord::T tpp, spp;
            g->computeUnitsPerPixel( p, tpp, spp );
            timePerPixel[i] = tpp * scale;
            scalePerPixel[i] = spp * scale;
        }
    }


    // 4 render and decide upon scale
    vectord x(1,0,0), z(0,0,1);

    FreqAxis fa = display_scale;
    // loop along all sides
    typedef tvector<4,GLfloat> GLvectorF;

    for (unsigned i=0; i<clippedFrustum.size(); i++)
    {
        unsigned j=(i+1)%clippedFrustum.size();
        vectord p1_0 = clippedFrustum[i]; // starting point of side
        vectord p2_0 = clippedFrustum[j]; // end point of side
        const vectord v0 = p2_0-p1_0;

        // decide if this side is a t or f axis
        bool taxis = std::abs(v0[0]*scalePerPixel_inside) > std::abs(v0[2]*timePerPixel_inside);

        // decide in which direction to traverse this edge
        vectord::T timePerPixel1 = timePerPixel[i],
                scalePerPixel1 = scalePerPixel[i],
                timePerPixel2 = timePerPixel[j],
                scalePerPixel2 = scalePerPixel[j];

        double dscale = 0.001;
        double hzDelta1= fabs(fa.getFrequencyT( p1_0[2] + v0[2]*dscale ) - fa.getFrequencyT( p1_0[2] ));
        double hzDelta2 = fabs(fa.getFrequencyT( p2_0[2] - v0[2]*dscale ) - fa.getFrequencyT( p2_0[2] ));

        if ((taxis && timePerPixel1 > timePerPixel2) || (!taxis && hzDelta1 > hzDelta2))
        {
            std::swap(p1_0, p2_0);
            std::swap(timePerPixel1, timePerPixel2);
            std::swap(scalePerPixel1, scalePerPixel2);
        }

        if (render_settings.draw_axis_at0==-1)
            (taxis?p1_0[2]:p1_0[0]) = ((taxis?p1_0[2]:p1_0[0])==0) ? 1 : 0;
        const vectord& p1 = p1_0;
        const vectord& p2 = p2_0;
        const vectord v = p2-p1;

        if (!v[0] && !v[2]) // skip if |v| = 0
            continue;

        // need initial f value
        vectord p = p1; // starting point
        vectord pp = p;
        double f = fa.getFrequencyT( p[2] );

        if (((taxis && render_settings.draw_t) || (!taxis && render_settings.draw_hz)) &&
            (render_settings.draw_axis_at0!=0?(taxis?p[2]==0:p[0]==0):true))
        for (double u=-1; true; )
        {
            // linear interpolation, false, but good enough. The true value would
            // be really slow.
            vectord::T timePerPixel = timePerPixel1 + (u<0?0:u)*(timePerPixel2-timePerPixel1),
                       scalePerPixel = scalePerPixel1 + (u<0?0:u)*(scalePerPixel2-scalePerPixel1);

            double ppp=0.4;
            timePerPixel = timePerPixel * ppp + timePerPixel_closest * (1.0-ppp);
            scalePerPixel = scalePerPixel * ppp + scalePerPixel_closest * (1.0-ppp);

            vectord::T ST = timePerPixel * 750; // ST = time units per 750 pixels, 750 pixels is a fairly common window size
            vectord::T SF = scalePerPixel * 750;
            double drawScaleT = std::min(ST, 50*timePerPixel_closest*750);
            double drawScaleF = std::min(SF, 50*scalePerPixel_closest*750);

            double time_axis_density = 18;
            if (20.+2.*log10(timePerPixel) < 18.)
                time_axis_density = std::max(1., 20.+2.*log10(timePerPixel));

            double scale_axis_density = std::max(10., 22. - ceil(fabs(log10(f))));
            if (f == 0)
                scale_axis_density = 21;

            int st = floor(log10( ST / time_axis_density ));
            //int sf = floor(log10( SF / scale_axis_density ));

            double DT = pow(10.0, st);
            //double DF = pow(10, sf);
            double DF = std::min(0.2, SF / scale_axis_density);

            // compute index of next marker along t and f
            int tmultiple = 10, tsubmultiple = 5;

            if (st>0)
            {
                st = 0;
                if( 60 < ST ) DT = 10, st++, tmultiple = 6, tsubmultiple = 3;
                if( 60*10 < ST ) DT *= 6, st++, tmultiple = 10, tsubmultiple = 5;
                if( 60*10*6 < ST ) DT *= 10, st++, tmultiple = 6, tsubmultiple = 3;
                if( 60*10*6*24 < ST ) DT *= 6, st++, tmultiple = 24, tsubmultiple = 6;
                if( 60*10*6*24*5 < ST ) DT *= 24, st++, tmultiple = 5, tsubmultiple = 5;
            }

            int tupdatedetail = 1;
            DT /= tupdatedetail;
            int t = floor(p[0]/DT + .5); // t marker index along t
            double t2 = t*DT;
            if (t2 < p[0] && v[0] > 0)
                t++;
            if (t2 > p[0] && v[0] < 0)
                t--;
            p[0] = t*DT;

            //int tmarkanyways = (bool)(fabsf(5*DT*tupdatedetail) > (ST / time_axis_density) && ((unsigned)(p[0]/DT + 0.5)%(tsubmultiple*tupdatedetail)==0) && ((unsigned)(p[0]/DT +.5)%(tmultiple*tupdatedetail)!=0));
            int tmarkanyways = (bool)(std::abs(5*DT*tupdatedetail) > (ST / time_axis_density) && ((unsigned)(p[0]/DT + 0.5)%(tsubmultiple*tupdatedetail)==0));
            if (tmarkanyways)
                st--;

            // compute index of next marker along t and f
            double epsilon = 1./10;
            double hz1 = fa.getFrequencyT( p[2] - DF * epsilon );
            double hz2 = fa.getFrequencyT( p[2] + DF * epsilon );
            if (hz2-f < f-hz1)  hz1 = f;
            else                hz2 = f;
            double fc0 = (hz2 - hz1)/epsilon;
            int sf = floor(log10( fc0 ));
            double fc = powf(10, sf);
            int fmultiple = 10;

            int fupdatedetail = 1;
            fc /= fupdatedetail;
            int mif = floor(f / fc + .5); // f marker index along f
            double nf = mif * fc;
            if (!(f - fc*0.05 < nf && nf < f + fc*0.05))
                nf += v[2] > 0 ? fc : -fc;
            f = nf;
            p[2] = fa.getFrequencyScalarNotClampedT(f);

            double np1 = fa.getFrequencyScalarNotClampedT( f + fc);
            double np2 = fa.getFrequencyScalarNotClampedT( f - fc);
            int fmarkanyways = false;
            fmarkanyways |= 0.9*std::abs(np1 - p[2]) > DF && 0.9*std::abs(np2 - p[2]) > DF && ((unsigned)(f / fc + .5)%1==0);
            fmarkanyways |= 4.5*std::abs(np1 - p[2]) > DF && 4.5*std::abs(np2 - p[2]) > DF && ((unsigned)(f / fc + .5)%5==0);
            if (fmarkanyways)
                sf--;


            if (taxis && render_settings.draw_cursor_marker)
            {
                float w = (render_settings.cursor[0] - pp[0])/(p[0] - pp[0]);

                if (0 < w && w <= 1)
                    if (!tmarkanyways)
                        st--;

                if (fabsf(w) < tupdatedetail*tmultiple/2)
                    tmarkanyways = -1;

                if (0 < w && w <= 1)
                {
                    DT /= 10;
                    t = floor(render_settings.cursor[0]/DT + 0.5); // t marker index along t

                    p = p1 + v*((render_settings.cursor[0] - p1[0])/v[0]);
                    p[0] = render_settings.cursor[0]; // exact float value so that "cursor[0] - pp[0]" == 0

                    if (!tmarkanyways)
                        st--;

                    tmarkanyways = 2;
                }
            }
            else if(render_settings.draw_cursor_marker && fa.axis_scale != AxisScale_Unknown)
            {
                float w = (render_settings.cursor[2] - pp[2])/(p[2] - pp[2]);

                if (0 < w && w <= 1)
                    if (!fmarkanyways)
                        sf--;

                if (fabsf(w) < fupdatedetail*fmultiple/2)
                    fmarkanyways = -1;

                if (0 < w && w <= 1)
                {
                    f = fa.getFrequencyT( render_settings.cursor[2] );
                    fc /= 10;
                    mif = floor(f / fc + .5); // f marker index along f
                    f = mif * fc;

                    p = p1 + v*((render_settings.cursor[2] - p1[2])/v[2]);
                    p[2] = render_settings.cursor[2]; // exact float value so that "cursor[2] - pp[2]" == 0

                    fmarkanyways = 2;
                }
            }


            // find next intersection along v
            double nu;
            int c1 = taxis ? 0 : 2;
            int c2 = !taxis ? 0 : 2;
            nu = (p[c1] - p1[c1])/v[c1];

            // if valid intersection
            if ( nu > u && nu<=1 ) { u = nu; }
            else break;

            // compute intersection
            p[c2] = p1[c2] + v[c2]*u;


            vectord np = p;
            nf = f;
            int nt = t;

            if (taxis)
            {
                if (v[0] > 0) nt++;
                if (v[0] < 0) nt--;
                np[0] = nt*DT;
            }
            else
            {
                if (v[2] > 0) nf+=fc;
                if (v[2] < 0) nf-=fc;
                nf = floor(nf/fc + .5)*fc;
                np[2] = fa.getFrequencyScalarNotClampedT(nf);
            }

            // draw marker
            if (taxis) {
                if (0 == t%tupdatedetail || tmarkanyways==2)
                {
                    float size = 1+ (0 == (t%(tupdatedetail*tmultiple)));
                    if (tmarkanyways)
                        size = 2;
                    if (-1 == tmarkanyways)
                        size = 1;

                    double sign = (v0^z)%(v0^( p - inside))>0 ? 1 : -1;
                    vectord o { 0, 0, size*SF*.008*sign };
                    vectord ow { ST*0.0005 * size, 0, 0 };

                    vectord v[] = {
                        p - ow,
                        p - ow - o,
                        p + ow,
                        p + ow - o
                    };
                    addVertices(ae.vertices, tvector<4,GLfloat>(0,0,0,0.8), v, 4 );

                    if (size>1) {
                        float angle = atan2(v0[2]/SF, v0[0]/ST) * (180*M_1_PI);

                        matrixd modelview { gl_projection->modelview };
                        modelview *= matrixd::translate (p[0], 0, p[2]);
                        modelview *= matrixd::rot (90,1,0,0);
                        modelview *= matrixd::scale (0.013*drawScaleT, 0.013*drawScaleF, 1);
                        modelview *= matrixd::rot (angle,0,0,1);

                        char a[100];
                        char b[100];
                        sprintf(b,"%%d:%%0%d.%df", 2+(st<-1?-st:0), st<0?-1-st:0);
                        int minutes = (int)(t*DT/60);
                        sprintf(a, b, minutes,t*DT-60*minutes);

                        if (!render_settings.left_handed_axes)
                            modelview *= matrixd::scale (-1,1,1);
                        if (sign<0)
                            modelview *= matrixd::rot (180,0,0,1);

                        ae.glyphs.push_back (GlyphData{std::move(modelview), a, 0.0, -0.05, 0.5, 0.5 - .7*(sign < 0 ? -1 : 1)});
                    }
                }
            } else if (fa.axis_scale != AxisScale_Unknown) {
                if (0 == ((unsigned)floor(f/fc + .5))%fupdatedetail || fmarkanyways==2)
                {
                    int size = 1;
                    if (0 == ((unsigned)floor(f/fc + .5))%(fupdatedetail*fmultiple))
                        size = 2;
                    if (fmarkanyways)
                        size = 2;
                    if (-1 == fmarkanyways)
                        size = 1;

                    double sign = (v0^x)%(v0^( p - inside))>0 ? 1 : -1;
                    vectord o { size*ST*.008*sign, 0, 0 };
                    vectord ow { 0, 0, SF*0.0005 * size };

                    vectord v[] = {
                        p - ow,
                        p - ow - o,
                        p + ow,
                        p + ow - o
                    };
                    addVertices(ae.vertices, tvector<4,GLfloat>(0,0,0,0.8), v, 4 );

                    if (size>1)
                    {
                        float angle = atan2(v0[2]/SF, v0[0]/ST) * (180*M_1_PI);

                        matrixd modelview { gl_projection->modelview };
                        modelview *= matrixd::translate (p[0],0,p[2]);
                        modelview *= matrixd::rot (90,1,0,0);
                        modelview *= matrixd::scale (0.013*drawScaleT, 0.013*drawScaleF, 1);
                        modelview *= matrixd::rot (angle,0,0,1);

                        char a[100];
                        char b[100];
                        sprintf(b,"%%.%df", sf<0?-1-sf:0);
                        sprintf(a, b, f);
                        //sprintf(a,"%g", f);

                        if (!render_settings.left_handed_axes)
                            modelview *= matrixd::scale (-1,1,1);
                        if (sign<0)
                            modelview *= matrixd::rot (180,0,0,1);

                        ae.glyphs.push_back (GlyphData{std::move(modelview), a, 0.0, -0.05, 0.5, 0.5 - .7*(sign < 0 ? -1 : 1)});
                    }
                }
            }

            pp = p;
            p = np; // q.e.d.
            f = nf;
            t = nt;
        }

        if (!taxis && render_settings.draw_piano && (render_settings.draw_axis_at0?p[0]==0:true))
        {
            vectord::T timePerPixel = 0.5*(timePerPixel1 + timePerPixel2),
                       scalePerPixel = 0.5*(scalePerPixel1 + scalePerPixel2);

            double ST = timePerPixel * 750;
            double SF = scalePerPixel * 750;

            // from http://en.wikipedia.org/wiki/Piano_key_frequencies
            // F(n) = 440 * pow(pow(2, 1/12),n-49)
            // log(F(n)/440) = log(pow(2, 1/12),n-49)
            // log(F(n)/440) = log(pow(2, 1/12))*log(n-49)
            // log(F(n)/440)/log(pow(2, 1/12)) = log(n-49)
            // n = exp(log(F(n)/440)/log(pow(2, 1/12))) + 49

            unsigned F1 = fa.getFrequency( (float)p1[2] );
            unsigned F2 = fa.getFrequency( (float)(p1+v)[2] );
            if (F2<F1) { unsigned swap = F2; F2=F1; F1=swap; }
            if (!(F1>fa.min_hz)) F1=fa.min_hz;
            if (!(F2<fa.max_hz())) F2=fa.max_hz();
            double tva12 = powf(2., 1./12);


            if (0 == F1)
                F1 = 1;
            int startTone = log(F1/440.)/log(tva12) + 45;
            int endTone = ceil(log(F2/440.)/log(tva12)) + 45;
            double sign = (v^x)%(v^( clippedFrustum[i] - inside))>0 ? 1 : -1;
            if (!render_settings.left_handed_axes)
                sign *= -1;

            for( int tone = startTone; tone<=endTone; tone++)
            {
                float ff = fa.getFrequencyScalar(440 * pow(tva12,tone-45));
                float ffN = fa.getFrequencyScalar(440 * pow(tva12,tone-44));
                float ffP = fa.getFrequencyScalar(440 * pow(tva12,tone-46));

                int toneTest = tone;
                while(toneTest<0) toneTest+=12;

                bool blackKey = false;
                switch(toneTest%12) { case 1: case 3: case 6: case 8: case 10: blackKey = true; }
                bool blackKeyP = false;
                switch((toneTest+11)%12) { case 1: case 3: case 6: case 8: case 10: blackKeyP = true; }
                bool blackKeyN = false;
                switch((toneTest+1)%12) { case 1: case 3: case 6: case 8: case 10: blackKeyN = true; }
                float wN = ffN-ff, wP = ff-ffP;
                if (blackKey)
                    wN *= .5, wP *= .5;
                else {
                    if (!blackKeyN)
                        wN *= .5;
                    if (!blackKeyP)
                        wP *= .5;
                }

                float u = (ff - p1[2])/v[2];
                float un = (ff+wN - p1[2])/v[2];
                float up = (ff-wP - p1[2])/v[2];
                vectord pt = p1+v*u;
                vectord pn = p1+v*un;
                vectord pp = p1+v*up;

                vectord dx { 0.016000*ST, 0, 0 };
                float blackw = 0.4f;

                if (sign>0)
                {
                    pp += dx;
                    pn += dx;
                    pt += dx;
                }

                tvector<4,GLfloat> keyColor(0,0,0, 0.7f * blackKey);
                if (render_settings.draw_cursor_marker)
                {
                    float w = (render_settings.cursor[2] - ff)/(ffN - ff);
                    w = fabsf(w/1.6f);
                    if (w < 1)
                    {
                        keyColor[1] = (1-w)*(1-w);
                        if (blackKey)
                            keyColor[3] = keyColor[3]*w + .9f*(1-w);
                        else
                            keyColor[3] = keyColor[1] * .7f;
                    }
                }

                if (keyColor[3] != 0)
                {
                    if (blackKey)
                    {
                        vectord v[] = {
                            pp - dx,
                            pp - dx*blackw,
                            pn - dx,
                            pn - dx*blackw,
                        };

                        addVertices(ae.vertices, keyColor, v, 4);
                    }
                    else
                    {
                        vectord v[] = {
                            vectord (pp - dx*(blackKeyP ? blackw : 1.)),
                            vectord (pp),
                            vectord (pp*0.5 + pt*0.5 - dx*(blackKeyP ? blackw : 1.)),
                            vectord (pp*0.5 + pt*0.5),
                            vectord (pp*0.5 + pt*0.5),
                            vectord (pp*0.5 + pt*0.5 - dx), // backside
                            vectord (pn*0.5 + pt*0.5),
                            vectord (pn*0.5 + pt*0.5 - dx),
                            vectord (pn*0.5 + pt*0.5 - dx),
                            vectord (pn*0.5 + pt*0.5),
                            vectord (pn*0.5 + pt*0.5),
                            vectord (pn*0.5 + pt*0.5 - dx*(blackKeyN ? blackw : 1.)),
                            vectord (pn),
                            vectord (pn - dx*(blackKeyN ? blackw : 1.))
                        };

                        addVertices(ae.vertices, keyColor, v, 14);
                    }
                }


                // outline
                vectord lx { 0.0005*ST, 0, 0 };
                vectord ly { 0, 0, 0.0005*SF };
                vectord v[] = {
                    pn - dx - lx,
                    pn - dx + lx,
                    pp - dx - lx,
                    pp - dx + lx
                };
                addVertices(ae.vertices, tvector<4,GLfloat>(0,0,0,0.8), v, 4 );

                vectord v2[] = {
                    pp - dx*(blackKeyP ? blackw : 1.) - ly,
                    pp - dx*(blackKeyP ? blackw : 1.) + ly,
                    pp - dx*(blackKey ? blackw : 0.) - ly - lx,
                    pp - dx*(blackKey ? blackw : 0.) + ly + lx,
                    pn - dx*(blackKey ? blackw : 0.) + ly + lx,
                    pn - dx*(blackKey ? blackw : 0.) - ly - lx,
                    pn - dx*(blackKeyN ? blackw : 1.) + ly,
                    pn - dx*(blackKeyN ? blackw : 1.) - ly
                };
                addVertices(ae.vertices, tvector<4,GLfloat>(0,0,0,0.8), v2, 8 );

                if (tone%12 == 0)
                {
                    matrixd modelview { gl_projection->modelview };
                    modelview *= matrixd::translate ( pp[0], 0, pp[2] );
                    modelview *= matrixd::rot (90,1,0,0);

                    //modelview *= matrixd::scale (0.00014*ST, 0.00014*SF, 1);
                    modelview *= matrixd::scale (0.5 * dx[0], 35. * dx[0]/ST*(pn[2]-pp[2]), 1.);

                    if (!render_settings.left_handed_axes)
                        modelview *= matrixd::scale (-1,1,1);

                    char a[100];
                    sprintf(a,"C%d", tone/12+1);
                    ae.glyphs.push_back (GlyphData{std::move(modelview), a, 0.1, -0.05, 1., 0.});
                }
            }
        }
    }
}


void RenderAxes::
        drawElements( const RenderAxes::AxesElements& ae)
{
    if (!program_)
    {
        program_ = ShaderResource::loadGLSLProgramSource (
                                          R"vertexshader(
                                              attribute highp vec4 qt_Vertex;
                                              attribute highp vec4 colors;
                                              uniform highp mat4 qt_ModelViewMatrix;
                                              uniform highp mat4 qt_ProjectionMatrix;
                                              varying highp vec4 color;

                                              void main() {
                                                  gl_Position = qt_ProjectionMatrix * (qt_ModelViewMatrix * qt_Vertex);
                                                  color = colors;
                                              }
                                          )vertexshader",
                                          R"fragmentshader(
                                              varying highp vec4 color;

                                              void main() {
                                                  gl_FragColor = color;
                                              }
                                          )fragmentshader");
        if (program_->isLinked())
        {
            uni_ProjectionMatrix = program_->uniformLocation("qt_ProjectionMatrix");
            uni_ModelViewMatrix = program_->uniformLocation("qt_ModelViewMatrix");
            attrib_Vertex = program_->attributeLocation("qt_Vertex");
            attrib_Color = program_->attributeLocation("colors");
        }

        orthoprogram_ = ShaderResource::loadGLSLProgramSource (
                                          R"vertexshader(
                                              attribute highp vec4 qt_Vertex;
                                              attribute highp vec4 colors;
                                              uniform highp mat4 qt_ProjectionMatrix;
                                              varying highp vec4 color;

                                              void main() {
                                                  gl_Position = qt_ProjectionMatrix * qt_Vertex;
                                                  color = colors;
                                              }
                                          )vertexshader",
                                          R"fragmentshader(
                                              varying highp vec4 color;

                                              void main() {
                                                  gl_FragColor = color;
                                              }
                                          )fragmentshader");
        if (orthoprogram_->isLinked())
        {
            uni_OrthoProjectionMatrix = orthoprogram_->uniformLocation("qt_ProjectionMatrix");
            attrib_OrthoVertex = orthoprogram_->attributeLocation("qt_Vertex");
            attrib_OrthoColor = orthoprogram_->attributeLocation("colors");

            matrixd ortho;
            glhOrtho(ortho.v (), 0, 1, 0, 1, -1, 1);

            GlState::glUseProgram (orthoprogram_->programId());
            orthoprogram_->setUniformValue(uni_OrthoProjectionMatrix,
                                      QMatrix4x4(GLmatrixf(ortho).transpose ().v ()));
        }
    }

    if (!program_->isLinked () || !orthoprogram_->isLinked ())
        return;

    GlState::glDisable (GL_DEPTH_TEST);
    glDepthMask(false);
    GlState::glEnable (GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GlState::glEnableVertexAttribArray (0);
    GlState::glEnableVertexAttribArray (1);

    if (!ae.orthovertices.empty () && orthoprogram_->isLinked ())
    {
        GlState::glUseProgram (orthoprogram_->programId());

        if (!orthobuffer_)
            GlException_SAFE_CALL( glGenBuffers(1, &orthobuffer_) );
        GlException_SAFE_CALL( GlState::glBindBuffer(GL_ARRAY_BUFFER, orthobuffer_) );
        if (orthobuffer_size_ < ae.orthovertices.size () || ae.orthovertices.size () < 4*orthobuffer_size_)
        {
            GlException_SAFE_CALL( glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex)*ae.orthovertices.size (), &ae.orthovertices[0], GL_STREAM_DRAW) );
            orthobuffer_size_ = ae.orthovertices.size ();
            orthoprogram_->setAttributeBuffer(attrib_OrthoVertex, GL_FLOAT, 0, 4, sizeof(Vertex));
            orthoprogram_->setAttributeBuffer(attrib_OrthoColor, GL_FLOAT, sizeof(tvector<4,GLfloat>), 4, sizeof(Vertex));
        }
        else
        {
            glBufferSubData (GL_ARRAY_BUFFER, 0, sizeof(Vertex)*ae.orthovertices.size (), &ae.orthovertices[0]);
        }

        GlState::glDrawArrays(GL_TRIANGLE_STRIP, 0, ae.orthovertices.size());
    }

    if (!ae.vertices.empty () && program_->isLinked ())
    {
        GlState::glUseProgram (program_->programId());

        if (!vertexbuffer_)
            GlException_SAFE_CALL( glGenBuffers(1, &vertexbuffer_) );
        GlException_SAFE_CALL( GlState::glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer_) );
        if (vertexbuffer_size_ < ae.vertices.size () || ae.vertices.size () < 4*vertexbuffer_size_)
        {
            GlException_SAFE_CALL( glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex)*ae.vertices.size (), &ae.vertices[0], GL_STREAM_DRAW) );
            vertexbuffer_size_ = ae.vertices.size ();
            program_->setAttributeBuffer(attrib_Vertex, GL_FLOAT, 0, 4, sizeof(Vertex));
            program_->setAttributeBuffer(attrib_Color, GL_FLOAT, sizeof(tvector<4,GLfloat>), 4, sizeof(Vertex));
        }
        else
        {
            glBufferSubData (GL_ARRAY_BUFFER, 0, sizeof(Vertex)*ae.vertices.size (), &ae.vertices[0]);
        }

        program_->setUniformValue(uni_ProjectionMatrix,
                                  QMatrix4x4(GLmatrixf(gl_projection->projection).transpose ().v ()));
        program_->setUniformValue(uni_ModelViewMatrix,
                                  QMatrix4x4(GLmatrixf(gl_projection->modelview).transpose ().v ()));

        GlState::glDrawArrays(GL_TRIANGLE_STRIP, 0, ae.vertices.size());
    }

    GlState::glUseProgram (0);

    GlState::glDisableVertexAttribArray (1);
    GlState::glDisableVertexAttribArray (0);

    GlException_SAFE_CALL( glyphs_->drawGlyphs (*gl_projection, ae.glyphs) );

    GlState::glDisable (GL_BLEND);
    GlState::glEnable (GL_DEPTH_TEST);
    glDepthMask(true);
}


} // namespace Render
} // namespace Heightmap
