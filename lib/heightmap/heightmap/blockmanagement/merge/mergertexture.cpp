﻿#include "mergertexture.h"

#include "heightmap/blockquery.h"
#include "mergekernel.h"
#include "heightmap/render/shaderresource.h"
#include "heightmap/render/blocktextures.h"

#include "tasktimer.h"
#include "computationkernel.h"
#include "gltextureread.h"
#include "glframebuffer.h"
#include "datastoragestring.h"
#include "GlException.h"
#include "glPushContext.h"
#include "glprojection.h"
#include "gluperspective.h"

#include <QGLContext>

#ifdef GL_ES_VERSION_2_0
#define DRAW_STRAIGHT_ONTO_BLOCK
#endif

//#define VERBOSE_COLLECTION
#define VERBOSE_COLLECTION if(0)

//#define INFO_COLLECTION
#define INFO_COLLECTION if(0)

using namespace Signal;
using namespace std;

void printUniformInfo(int program)
{
    int n_uniforms = 0;
    int len_uniform = 0;
    glGetProgramiv (program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &len_uniform);
    glGetProgramiv (program, GL_ACTIVE_UNIFORMS, &n_uniforms);
    char name[len_uniform];
    Log("Found %d uniforms in program") % n_uniforms;
    for (int i=0; i<n_uniforms; i++) {
        GLint size;
        GLenum type;
        glGetActiveUniform(program,
            i,
            len_uniform,
            0,
            &size,
            &type,
            name);
        Log("%d: %s, size=%d, type=%d") % i % ((char*)name) % size % type;
    }
}

namespace Heightmap {
namespace BlockManagement {
namespace Merge {

typedef pair<Region,pBlock> RegionBlock;
typedef vector<RegionBlock> RegionBlockVector;

RegionBlockVector& operator-=(RegionBlockVector& R, Region t)
{
    RegionBlockVector C;
    C.reserve (R.size ());

    for (auto v : R) {
        Region r = v.first;
        if (r.a.time >= t.b.time || r.b.time <= t.a.time || r.a.scale >= t.b.scale || r.b.scale <= t.a.scale) {
            // keep whole
            C.push_back (v);
            continue;
        }

        if (r.a.scale < t.a.scale) {
            // keep bottom
            C.push_back (RegionBlock(Region(Position(r.a.time, r.a.scale),Position(r.b.time,t.a.scale)),v.second));
        }
        if (r.b.scale > t.b.scale) {
            // keep top
            C.push_back (RegionBlock(Region(Position(r.a.time, t.b.scale),Position(r.b.time,r.b.scale)),v.second));
        }
        if (r.a.time < t.a.time) {
            // keep left
            C.push_back (RegionBlock(Region(Position(r.a.time, max(r.a.scale,t.a.scale)),Position(t.a.time,min(r.b.scale,t.b.scale))),v.second));
        }
        if (r.b.time > t.b.time) {
            // keep right
            C.push_back (RegionBlock(Region(Position(t.b.time, max(r.a.scale,t.a.scale)),Position(r.b.time,min(r.b.scale,t.b.scale))),v.second));
        }
    }

    return R = C;
}


void testRegionBlockOperator() {
    // RegionBlockVector& operator-=(RegionBlockVector& R, Region t) should work
    try {
        Reference ref;
        ref.log2_samples_size[0] = -10;
        ref.log2_samples_size[1] = -10;
        ref.block_index[0] = 1;
        ref.block_index[1] = 1;

        BlockLayout bl(128,128,100);
        RegionFactory f(bl);
        RegionBlockVector V = {RegionBlock(f(ref),pBlock())};

        V -= f(ref.left ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 1u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref.right ()) );
        V -= f(ref.top ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 1u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref.right ().bottom ()) );

        V = {RegionBlock(f(ref),pBlock())};
        V -= f(ref.right ().top ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 2u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref.bottom ()) );
        EXCEPTION_ASSERT_EQUALS( V[1].first, f(ref.top ().left ()) );

        V = {RegionBlock(f(ref),pBlock())};
        V -= f(ref.sibblingLeft ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 1u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref) );
        V -= f(ref.sibblingRight ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 1u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref) );
        V -= f(ref.sibbling1 ());
        V -= f(ref.sibbling2 ());
        V -= f(ref.sibbling3 ());
        V -= f(ref.sibblingTop ());
        V -= f(ref.sibblingBottom ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 1u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref) );

        V -= f(ref.parentHorizontal ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 0u );

        V = {RegionBlock(f(ref),pBlock())};
        V -= f(ref.parentHorizontal ().top ());
        EXCEPTION_ASSERT_EQUALS( V.size(), 1u );
        EXCEPTION_ASSERT_EQUALS( V[0].first, f(ref.parentHorizontal ().bottom ().right ()) );
    } catch (...) {
        Log("%s: %s") % __FUNCTION__ % boost::current_exception_diagnostic_information ();
        throw;
    }
}


MergerTexture::
        MergerTexture(BlockCache::const_ptr cache, BlockLayout block_layout, bool disable_merge)
    :
      cache_(cache),
      vbo_(0),
      tex_(0),
      block_layout_(block_layout),
      disable_merge_(disable_merge),
      program_(0)
{
}


MergerTexture::
        ~MergerTexture()
{
    if (program_) glDeleteProgram(program_);
    program_ = 0;

    if (vbo_) glDeleteBuffers (1, &vbo_);
    vbo_ = 0;
}


void MergerTexture::
        init()
{
    if (vbo_)
        return;

    EXCEPTION_ASSERT(QGLContext::currentContext ());

#ifndef DRAW_STRAIGHT_ONTO_BLOCK
        tex_ = Render::BlockTextures::get1 ();
        fbo_.reset (new GlFrameBuffer(*tex_));
#endif

    glGenBuffers (1, &vbo_);
    float vertices[] = {
        0, 0, 0, 0,
        0, 1, 0, 1,
        1, 0, 1, 0,
        1, 1, 1, 1,
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    //    program_ = ShaderResource::loadGLSLProgram("", ":/shaders/mergertexture.frag");
//    program_ = ShaderResource::loadGLSLProgram(":/shaders/mergertexture.vert", ":/shaders/mergertexture0.frag");
    program_ = ShaderResource::loadGLSLProgram(":/shaders/mergertexture.vert", ":/shaders/mergertexture.frag");

    qt_Vertex = glGetAttribLocation (program_, "qt_Vertex");
    qt_MultiTexCoord0 = glGetAttribLocation (program_, "qt_MultiTexCoord0");
    qt_Texture0 = glGetUniformLocation(program_, "qt_Texture0");
    invtexsize = glGetUniformLocation(program_, "invtexsize");
    uniProjection = glGetUniformLocation (program_, "qt_ProjectionMatrix");
    uniModelView = glGetUniformLocation (program_, "qt_ModelViewMatrix");
}

Signal::Intervals MergerTexture::
        fillBlocksFromOthers( const std::vector<pBlock>& blocks )
{
    init ();
    INFO_COLLECTION TaskTimer tt(boost::format("MergerTexture: fillBlocksFromOthers %s blocks") % blocks.size ());

    GlException_CHECK_ERROR();

    glClearColor (0,0,0,1);

    glViewport(0, 0, block_layout_.texels_per_row (), block_layout_.texels_per_column () );

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    glDisable (GL_CULL_FACE);

    struct vertex_format {
        float x, y, u, v;
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glEnableVertexAttribArray (qt_Vertex);
    glEnableVertexAttribArray (qt_MultiTexCoord0);
    glVertexAttribPointer (qt_Vertex, 2, GL_FLOAT, GL_TRUE, sizeof(vertex_format), 0);
    glVertexAttribPointer (qt_MultiTexCoord0, 2, GL_FLOAT, GL_TRUE, sizeof(vertex_format), (float*)0 + 2);

    glUseProgram (program_);
    if (invtexsize) glUniform2f(invtexsize, 1.0/block_layout_.texels_per_row (), 1.0/block_layout_.texels_per_column ());
    glUniform1i(qt_Texture0, 0); // GL_TEXTURE0 + i

    cache_clone = cache_->clone();

    Signal::Intervals I;

    {
    #ifndef DRAW_STRAIGHT_ONTO_BLOCK
        auto fboBinding = fbo_->getScopeBinding ();
        (void)fboBinding; // raii
    #endif

        for (pBlock b : blocks)
            I |= fillBlockFromOthersInternal (b);
    }

    cache_clone.clear ();

    glUseProgram (0);

    glDisableVertexAttribArray (qt_MultiTexCoord0);
    glDisableVertexAttribArray (qt_Vertex);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable (GL_DEPTH_TEST);
    glEnable (GL_CULL_FACE);

    for (pBlock b : blocks)
    {
        glBindTexture (GL_TEXTURE_2D, b->texture ()->getOpenGlTextureId());
        glGenerateMipmap (GL_TEXTURE_2D);
        glBindTexture (GL_TEXTURE_2D, 0);
    }

    GlException_CHECK_ERROR();

    return I;
}


Signal::Intervals MergerTexture::
        fillBlockFromOthersInternal( pBlock block )
{
    Signal::Intervals missing_details;

    VERBOSE_COLLECTION TaskTimer tt(boost::format("MergerTexture: Stubbing new block %s") % block->getRegion ());

    Region r = block->getRegion ();

    matrixd projection;
    glhOrtho (projection.v (), r.a.time, r.b.time, r.a.scale, r.b.scale, -10, 10);

    glUniformMatrix4fv (uniProjection, 1, false, GLmatrixf(projection).v ());

#ifdef DRAW_STRAIGHT_ONTO_BLOCK
    GlFrameBuffer fbo(*block->texture ());
    auto fboBinding = fbo.getScopeBinding ();
    (void)fboBinding; // raii
#endif

    glClear( GL_COLOR_BUFFER_BIT );

    if (!disable_merge_)
    {
        class isRegionLarger{
        public:
            bool operator()(const pBlock& a, const pBlock& b) const {
                const Region ra = a->getRegion ();
                const Region rb = b->getRegion ();
                float va = ra.time ()*ra.scale ();
                float vb = rb.time ()*rb.scale ();
                if (va != vb)
                    return va > vb;
                if (ra.time () != rb.time())
                    return ra.time () > rb.time();
                if (ra.scale () != rb.scale())
                    return ra.scale () > rb.scale();

                // They have the same size, order by position
                if (ra.a.time != rb.a.time)
                    return ra.a.time > rb.a.time;
                return ra.a.scale > rb.a.scale;
            }
        };

        // Largest first
        RegionBlockVector largeblocks;
        RegionBlockVector smallblocks;

        for (const auto& c : cache_clone)
        {
            const pBlock& bl = c.second;
            const Region& r2 = bl->getRegion ();

            // If r2 doesn't overlap r at all
            if (r2.a.scale >= r.b.scale || r2.b.scale <= r.a.scale )
                continue;
            if (r2.a.time >= r.b.time || r2.b.time <= r.a.time )
                continue;

            bool is_small_s = r2.scale () <= r.scale ();
            bool is_small_t = r2.time () <= r.time ();
            if (is_small_s && is_small_t)
                smallblocks.push_back (RegionBlock(r2,bl));
            else
                largeblocks.push_back (RegionBlock(r2,bl));
        }

        RegionBlockVector missing_details_region{RegionBlock(r,block)};
        for (auto r : smallblocks) {
            largeblocks -= r.first;
            missing_details_region -= r.first;
        }

        double fs = block_layout_.sample_rate ();
        for (auto v: missing_details_region)
        {
            Region r2 = v.first;
            double d = 0.5/v.second->sample_rate();
            missing_details |= Signal::Interval((r2.a.time-d)*fs, (r2.b.time+d)*fs+1);
        }

        for (auto v : largeblocks)
        {
            auto bl = v.second;
            mergeBlock( bl->getRegion (), bl->texture ()->getOpenGlTextureId () );
        }

        for (auto v : smallblocks)
        {
            auto bl = v.second;
            mergeBlock( bl->getRegion (), bl->texture ()->getOpenGlTextureId () );
        }
    }

#ifndef DRAW_STRAIGHT_ONTO_BLOCK
    {
        VERBOSE_COLLECTION TaskTimer tt(boost::format("Filled %s") % block->getRegion ());

        GlTexture::ptr t = block->texture ();
        #ifdef GL_ES_VERSION_2_0
            GlException_SAFE_CALL( glCopyTextureLevelsAPPLE(t->getOpenGlTextureId (), fbo_->getGlTexture (), 0, 1) );
        #else
            glBindTexture(GL_TEXTURE_2D, t->getOpenGlTextureId ());
            GlException_SAFE_CALL( glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, 0,0, t->getWidth (), t->getHeight ()) );
            glBindTexture(GL_TEXTURE_2D, 0);
        #endif
    }
#endif

    return missing_details;
}


void MergerTexture::
        clearBlock( const Region& ri )
{
    // Read from unbound texture, i.e paint zero
    mergeBlock(ri,0);
}


void MergerTexture::
        mergeBlock( const Region& ri, int texture )
{
    VERBOSE_COLLECTION TaskTimer tt(boost::format("MergerTexture: Filling with %d from %s") % texture % ri);

    matrixd modelview = matrixd::identity ();
    modelview *= matrixd::translate (ri.a.time, ri.a.scale, 0);
    modelview *= matrixd::scale (ri.time (), ri.scale (), 1.f);

    glUniformMatrix4fv (uniModelView, 1, false, GLmatrixf(modelview).v ());

    glBindTexture( GL_TEXTURE_2D, texture);
    // disable mipmaps while resampling contents if downsampling, but not when upsampling
    if (texture) glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); // Paint new contents over it
    if (texture) glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    if (texture) glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace Merge
} // namespace BlockManagement
} // namespace Heightmap


#include "log.h"
#include "cpumemorystorage.h"
#include "heightmap/render/blocktextures.h"
#include <QtWidgets> // QApplication
#include <QtOpenGL> // QGLWidget

namespace Heightmap {
namespace BlockManagement {
namespace Merge {

static void clearCache(BlockCache::ptr cache) {
    auto c = cache->clear ();
    c.clear();
}


void MergerTexture::
        test()
{
    std::string name = "MergerTexture";
    int argc = 1;
    char * argv = &name[0];
    QApplication a(argc,&argv);
    QGLWidget w;
    w.makeCurrent ();

    testRegionBlockOperator();

    // It should merge contents from other blocks to stub the contents of a new block.
    {
        BlockCache::ptr cache(new BlockCache);

        Reference ref;
        BlockLayout bl(4,4,4);
        Render::BlockTextures::Scoped bt_raii(bl.texels_per_row (), bl.texels_per_column ());
        VisualizationParams::ptr vp(new VisualizationParams);

        // VisualizationParams has only things that have nothing to do with MergerTexture.
        pBlock block(new Block(ref,bl,vp));
        GlTexture::ptr tex = block->texture ();

        MergerTexture(cache, bl).fillBlockFromOthers(block);

        DataStorage<float>::ptr data;

        float expected1[]={ 0, 0, 0, 0,
                            0, 0, 0, 0,
                            0, 0, 0, 0,
                            0, 0, 0, 0};

        data = GlTextureRead(*tex).readFloat (0, GL_RED);
        //data = block->block_data ()->cpu_copy;
        COMPARE_DATASTORAGE(expected1, sizeof(expected1), data);

        {
            float srcdata[]={ 1, 0, 0, .5,
                              0, 0, 0, 0,
                              0, 0, 0, 0,
                             .5, 0, 0, .5};

            pBlock block(new Block(ref.parentHorizontal (),bl,vp));
            GlTexture::ptr tex = block->texture ();
            auto ts = tex->getScopeBinding ();
            GlException_SAFE_CALL( glTexSubImage2D(GL_TEXTURE_2D,0,0,0, 4, 4, GL_RED, GL_FLOAT, srcdata) );

            cache->insert(block);
            (void)ts;
        }

        MergerTexture(cache, bl).fillBlockFromOthers(block);
        clearCache(cache);
        float a = 1.0, b = 0.75,  c = 0.25;
        float expected2[]={   a,   b,   c,   0,
                              0,   0,   0,   0,
                              0,   0,   0,   0,
                              a/2, b/2, c/2, 0};
        data = GlTextureRead(*tex).readFloat (0, GL_RED);
        //data = block->block_data ()->cpu_copy;
        COMPARE_DATASTORAGE(expected2, sizeof(expected2), data);

        {
            float srcdata[]={ 1, 2, 3, 4,
                              5, 6, 7, 8,
                              9, 10, 11, 12,
                              13, 14, 15, .16};

            pBlock block(new Block(ref.right (),bl,vp));
            GlTexture::ptr tex = block->texture ();
            auto ts = tex->getScopeBinding ();
            GlException_SAFE_CALL( glTexSubImage2D(GL_TEXTURE_2D,0,0,0, 4, 4, GL_RED, GL_FLOAT, srcdata) );

            cache->insert(block);
            (void)ts;
        }

        MergerTexture(cache, bl).fillBlockFromOthers(block);
        float v16 = 7.57812476837;
        //float v32 = 7.58;
        float expected3[]={   0, 0,    1.5,  3.5,
                              0, 0,    5.5,  7.5,
                              0, 0,    9.5,  11.5,
                              0, 0,   13.5,  v16};

        data = GlTextureRead(*tex).readFloat (0, GL_RED);
        //data = block->block_data ()->cpu_copy;
        COMPARE_DATASTORAGE(expected3, sizeof(expected3), data);
        clearCache(cache);

        tex.reset ();
    }
}

} // namespace Merge
} // namespace BlockManagement
} // namespace Heightmap
