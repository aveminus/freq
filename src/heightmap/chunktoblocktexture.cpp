#include "chunktoblocktexture.h"
#include "glblock.h"
#include "tfr/chunk.h"

#include "glframebuffer.h"
#include "TaskTimer.h"
#include "gl.h"
#include "GlException.h"
#include "shaderresource.h"
#include "glPushContext.h"
#include "gltextureread.h"
#include "datastoragestring.h"

//#define PRINT_TEXTURES
#define PRINT_TEXTURES if(0)

//#define INFO
#define INFO if(0)

namespace Heightmap {

struct vertex_format {
    float x, y, u, v;
};

ChunkToBlockTexture::
        ChunkToBlockTexture(Tfr::pChunk chunk)
    :
      vbo_(0)
{
    shader_ = ShaderResource::loadGLSLProgram("", ":/shaders/complex_magnitude.frag");
    glUseProgram(shader_);
    int loc = glGetUniformLocation(shader_, "mytex");
    normalization_location_ = glGetUniformLocation(shader_, "normalization");
    amplitude_axis_location_ = glGetUniformLocation(shader_, "amplitude_axis");
    glUniform1i(loc, 0); // mytex corresponds to GL_TEXTURE0
    glUseProgram(0);

    nScales = chunk->nScales ();
    nSamples = chunk->nSamples ();
    transpose = chunk->order == Tfr::Chunk::Order_column_major;
    unsigned data_width  = transpose ? nScales : nSamples,
             data_height = transpose ? nSamples : nScales;
    chunk_texture_.reset (new GlTexture( data_width, data_height, GL_RG, GL_RG, GL_FLOAT, chunk->transform_data->getCpuMemory ()));

    chunk_scale = chunk->freqAxis;
    a_t = (chunk->chunk_offset/chunk->sample_rate).asFloat();
    b_t = a_t + nSamples/chunk->sample_rate;
}


void ChunkToBlockTexture::
        prepVbo(Tfr::FreqAxis display_scale)
{
    if (this->display_scale == display_scale)
        return;
    this->display_scale = display_scale;

    float a_t = this->a_t;
    float b_t = this->b_t;
    unsigned Y = nScales;
    bool transpose = this->transpose;
    const Tfr::FreqAxis& chunk_scale = this->chunk_scale;

//    float min_hz = chunk_scale.getFrequency (0u);
//    float max_hz = chunk_scale.getFrequency (Y);

//    Region chunk_region = Region(Position(),Position());
//    chunk_region.a.time = a_t;
//    chunk_region.b.time = b_t;
//    chunk_region.a.scale = display_scale.getFrequencyScalar (min_hz);
//    chunk_region.b.scale = display_scale.getFrequencyScalar (max_hz);
//    INFO TaskTimer tt(boost::format("Creating VBO for %s") % chunk_region);

    std::vector<float> vertices((Y*2)*sizeof(vertex_format)/sizeof(float));
    int i=0;
    float iY = 1.f/(Y-1);

    float dt = b_t - a_t;
    a_t -= 0.5 * dt / nSamples;
    b_t += 0.5 * dt / nSamples;

    float ky = (1.f + 1.f/Y);
    float oy = 0.5f/Y;

    for (unsigned y=0; y<Y; y++) {
        float hz = chunk_scale.getFrequency (y * ky - oy );
        float s = display_scale.getFrequencyScalar(hz);
        vertices[i++] = a_t;
        vertices[i++] = s;
        vertices[i++] = transpose ? y*iY : 0;
        vertices[i++] = transpose ? 0 : y*iY;
        vertices[i++] = b_t;
        vertices[i++] = s;
        vertices[i++] = transpose ? y*iY : 1;
        vertices[i++] = transpose ? 1 : y*iY;
    }

    GlException_CHECK_ERROR();

    if (vbo_)
      {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData (GL_ARRAY_BUFFER, 0, sizeof(float)*vertices.size (), &vertices[0]);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        GlException_CHECK_ERROR();
      }
    else
      {
        glGenBuffers (1, &vbo_); // Generate 1 buffer
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float)*vertices.size (), &vertices[0], GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        GlException_CHECK_ERROR();
      }
}


ChunkToBlockTexture::
        ~ChunkToBlockTexture()
{
    glDeleteBuffers (1, &vbo_);

    glUseProgram(0);
    glDeleteProgram(shader_);
}


void ChunkToBlockTexture::
        mergeChunk( const pBlock pblock )
{
    INFO TaskTimer tt("ChunkToBlockTexture::mergeChunk");

    GlException_CHECK_ERROR();

    Block& block = *pblock;
    VisualizationParams::ConstPtr vp = block.visualization_params ();

    // Setup the VBO, need to know the current display scale, which is defined by the block.
    prepVbo(vp->display_scale ());

    // Setup framebuffer rendering
    GlTexture::Ptr t = block.glblock->glTexture ();
    GlFrameBuffer fbo(t->getOpenGlTextureId ());
    GlFrameBuffer::ScopeBinding sb = fbo.getScopeBinding ();

    // Disable unwanted capabilities when resampling a texture
    glPushAttribContext pa(GL_ENABLE_BIT);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_BLEND);
    glDisable (GL_CULL_FACE);

    // Setup matrices, while preserving the old ones
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    GlException_SAFE_CALL( glViewport(0, 0, fbo.getWidth (), fbo.getHeight () ) );
    glPushMatrixContext mpc( GL_PROJECTION );
    glLoadIdentity();
    Region block_region = block.getRegion ();
    glOrtho(block_region.a.time, block_region.b.time, block_region.a.scale, block_region.b.scale, -10,10);
    glPushMatrixContext mc( GL_MODELVIEW );
    glLoadIdentity();

    // Setup drawing with VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexPointer(2, GL_FLOAT, sizeof(vertex_format), 0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vertex_format), (float*)0 + 2);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    // Setup the shader
    glUseProgram(shader_);
    glUniform1f(normalization_location_, normalization_factor);
    glUniform1i(amplitude_axis_location_, (int)vp->amplitude_axis());

    // Draw from chunk texture onto block texture
    {
        GlTexture::ScopeBinding texObjBinding = chunk_texture_->getScopeBinding();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, nScales*2);
    }

    // Finish with shader
    glUseProgram(0);

    // Finish drawing with WBO
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Restore old matrices
    GlException_SAFE_CALL( glViewport(viewport[0], viewport[1], viewport[2], viewport[3] ) );

    PRINT_TEXTURES PRINT_DATASTORAGE(GlTextureRead(fbo.getGlTexture()).readFloat (), "fbo");
    PRINT_TEXTURES PRINT_DATASTORAGE(GlTextureRead(chunk_texture_->getOpenGlTextureId ()).readFloat (), "source");

    const_cast<Block*>(&block)->discard_new_block_data ();

    GlException_CHECK_ERROR();
}


} // namespace Heightmap



namespace Heightmap {

void ChunkToBlockTexture::
        test()
{
    // It should merge the contents of a chunk directly onto the texture of a block.
    {

    }
}

} // namespace Heightmap
