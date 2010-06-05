#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#ifndef __APPLE__
 #include "GL/glew.h"
 #include <GL/glut.h>
#else
  #include "OpenGL/glu.h"
  #include <GLUT/glut.h>
#endif

#include "heightmap-renderer.h"
#include <float.h>
#include <GlException.h>
#include <CudaException.h>
#include "displaywidget.h"
#include <glPushContext.h>

#ifdef _MSC_VER
#include "msc_stdc.h"
#endif

namespace Heightmap {


using namespace std;

static bool g_invalidFrustum = true;

Renderer::Renderer( Collection* collection, DisplayWidget* _tempToRemove )
:   draw_piano(true),
    draw_hz(false),
    _collection(collection),
    _tempToRemove( _tempToRemove ),
    _mesh_index_buffer(0),
    _mesh_width(0),
    _mesh_height(0),
    _initialized(false),
    _redundancy(2), // 1 means every pixel gets its own vertex, 10 means every 10th pixel gets its own vertex
    _drawn_blocks(0)
{
}

void Renderer::setSize( unsigned w, unsigned h)
{
    if (w == _mesh_width && h ==_mesh_height)
        return;

    createMeshIndexBuffer(w, h);
    createMeshPositionVBO(w, h);
}

// create index buffer for rendering quad mesh
void Renderer::createMeshIndexBuffer(unsigned w, unsigned h)
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
void Renderer::createMeshPositionVBO(unsigned w, unsigned h)
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

typedef tvector<4,GLdouble> GLvector4;
typedef tmatrix<4,GLdouble> GLmatrix;

static GLvector4 to4(const GLvector& a) { return GLvector4(a[0], a[1], a[2], 1);}
// static GLvector to3(const GLvector4& a) { return GLvector(a[0], a[1], a[2]);}

template<typename f>
GLvector gluProject(tvector<3,f> obj, const GLdouble* model, const GLdouble* proj, const GLint *view, bool *r=0) {
    GLvector win;
    bool s = (GLU_TRUE == ::gluProject(obj[0], obj[1], obj[2], model, proj, view, &win[0], &win[1], &win[2]));
    if(r) *r=s;
    return win;
}

template<typename f>
GLvector gluUnProject(tvector<3,f> win, const GLdouble* model, const GLdouble* proj, const GLint *view, bool *r=0) {
    GLvector obj;
    bool s = (GLU_TRUE == ::gluUnProject(win[0], win[1], win[2], model, proj, view, &obj[0], &obj[1], &obj[2]));
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

/* static bool validWindowPos(GLvector win) {
    GLint view[4];
    glGetIntegerv(GL_VIEWPORT, view);

    return win[0]>view[0] && win[1]>view[1]
            && win[0]<view[0]+view[2]
            && win[1]<view[1]+view[3]
            && win[2]>=0.1 && win[2]<=100;
}*/

/*static GLvector4 applyModelMatrix(GLvector4 obj) {
    GLdouble m[16];
    glGetDoublev(GL_MODELVIEW_MATRIX, m);

    GLvector4 eye = GLmatrix(m) * obj;
    return eye;
}*/

/*static GLvector4 applyProjectionMatrix(GLvector4 eye) {
    GLdouble p[16];
    glGetDoublev(GL_PROJECTION_MATRIX, p);

    GLvector4 clip = GLmatrix(p) * eye;
    return clip;
}*/

/* distance along normal, a negative distance means obj is in front of plane */
static float distanceToPlane( GLvector obj, const GLvector& plane, const GLvector& normal ) {
    return (plane-obj)%normal;
}


void Renderer::init()
{
    TaskTimer tt("Initializing OpenGL");

    // initialize necessary OpenGL extensions
    GlException_CHECK_ERROR();

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
    _shader_prog = loadGLSLProgram(":/shaders/heightmap.vert", ":/shaders/heightmap.frag");

    setSize( _collection->samples_per_block(), _collection->scales_per_block() );

    _initialized=true;

    GlException_CHECK_ERROR();
}

void Renderer::draw( float scaley )
{
    GlException_CHECK_ERROR();
    TaskTimer tt(TaskTimer::LogVerbose, "Rendering scaletime plot");
    if (!_initialized) init();

    g_invalidFrustum = true;

    glPushMatrixContext mc;

/*    if (.01 > scaley)
        //setSize(2,2),
        scaley = 0.01;
    else
        setSize( _collection->samples_per_block(), _collection->scales_per_block() );
*/
    glScalef(0, scaley, 0);

    Position mss = _collection->max_sample_size();
    Reference ref = _collection->findReference(Position(0,0), mss);

    beginVboRendering();

    renderChildrenSpectrogramRef(ref);

    endVboRendering();

    tt.info("Drew %u block%s", _drawn_blocks, _drawn_blocks==1?"":"s");
    _drawn_blocks=0;

    GlException_CHECK_ERROR();
}

void Renderer::beginVboRendering()
{
    GlException_CHECK_ERROR();
    //unsigned meshW = _collection->samples_per_block();
    //unsigned meshH = _collection->scales_per_block();

    glUseProgram(_shader_prog);

    // Set default uniform variables parameters for the vertex shader
    //GLuint uniHeightScale, uniChopiness, uniSize;

/*    uniHeightScale = glGetUniformLocation(_shader_prog, "heightScale");
    glUniform1f(uniHeightScale, 0.0125f);

    uniSize        = glGetUniformLocation(_shader_prog, "size");
    glUniform2f(uniSize, meshW, meshH);
*/
    // Set default uniform variables parameters for the pixel shader
    //GLuint uniDeepColor, uniShallowColor, uniSkyColor, uniLightDir;

/*    uniDeepColor = glGetUniformLocation(_shader_prog, "deepColor");
    glUniform4f(uniDeepColor, 0.0f, 0.0f, 0.1f, 1.0f);

    uniShallowColor = glGetUniformLocation(_shader_prog, "shallowColor");
    glUniform4f(uniShallowColor, 0.1f, 0.4f, 0.3f, 1.0f);

    uniSkyColor = glGetUniformLocation(_shader_prog, "skyColor");
    glUniform4f(uniSkyColor, 0.5f, 0.5f, 0.5f, 1.0f);

    uniLightDir = glGetUniformLocation(_shader_prog, "lightDir");
    glUniform3f(uniLightDir, 0.0f, 1.0f, 0.0f);
    // end of uniform settings
*/
    glBindBuffer(GL_ARRAY_BUFFER, *_mesh_position);
    glVertexPointer(4, GL_FLOAT, 0, 0);
    glEnableClientState(GL_VERTEX_ARRAY);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mesh_index_buffer);

    GlException_CHECK_ERROR();
}

void Renderer::endVboRendering() {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glDisableClientState(GL_VERTEX_ARRAY);
    glUseProgram(0);
}

bool Renderer::renderSpectrogramRef( Reference ref )
{
    if (!ref.containsSpectrogram())
        return false;

    TaskTimer(TaskTimer::LogVerbose, "drawing").suppressTiming();

    Position a, b;
    ref.getArea( a, b );
    glPushMatrixContext mc;

    glTranslatef(a.time, 0, a.scale);
    glScalef(b.time-a.time, 1, b.scale-a.scale);

    pBlock block = _collection->getBlock( ref );
    if (0!=block.get()) {
        if (0 /* direct rendering */ )
            block->glblock->draw_directMode();
        else if (1 /* vbo */ )
            block->glblock->draw();

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

bool Renderer::renderChildrenSpectrogramRef( Reference ref )
{
    Position a, b;
    ref.getArea( a, b );
    TaskTimer tt(TaskTimer::LogVerbose, "[%g, %g]", a.time, b.time);

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
        needBetterF = scalePixels / (_redundancy*_collection->scales_per_block());
    if (0==timePixels)
        needBetterT = 1.01;
    else
        needBetterT = timePixels / (_redundancy*_collection->samples_per_block());

    if ( needBetterF > needBetterT && needBetterF > 1 && ref.top().containsSpectrogram() ) {
        renderChildrenSpectrogramRef( ref.top() );
        renderChildrenSpectrogramRef( ref.bottom() );
    }
    else if ( needBetterT > 1 && ref.left().containsSpectrogram() ) {
        renderChildrenSpectrogramRef( ref.left() );
        renderChildrenSpectrogramRef( ref.right() );
    }
    else {
        renderSpectrogramRef( ref );
    }

    return true;
}

void Renderer::renderParentSpectrogramRef( Reference ref )
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

static std::vector<GLvector> clipFrustum( std::vector<GLvector> l, GLvector &closest_i, float w=0, float h=0 ) {

    static GLvector projectionPlane, projectionNormal,
        rightPlane, rightNormal,
        leftPlane, leftNormal,
        topPlane, topNormal,
        bottomPlane, bottomNormal;

    if (!(0==w && 0==h))
        g_invalidFrustum = true;

    if (g_invalidFrustum) {
        GLint view[4];
        glGetIntegerv(GL_VIEWPORT, view);
        float z0 = .1, z1=.2;
        if (0==w && 0==h)
            g_invalidFrustum = false;

        projectionPlane = gluUnProject( GLvector( view[0] + view[2]/2, view[1] + view[3]/2, z0) );
        projectionNormal = (gluUnProject( GLvector( view[0] + view[2]/2, view[1] + view[3]/2, z1) ) - projectionPlane).Normalize();

        rightPlane = gluUnProject( GLvector( view[0] + (1-w)*view[2], view[1] + view[3]/2, z0) );
        GLvector rightZ = gluUnProject( GLvector( view[0] + (1-w)*view[2], view[1] + view[3]/2, z1) );
        GLvector rightY = gluUnProject( GLvector( view[0] + (1-w)*view[2], view[1] + view[3]/2+1, z0) );
        rightZ=rightZ-rightPlane;
        rightY=rightY-rightPlane;
        rightNormal = ((rightY)^(rightZ)).Normalize();

        leftPlane = gluUnProject( GLvector( view[0]+w*view[2], view[1] + view[3]/2, z0) );
        GLvector leftZ = gluUnProject( GLvector( view[0]+w*view[2], view[1] + view[3]/2, z1) );
        GLvector leftY = gluUnProject( GLvector( view[0]+w*view[2], view[1] + view[3]/2+1, z0) );
        leftNormal = ((leftZ-leftPlane)^(leftY-leftPlane)).Normalize();

        topPlane = gluUnProject( GLvector( view[0] + view[2]/2, view[1] + (1-h)*view[3], z0) );
        GLvector topZ = gluUnProject( GLvector( view[0] + view[2]/2, view[1] + (1-h)*view[3], z1) );
        GLvector topX = gluUnProject( GLvector( view[0] + view[2]/2+1, view[1] + (1-h)*view[3], z0) );
        topNormal = ((topZ-topPlane)^(topX-topPlane)).Normalize();

        bottomPlane = gluUnProject( GLvector( view[0] + view[2]/2, view[1]+h*view[3], z0) );
        GLvector bottomZ = gluUnProject( GLvector( view[0] + view[2]/2, view[1]+h*view[3], z1) );
        GLvector bottomX = gluUnProject( GLvector( view[0] + view[2]/2+1, view[1]+h*view[3], z0) );
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

static std::vector<GLvector> clipFrustum( GLvector corner[4], GLvector &closest_i, float w=0, float h=0 ) {
    std::vector<GLvector> l;
    for (unsigned i=0; i<4; i++)
        l.push_back( corner[i] );
    return clipFrustum(l, closest_i, w, h);
}

/**
  @arg timePixels Estimated longest line of pixels along t-axis within ref measured in pixels
  @arg scalePixels Estimated longest line of pixels along t-axis within ref measured in pixels
  */
bool Renderer::computePixelsPerUnit( Reference ref, float& timePixels, float& scalePixels )
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

void Renderer::drawAxes( float T )
{
    // Draw overlay borders, on top, below, to the right or to the left
    // default left bottom

    // 1 gray draw overlay
    // 2 clip entire sound to frustum
    // 3 decide upon scale
    // 4 draw axis

    float w = 0.1f, h=0.05f;
    { // 1 gray draw overlay
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();

        glLoadIdentity();
        gluOrtho2D( 0, 1, 0, 1 );

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        //glEnable(GL_BLEND);
        //glBlendFunc( GL_ONE, GL_SRC_ALPHA );
        glDisable(GL_DEPTH_TEST);

        glColor4f( .4f, .4f, .4f, .4f );
        glBegin( GL_QUADS );
            glVertex2f( 0, 0 );
            glVertex2f( w, 0 );
            glVertex2f( w, 1 );
            glVertex2f( 0, 1 );

            glVertex2f( w, 0 );
            glVertex2f( w, h );
            glVertex2f( 1-w, h );
            glVertex2f( 1-w, 0 );

            glVertex2f( 1, 0 );
            glVertex2f( 1-w, 0 );
            glVertex2f( 1-w, 1 );
            glVertex2f( 1, 1 );

            glVertex2f( w, 1 );
            glVertex2f( w, 1-h );
            glVertex2f( 1-w, 1-h );
            glVertex2f( 1-w, 1 );
        glEnd();

        glEnable(GL_DEPTH_TEST);

        //glDisable(GL_BLEND);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }

    // 2 clip entire sound to frustum
    clippedFrustum.clear();

    {   //float T = _collection->worker->source()->length();
        GLvector closest_i;
        GLvector corner[4]=
        {
            GLvector( 0, 0, 0),
            GLvector( 0, 0, 1),
            GLvector( T, 0, 1),
            GLvector( T, 0, 0),
        };

        clippedFrustum = clipFrustum(corner, closest_i, w, h);
    }

    // 3 decide upon scale
    float circumference = 0;
    float DT=0, DF=0, ST=0, SF=0;
    int st = 0, sf = 0;
    GLvector inside;
    {   float mint=FLT_MAX, maxt=0, minf=1, maxf=0;
        for (unsigned i=0; i<clippedFrustum.size(); i++)
        {
            unsigned j=(i+1)%clippedFrustum.size();

            GLvector a = clippedFrustum[j]-clippedFrustum[i];
            inside = inside + clippedFrustum[i];

            if (a[0] < mint) mint = a[0];
            if (a[0] > maxt) maxt = a[0];
            if (a[2] < minf) minf = a[2];
            if (a[2] > maxf) maxf = a[2];
            circumference += a.length();
        }
        // as clippedFrustum is a convex polygon, the mean position of its vertices will be inside
        inside = inside * (1.f/clippedFrustum.size());

        ST = maxt-mint;
        SF = maxf-minf;

        if (clippedFrustum.size())
        {
            st = 0, sf = 0;
            while( powf(10, st) < ST*.5f ) st++;
            while( powf(10, st) > ST*.5f ) st--;
            while( powf(10, sf) < SF*.5f ) sf++;
            while( powf(10, sf) > SF*.5f ) sf--;

            st-=1;
            sf-=1;

            DT = powf(10, st);
            DF = powf(10, sf);
        }
    }


    // 4 render
    GLvector x(1,0,0), z(0,0,1);

    //glEnable(GL_BLEND);
    glDepthMask(false);
    glDisable(GL_DEPTH_TEST);
    //glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    float min_hz = Tfr::CwtSingleton::instance()->min_hz();
    float max_hz = Tfr::CwtSingleton::instance()->max_hz( _collection->worker->source()->sample_rate() );
    float steplogsize = log(max_hz) - log(min_hz);
    //float steplogsize = log(max_hz-min_hz);
    // loop along all sides
    for (unsigned i=0; i<clippedFrustum.size(); i++)
    {
        glColor4f(0,0,0,1);
        unsigned j=(i+1)%clippedFrustum.size();
        GLvector p = clippedFrustum[i]; // starting point of side
        GLvector v = clippedFrustum[j]-p; // vector pointing from p to the next vertex

        if (!v[0] && !v[2]) // skip if |v| = 0
            continue;

        // compute index of next marker along t and f
        unsigned t = p[0]/DT; // t marker index along t
        if (v[0] > 0) t++;

        unsigned fc, f = exp(p[2]*steplogsize)*min_hz; // t marker index along f
        for(fc = 1; fc*10 < f; fc*=10) {}

        f = f/fc*fc;
        if (10*fc==f) { fc*=10; }
        // unsigned p[2]/DF; // t marker index along f
        if (v[2] > 0) f+=fc;

        // decide if this side is a t or f axis
        bool taxis = fabsf(v[0]*SF) > fabsf(v[2]*ST);

        if (taxis || draw_hz)
        for (float u=0; true; )
        {
            // find next intersection along v
            float nu;
            if (taxis)  nu = (t*DT - clippedFrustum[i][0])/v[0];
            else        nu = (log(f/min_hz)/steplogsize - clippedFrustum[i][2])/v[2];

            // if valid intersection
            if ( nu > u && nu<1 ) { u = nu; }
            else break;

            // compute intersection
            p = clippedFrustum[i]+v*u;

            // draw marker
            if (taxis) {
                float size = 1+ (0 == (t%10));
                glLineWidth(size);

                float sign = (v^z)%(v^( p - inside))>0 ? 1.f : -1.f;
                float o = size*SF*h*.1f*sign;

                glBegin(GL_LINES);
                    glVertex3f( p[0], 0, p[2] );
                    glVertex3f( p[0], 0, p[2] - o);
                glEnd();

                if (size>1) {
                    glLineWidth(1);
                    glPushMatrix();
                        glTranslatef(p[0], 0, p[2]);
//                        glRotatef(90,0,1,0);
                        glRotatef(90,1,0,0);
                        glScalef(0.00012f*ST,0.00012f*SF,1.f);
                        char a[100];
                        char b[100];
                        sprintf(b,"%%.%df", st<0?-1-st:0);
                        sprintf(a, b, t*DT);
                        float w=0;
                        float letter_spacing=15;

                        for (char*c=a;*c!=0; c++) {
                            if (c!=a)
                                w+=letter_spacing;
                            w+=glutStrokeWidth( GLUT_STROKE_ROMAN, *c );
                        }

                        glTranslatef(-.5f*w,sign*120-50.f,0);
                        for (char*c=a;*c!=0; c++) {
                            glutStrokeCharacter(GLUT_STROKE_ROMAN, *c);
                            glTranslatef(letter_spacing,0,0);
                        }
                    glPopMatrix();
                }

                if (v[0] > 0) t++;
                if (v[0] < 0) t--;
            } else {
                float size = 1+ (1 == (f/fc));
                glLineWidth(size);

                float sign = (v^x)%(v^( p - inside))>0 ? 1.f : -1.f;
                float o = size*ST*w*.1f*sign;
                glBegin(GL_LINES);
                    glVertex3f( p[0], 0, p[2] );
                    glVertex3f( p[0] - o, 0, p[2] );
                glEnd();

                if (size>1 || SF<.8f)
                {
                    glLineWidth(1);
                    glPushMatrix();
                        glTranslatef(p[0],0,p[2]);
                        glRotatef(90,1,0,0);
                        glScalef(0.00014f*ST,0.00014f*SF,1.f);
                        char a[100];
                        sprintf(a,"%d", f);
                        unsigned w=20;
                        if (sign<0) {
                            for (char*c=a;*c!=0; c++)
                                w+=glutStrokeWidth( GLUT_STROKE_ROMAN, *c );
                        }
                        glTranslatef(sign*w,-50.f,0);
                        for (char*c=a;*c!=0; c++)
                            glutStrokeCharacter(GLUT_STROKE_ROMAN, *c);
                    glPopMatrix();
                }

                if (v[2] > 0) {
                    if (10*fc==f) { fc*=10; }
                    f+=fc;
                    if (10*fc==f) { fc*=10; }
                }
                if (v[2] < 0) { if (fc==f) { fc/=10; } f-=fc; }
            }
        }

        if (!taxis && draw_piano)
        {
            // from http://en.wikipedia.org/wiki/Piano_key_frequencies
            // F(n) = 440 * pow(pow(2, 1/12),n-49)
            // log(F(n)/440) = log(pow(2, 1/12),n-49)
            // log(F(n)/440) = log(pow(2, 1/12))*log(n-49)
            // log(F(n)/440)/log(pow(2, 1/12)) = log(n-49)
            // n = exp(log(F(n)/440)/log(pow(2, 1/12))) + 49

            unsigned F1 = exp(clippedFrustum[i][2]*steplogsize)*min_hz;
            unsigned F2 = exp(clippedFrustum[j][2]*steplogsize)*min_hz;
            if (F2<F1) { unsigned swap = F2; F2=F1; F1=swap; }
            if (!(F1>min_hz)) F1=min_hz;
            if (!(F2<max_hz)) F2=max_hz;
            float tva12 = powf(2.f, 1.f/12);


            int startTone = log(F1/440.f)/log(tva12) + 45;
            int endTone = log(F2/440.f)/log(tva12) + 44;
            float sign = (v^x)%(v^( clippedFrustum[i] - inside))>0 ? 1.f : -1.f;

            for( int tone = startTone; tone<=endTone; tone++)
            {
                float ff = log(440 * pow(tva12,tone-45)/min_hz)/steplogsize;
                float ffN = log(440 * pow(tva12,tone-44)/min_hz)/steplogsize;
                float ffP = log(440 * pow(tva12,tone-46)/min_hz)/steplogsize;

                int toneTest = tone;
                while(toneTest<0) toneTest+=12;

                bool blackKey = false;
                switch(toneTest%12) { case 1: case 3: case 6: case 8: case 10: blackKey = true; }
                bool blackKeyP = false;
                switch((toneTest+11)%12) { case 1: case 3: case 6: case 8: case 10: blackKeyP = true; }
                bool blackKeyN = false;
                switch((toneTest+1)%12) { case 1: case 3: case 6: case 8: case 10: blackKeyN = true; }
                glLineWidth(1);
                float wN = ffN-ff, wP = ff-ffP;
                if (blackKey)
                    wN *= .5, wP *= .5;
                else {
                    if (!blackKeyN)
                        wN *= .5;
                    if (!blackKeyP)
                        wP *= .5;
                }

                float u = (ff - clippedFrustum[i][2])/v[2];
                float un = (ff+wN - clippedFrustum[i][2])/v[2];
                float up = (ff-wP - clippedFrustum[i][2])/v[2];
                GLvector pt = clippedFrustum[i]+v*u;
                GLvector pn = clippedFrustum[i]+v*un;
                GLvector pp = clippedFrustum[i]+v*up;
                    glPushMatrix();
                    if (sign>0)
                        glTranslatef( sign*ST*0.14f, 0.f, 0.f );
                    else
                        glTranslatef( -sign*ST*0.08f, 0.f, 0.f );
    glColor4f(0,0,0,.4);
            glBegin(GL_LINES );
                glVertex3f(pn[0] - ST*0.14f, 0, pn[2]);
                glVertex3f(pp[0] - ST*0.14f, 0, pp[2]);
            glEnd();
            glBegin(blackKey ? GL_QUADS:GL_LINE_STRIP );
                glVertex3f(pp[0] - ST*(.14f - .036f*blackKeyP), 0, pp[2]);
                glVertex3f(pp[0] - ST*(.08f + .024f*blackKey), 0, pp[2]);
                glVertex3f(pn[0] - ST*(.08f + .024f*blackKey), 0, pn[2]);
                glVertex3f(pn[0] - ST*(.14f - .036f*blackKeyN), 0, pn[2]);
            glEnd();
    glColor4f(0,0,0,1);
                    glPopMatrix();
                if (tone%12 == 0) {
                    glLineWidth(1.f);
                    glPushMatrix();
                    glTranslatef(.5f*pn[0]+.5f*pp[0],0,.5f*pn[2]+.5f*pp[2]);
                    glRotatef(90,1,0,0);

                    glScalef(0.00014f*ST,0.00014f*SF,1.f);

                    char a[100];
                    sprintf(a,"C%d", tone/12+1);
                    unsigned w=20;
                    if (sign<0) {
                        for (char*c=a;*c!=0; c++)
                            w+=glutStrokeWidth( GLUT_STROKE_ROMAN, *c );
                    }
                    glTranslatef(sign*w,-50.f,0);
                    for (char*c=a;*c!=0; c++)
                        glutStrokeCharacter(GLUT_STROKE_ROMAN, *c);
                    glPopMatrix();
                }
            }
        }
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(true);
    //glDisable(GL_BLEND);
}

void Renderer::
        drawFrustum()
{
    if (clippedFrustum.empty())
        return;

    glPushAttribContext ac;

    glDisable(GL_DEPTH_TEST);

    glPushMatrixContext mc;

    glColor4f(0,0,0,.125);
    glBegin( GL_TRIANGLE_FAN );
        for ( std::vector<GLvector>::const_iterator i = clippedFrustum.begin();
                i!=clippedFrustum.end();
                i++)
        {
            glVertex3dv( i->v );
        }
    glEnd();

    glColor4f(0,0,0,.5);
    glBegin( GL_LINE_LOOP );
        for ( std::vector<GLvector>::const_iterator i = clippedFrustum.begin();
                i!=clippedFrustum.end();
                i++)
        {
            glVertex3dv( i->v );
        }
    glEnd();
}

} // namespace Heightmap
