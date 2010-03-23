#ifdef _MSC_VER
#include <windows.h>
#endif
#ifndef __APPLE__
  #include "GL/glew.h"
#else
  #include "OpenGL/glu.h"
#endif
#include <stdio.h>
#include "spectrogram-renderer.h"
#include "spectrogram-vbo.h"
#include <list>
#include <GlException.h>
#include <CudaException.h>
#include <boost/array.hpp>
#include <tmatrix.h>
#include <float.h>
#include <msc_stdc.h>

using namespace std;

static bool g_invalidFrustum = true;

SpectrogramRenderer::SpectrogramRenderer( pSpectrogram spectrogram )
:   _spectrogram(spectrogram),
    _mesh_index_buffer(0),
    _mesh_width(0),
    _mesh_height(0),
    _initialized(false),
    _redundancy(2), // 1 means every pixel gets its own vertex, 10 means every 10th pixel gets its own vertex
    _fewest_pixles_per_unit( FLT_MAX ),
    _fewest_pixles_per_unit_ref(_spectrogram->findReference( Position(),  Position())),
    _drawn_blocks(0)
{
}

void SpectrogramRenderer::setSize( unsigned w, unsigned h)
{
    if (w == _mesh_width && h ==_mesh_height)
        return;

    createMeshIndexBuffer(w, h);
    createMeshPositionVBO(w, h);
}

// create index buffer for rendering quad mesh
void SpectrogramRenderer::createMeshIndexBuffer(unsigned w, unsigned h)
{
    // create index buffer
    if (_mesh_index_buffer)
        glDeleteBuffersARB(1, &_mesh_index_buffer);

    _mesh_width = w;
    _mesh_height = h;

    int size = ((w*2)+2)*(h-1)*sizeof(GLuint);
    glGenBuffersARB(1, &_mesh_index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mesh_index_buffer);
    glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER, size, 0, GL_STATIC_DRAW);

    // fill with indices for rendering mesh as triangle strips
    GLuint *indices = (GLuint *) glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
    if (!indices) {
        return;
    }

    for(unsigned y=0; y<h-1; y++) {
        for(unsigned x=0; x<w; x++) {
            *indices++ = y*w+x;
            *indices++ = (y+1)*w+x;
        }
        // start new strip with degenerate triangle
        *indices++ = (y+1)*w+(w-1);
        *indices++ = (y+1)*w;
    }

    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// create fixed vertex buffer to store mesh vertices
void SpectrogramRenderer::createMeshPositionVBO(unsigned w, unsigned h)
{
    _mesh_position.reset( new Vbo( w*h*4*sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, *_mesh_position);
    float *pos = (float *) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if (!pos) {
        return;
    }

    for(unsigned y=0; y<h; y++) {
        for(unsigned x=0; x<w; x++) {
            float u = x / (float) (w-1);
            float v = y / (float) (h-1);
            *pos++ = u;
            *pos++ = 0.0f;
            *pos++ = v;
            *pos++ = 1.0f;
        }
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


typedef tvector<3,GLdouble> GLvector;
typedef tvector<4,GLdouble> GLvector4;
typedef tmatrix<4,GLdouble> GLmatrix;

static GLvector4 to4(const GLvector& a) { return GLvector4(a[0], a[1], a[2], 1);}
static GLvector to3(const GLvector4& a) { return GLvector(a[0], a[1], a[2]);}

template<typename f>
GLvector gluProject(tvector<3,f> obj, const GLdouble* model, const GLdouble* proj, const GLint *view, bool *r=0) {
    GLvector win;
    bool s = (GLU_TRUE == gluProject(obj[0], obj[1], obj[2], model, proj, view, &win[0], &win[1], &win[2]));
    if(r) *r=s;
    return win;
}

template<typename f>
GLvector gluUnProject(tvector<3,f> win, const GLdouble* model, const GLdouble* proj, const GLint *view, bool *r=0) {
    GLvector obj;
    bool s = (GLU_TRUE == gluUnProject(win[0], win[1], win[2], model, proj, view, &obj[0], &obj[1], &obj[2]));
    if(r) *r=s;
    return obj;
}

template<typename f>
GLvector gluProject(tvector<3,f> obj, bool *r=0) {
    GLdouble model[16], proj[16];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    return gluProject(obj, model, proj, view, r);
}

template<typename f>
GLvector gluProject2(tvector<3,f> obj, bool *r=0) {
    GLint view[4];
    glGetIntegerv(GL_VIEWPORT, view);
    GLvector4 eye = applyProjectionMatrix(applyModelMatrix(to4(obj)));
    eye[0]/=eye[3];
    eye[1]/=eye[3];
    eye[2]/=eye[3];

    GLvector screen(view[0] + (eye[0]+1)*view[2]/2, view[1] + (eye[1]+1)*view[3]/2, eye[2]);
    float dummy=43*23;
    return screen;
}

template<typename f>
GLvector gluUnProject(tvector<3,f> win, bool *r=0) {
    GLdouble model[16], proj[16];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    return gluUnProject(win, model, proj, view, r);
}

static bool validWindowPos(GLvector win) {
    GLint view[4];
    glGetIntegerv(GL_VIEWPORT, view);

    return win[0]>view[0] && win[1]>view[1]
            && win[0]<view[0]+view[2]
            && win[1]<view[1]+view[3]
            && win[2]>=0.1 && win[2]<=100;
}

static GLvector4 applyModelMatrix(GLvector4 obj) {
    GLdouble m[16];
    glGetDoublev(GL_MODELVIEW_MATRIX, m);

    GLvector4 eye = GLmatrix(m) * obj;
    return eye;
}

static GLvector4 applyProjectionMatrix(GLvector4 eye) {
    GLdouble p[16];
    glGetDoublev(GL_PROJECTION_MATRIX, p);

    GLvector4 clip = GLmatrix(p) * eye;
    return clip;
}

/* distance along normal, a negative distance means obj is in front of plane */
static float distanceToPlane( GLvector obj, const GLvector& plane, const GLvector& normal ) {
    return (plane-obj)%normal;
}


class glPushMatrixContext
{
public:
    glPushMatrixContext() { glPushMatrix(); }
    ~glPushMatrixContext() { glPopMatrix(); }
};

void SpectrogramRenderer::init()
{
    // initialize necessary OpenGL extensions
    GlException_CHECK_ERROR_MSG("1");

    int cudaDevices;
    CudaException_SAFE_CALL( cudaGetDeviceCount( &cudaDevices) );
    if (0 == cudaDevices ) {
        fprintf(stderr, "ERROR: Couldn't find any \"cuda capable\" device.");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

#ifndef __APPLE__
    if (0 != glewInit() ) {
        fprintf(stderr, "ERROR: Couldn't initialize \"glew\".");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    if (! glewIsSupported("GL_VERSION_2_0" )) {
        fprintf(stderr, "ERROR: Support for necessary OpenGL extensions missing.");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    if (!glewIsSupported( "GL_VERSION_1_5 GL_ARB_vertex_buffer_object GL_ARB_pixel_buffer_object" )) {
            fprintf(stderr, "Error: failed to get minimal extensions\n");
            fprintf(stderr, "Sonic AWE requires:\n");
            fprintf(stderr, "  OpenGL version 1.5\n");
            fprintf(stderr, "  GL_ARB_vertex_buffer_object\n");
            fprintf(stderr, "  GL_ARB_pixel_buffer_object\n");
            fflush(stderr);
            exit(-1);
    }
#endif

    // load shader
    _shader_prog = loadGLSLProgram("spectrogram.vert", "spectrogram.frag");

    setSize( _spectrogram->samples_per_block(), _spectrogram->scales_per_block() );

    _initialized=true;

    GlException_CHECK_ERROR_MSG("2");
}

void SpectrogramRenderer::draw()
{
    TaskTimer tt("Rendering scaletime plot");
    if (!_initialized) init();

    g_invalidFrustum = true;

    glPushMatrixContext();

    Position mss = _spectrogram->max_sample_size();
    Spectrogram::Reference ref = _spectrogram->findReference(Position(0,0), mss);

    beginVboRendering();

    renderChildrenSpectrogramRef(ref);

    endVboRendering();

    tt.info("Drew %u block%s", _drawn_blocks, _drawn_blocks==1?"":"s");
    _drawn_blocks=0;

    if (_fewest_pixles_per_unit != FLT_MAX) {
        TaskTimer tt("Updating a part of the closest non finished scaletime block");
        _spectrogram->updateBlock( _spectrogram->getBlock(_fewest_pixles_per_unit_ref) );

        _fewest_pixles_per_unit = FLT_MAX;
    }


    GlException_CHECK_ERROR();
}

void SpectrogramRenderer::beginVboRendering()
{
    GlException_CHECK_ERROR();
    unsigned meshW = _spectrogram->samples_per_block();
    unsigned meshH = _spectrogram->scales_per_block();

    glUseProgram(_shader_prog);

    // Set default uniform variables parameters for the vertex shader
    GLuint uniHeightScale, uniChopiness, uniSize;

    uniHeightScale = glGetUniformLocation(_shader_prog, "heightScale");
    glUniform1f(uniHeightScale, 0.0125f);

    uniChopiness   = glGetUniformLocation(_shader_prog, "chopiness");
    glUniform1f(uniChopiness, 1.0f);

    uniSize        = glGetUniformLocation(_shader_prog, "size");
    glUniform2f(uniSize, meshW, meshH);

    // Set default uniform variables parameters for the pixel shader
    GLuint uniDeepColor, uniShallowColor, uniSkyColor, uniLightDir;

    uniDeepColor = glGetUniformLocation(_shader_prog, "deepColor");
    glUniform4f(uniDeepColor, 0.0f, 0.0f, 0.1f, 1.0f);

    uniShallowColor = glGetUniformLocation(_shader_prog, "shallowColor");
    glUniform4f(uniShallowColor, 0.1f, 0.4f, 0.3f, 1.0f);

    uniSkyColor = glGetUniformLocation(_shader_prog, "skyColor");
    glUniform4f(uniSkyColor, 0.5f, 0.5f, 0.5f, 1.0f);

    uniLightDir = glGetUniformLocation(_shader_prog, "lightDir");
    glUniform3f(uniLightDir, 0.0f, 1.0f, 0.0f);
    // end of uniform settings

    glBindBuffer(GL_ARRAY_BUFFER, *_mesh_position);
    glVertexPointer(4, GL_FLOAT, 0, 0);
    glEnableClientState(GL_VERTEX_ARRAY);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mesh_index_buffer);
}

void SpectrogramRenderer::endVboRendering() {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glDisableClientState(GL_VERTEX_ARRAY);
    glUseProgram(0);
}

bool SpectrogramRenderer::renderSpectrogramRef( Spectrogram::Reference ref, bool* finished_ref )
{
    if (!ref.containsSpectrogram())
        return false;

    TaskTimer("drawing").suppressTiming();

    Position a, b;
    ref.getArea( a, b );
    glPushMatrixContext mc;

    glTranslatef(a.time, 0, a.scale);
    glScalef(b.time-a.time, 1, b.scale-a.scale);

    Spectrogram::pBlock block = _spectrogram->getBlock( ref, finished_ref );
    if (0!=block.get()) {
        if (0 /* direct rendering */ )
            block->vbo->draw_directMode();
        else if (1 /* vbo */ )
            block->vbo->draw();

    } else {
        // getBlock would try to find something else if the requested block wasn't readily available.
        // If getBlock fails, we're most likely out of memory. Indicate this silently by not drawing the surface but only a wireframe

        glBegin(GL_LINE_LOOP );
            glVertex3f( 0, 0, 0 );
            glVertex3f( 0, 0, 1 );
            glVertex3f( 1, 0, 0 );
            glVertex3f( 1, 0, 1 );
        glEnd();
    }

    _drawn_blocks++;

    return true;
}

bool SpectrogramRenderer::renderChildrenSpectrogramRef( Spectrogram::Reference ref )
{
    Position a, b;
    ref.getArea( a, b );
    TaskTimer tt("[%g, %g]", a.time, b.time);

    if (!ref.containsSpectrogram())
        return false;

    float timePixels, scalePixels;
    if (!computePixelsPerUnit( ref, timePixels, scalePixels))
        return false;

    if(0) if (-10==ref.log2_samples_size[0] && -8==ref.log2_samples_size[1]) {
        fprintf(stdout, "Ref (%d,%d)\t%g\t%g\n", ref.block_index[0], ref.block_index[1], timePixels,scalePixels);
        fflush(stdout);
    }

    GLdouble needBetterF, needBetterT;

    if (0==scalePixels)
        needBetterF = 1.01;
    else
        needBetterF = scalePixels / (_redundancy*_spectrogram->scales_per_block());
    if (0==timePixels)
        needBetterT = 1.01;
    else
        needBetterT = timePixels / (_redundancy*_spectrogram->samples_per_block());

    if ( needBetterF > needBetterT && needBetterF > 1 && ref.top().containsSpectrogram() ) {
        renderChildrenSpectrogramRef( ref.top() );
        renderChildrenSpectrogramRef( ref.bottom() );
    }
    else if ( needBetterT > 1 && ref.left().containsSpectrogram() ) {
        renderChildrenSpectrogramRef( ref.left() );
        renderChildrenSpectrogramRef( ref.right() );
    }
    else {
        bool finished_ref=true;
        bool* finished_ptr = 0;
        float pixels_per_unit = timePixels*pow(2.,ref.log2_samples_size[0])
                              + scalePixels*pow(2.,ref.log2_samples_size[1]);

        if (pixels_per_unit < _fewest_pixles_per_unit)
        {
            finished_ptr = &finished_ref;
        }

        if (renderSpectrogramRef( ref, finished_ptr )) if (!finished_ref) {
            _fewest_pixles_per_unit = pixels_per_unit;
            _fewest_pixles_per_unit_ref = ref;
        }
    }

    return true;
}

void SpectrogramRenderer::renderParentSpectrogramRef( Spectrogram::Reference ref )
{
    // Assume that ref has already been drawn, draw sibblings, and call renderParent again
    renderChildrenSpectrogramRef( ref.sibbling1() );
    renderChildrenSpectrogramRef( ref.sibbling2() );
    renderChildrenSpectrogramRef( ref.sibbling3() );

    if (!ref.parent().toLarge() )
        renderParentSpectrogramRef( ref.parent() );
}

// the normal does not need to be normalized
static GLvector planeIntersection( GLvector pt1, GLvector pt2, float &s, const GLvector& plane, const GLvector& normal ) {
    GLvector dir = pt2-pt1;

    s = ((plane-pt1)%normal)/(dir % normal);
    GLvector p = pt1 + dir * s;

//    float v = (p-plane ) % normal;
//    fprintf(stdout,"p[2] = %g, v = %g\n", p[2], v);
//    fflush(stdout);
    return p;
}


static std::vector<GLvector> clipPlane( const std::vector<GLvector>& p, GLvector p0, GLvector n ) {
    std::vector<GLvector> r;

    for (unsigned i=0; i<p.size(); i++) {
        int nexti=(i+1)%p.size();
        if ((p0-p[i])%n < 0)
            r.push_back( p[i] );

        if (((p0-p[i])%n < 0) != ((p0-p[nexti])%n <0) ) {
            float s;
            GLvector xy = planeIntersection( p[i], p[nexti], s, p0, n );

            if (!isnan(s) && -.1 <= s && s <= 1.1)
                r.push_back( xy );
        }
    }

    return r;
}

static void printl(const char* str, const std::vector<GLvector>& l) {
    fprintf(stdout,"%s (%lu)\n",str,l.size());
    for (unsigned i=0; i<l.size(); i++) {
        fprintf(stdout,"  %g\t%g\t%g\n",l[i][0],l[i][1],l[i][2]);
    }
    fflush(stdout);
}

/* returns the first point on the border of the polygon 'l' that lies closest to 'target' */
static GLvector closestPointOnPoly( const std::vector<GLvector>& l, const GLvector &target)
{
    GLvector r;
    // check if point lies inside
    bool allNeg = true, allPos = true;

    // find point in poly closest to projectionPlane
    float min = FLT_MAX;
    for (unsigned i=0; i<l.size(); i++) {
        float f = (l[i]-target).dot();
        if (f<min) {
            min = f;
            r = l[i];
        }

        GLvector d = (l[(i+1)%l.size()] - l[i]),
                 v = target - l[i];

        if (d%v<0) allNeg=false;
        if (d%v>0) allPos=false;
        float k = d%v / (d.dot());
        if (0<k && k<1) {
            f = (l[i]+d*k-target).dot();
            if (f<min) {
                min = f;
                r = l[i]+d*k;
            }
        }
    }

    if (allNeg || allPos) {
        // point lies within convex polygon, create normal and project to surface
        if (l.size()>2) {
            GLvector n = (l[0]-l[1])^(l[0]-l[2]);
            r = target + n*distanceToPlane( target, l[0], n );
        }
    }
    return r;
}

static std::vector<GLvector> clipFrustum( std::vector<GLvector> l, GLvector &closest_i ) {

    static GLvector projectionPlane, projectionNormal,
        rightPlane, rightNormal,
        leftPlane, leftNormal,
        topPlane, topNormal,
        bottomPlane, bottomNormal;

    if (g_invalidFrustum) {
        GLint view[4];
        glGetIntegerv(GL_VIEWPORT, view);
        float z0 = .1, z1=.2;
        g_invalidFrustum = false;

        projectionPlane = gluUnProject( GLvector( view[0] + view[2]/2, view[1] + view[3]/2, z0) );
        projectionNormal = (gluUnProject( GLvector( view[0] + view[2]/2, view[1] + view[3]/2, z1) ) - projectionPlane).Normalize();

        rightPlane = gluUnProject( GLvector( view[0] + view[2], view[1] + view[3]/2, z0) );
        GLvector rightZ = gluUnProject( GLvector( view[0] + view[2], view[1] + view[3]/2, z1) );
        GLvector rightY = gluUnProject( GLvector( view[0] + view[2], view[1] + view[3]/2+1, z0) );
        rightZ=rightZ-rightPlane;
        rightY=rightY-rightPlane;
        rightNormal = ((rightY)^(rightZ)).Normalize();

        leftPlane = gluUnProject( GLvector( view[0], view[1] + view[3]/2, z0) );
        GLvector leftZ = gluUnProject( GLvector( view[0], view[1] + view[3]/2, z1) );
        GLvector leftY = gluUnProject( GLvector( view[0], view[1] + view[3]/2+1, z0) );
        leftNormal = ((leftZ-leftPlane)^(leftY-leftPlane)).Normalize();

        topPlane = gluUnProject( GLvector( view[0] + view[2]/2, view[1] + view[3], z0) );
        GLvector topZ = gluUnProject( GLvector( view[0] + view[2]/2, view[1] + view[3], z1) );
        GLvector topX = gluUnProject( GLvector( view[0] + view[2]/2+1, view[1] + view[3], z0) );
        topNormal = ((topZ-topPlane)^(topX-topPlane)).Normalize();

        bottomPlane = gluUnProject( GLvector( view[0] + view[2]/2, view[1], z0) );
        GLvector bottomZ = gluUnProject( GLvector( view[0] + view[2]/2, view[1], z1) );
        GLvector bottomX = gluUnProject( GLvector( view[0] + view[2]/2+1, view[1], z0) );
        bottomNormal = ((bottomX-bottomPlane)^(bottomZ-bottomPlane)).Normalize();
    }

    // must make all normals negative because one of axes are flipped (glScale with a minus sign on the x-axis)
    //printl("Start",l);
    l = clipPlane(l, projectionPlane, projectionNormal);
    //printl("Projectionclipped",l);
    l = clipPlane(l, rightPlane, -rightNormal);
    //printl("Right", l);
    l = clipPlane(l, leftPlane, -leftNormal);
    //printl("Left", l);
    l = clipPlane(l, topPlane, -topNormal);
    //printl("Top",l);
    l = clipPlane(l, bottomPlane, -bottomNormal);
    //printl("Bottom",l);
    //printl("Clipped polygon",l);

    closest_i = closestPointOnPoly(l, projectionPlane);
    return l;
}

static std::vector<GLvector> clipFrustum( GLvector corner[4], GLvector &closest_i ) {
    std::vector<GLvector> l;
    for (unsigned i=0; i<4; i++)
        l.push_back( corner[i] );
    return clipFrustum(l, closest_i);
}

/**
  @arg timePixels Estimated longest line of pixels along t-axis within ref measured in pixels
  @arg scalePixels Estimated longest line of pixels along t-axis within ref measured in pixels
  */
bool SpectrogramRenderer::computePixelsPerUnit( Spectrogram::Reference ref, float& timePixels, float& scalePixels )
{
    Position p[2];
    ref.getArea( p[0], p[1] );

    GLvector corner[4]=
    {
        GLvector( p[0].time, 0, p[0].scale),
        GLvector( p[0].time, 0, p[1].scale),
        GLvector( p[1].time, 0, p[1].scale),
        GLvector( p[1].time, 0, p[0].scale)
    };

    GLvector closest_i;
    std::vector<GLvector> clippedCorners = clipFrustum(corner, closest_i);
    if(0) if (-10==ref.log2_samples_size[0] && -8==ref.log2_samples_size[1])
    {
        printl("Clipped corners",clippedCorners);
        printf("closest_i %g\t%g\t%g\n", closest_i[0], closest_i[1], closest_i[2]);
    }
    if (0==clippedCorners.size())
        return false;

    GLint view[4];
    glGetIntegerv(GL_VIEWPORT, view);

    // Find units per pixel at point closest_i with glUnProject
    GLvector screen = gluProject( closest_i );
    GLvector screenX=screen, screenY=screen;
    if (screen[0]>view[0] + view[2]/2)       screenX[0]--;
    else                                     screenX[0]++;

    if (screen[1]>view[1] + view[3]/2)       screenY[1]--;
    else                                     screenY[1]++;

    GLvector
            wBase = gluUnProject( screen ),
            w1 = gluUnProject( screenX ),
            w2 = gluUnProject( screenY );

    screen[2]+=1;
    screenX[2]+=1;
    screenY[2]+=1;

    // directions
    GLvector
            dirBase = gluUnProject( screen )-wBase,
            dir1 = gluUnProject( screenX )-w1,
            dir2 = gluUnProject( screenY )-w2;

    // valid projection on xz-plane exists if dir?[1]<0 wBase[1]<0
    GLvector
            xzBase = wBase - dirBase*(wBase[1]/dirBase[1]),
            xz1 = w1 - dir1*(w1[1]/dir1[1]),
            xz2 = w2 - dir2*(w2[1]/dir2[1]);
    if(0) if (-10==ref.log2_samples_size[0] && -8==ref.log2_samples_size[1])
    {
    printf("xzBase %g\t%g\t%g\n", xzBase[0], xzBase[1], xzBase[2]);
    printf("xz1 %g\t%g\t%g\n", xz1[0], xz1[1], xz1[2]);
    printf("xz2 %g\t%g\t%g\n", xz2[0], xz2[1], xz2[2]);
    printf("xz1-xzBase %g\t%g\t%g\n", (xz1-xzBase)[0], (xz1-xzBase)[1], (xz1-xzBase)[2]);
    printf("xz2-xzBase %g\t%g\t%g\n", (xz2-xzBase)[0], (xz2-xzBase)[1], (xz2-xzBase)[2]);
    }

    // compute {units in xz-plane} per {screen pixel}, that determines the required resolution
    GLdouble
            timePerPixel = 0,
            freqPerPixel = 0;

    if (dir1[1] != 0 && dirBase[1] != 0) {
        timePerPixel = max(timePerPixel, fabs(xz1[0]-xzBase[0]));
        freqPerPixel = max(freqPerPixel, fabs(xz1[2]-xzBase[2]));
    }
    if (dir2[1] != 0 && dirBase[1] != 0) {
        timePerPixel = max(timePerPixel, fabs(xz2[0]-xzBase[0]));
        freqPerPixel = max(freqPerPixel, fabs(xz2[2]-xzBase[2]));
    }

    if (0 == timePerPixel)
        timePerPixel = max(fabs(w1[0]-wBase[0]), fabs(w2[0]-wBase[0]));
    if (0 == freqPerPixel)
        freqPerPixel = max(fabs(w1[2]-wBase[2]), fabs(w2[2]-wBase[2]));

    if (0==freqPerPixel) freqPerPixel=timePerPixel;
    if (0==timePerPixel) timePerPixel=freqPerPixel;

    timePixels = (p[1].time - p[0].time)/timePerPixel;
    scalePixels = (p[1].scale - p[0].scale)/freqPerPixel;

    return true;
}
