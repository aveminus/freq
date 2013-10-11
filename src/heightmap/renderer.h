#ifndef HEIGHTMAPRENDERER_H
#define HEIGHTMAPRENDERER_H

// Heightmap namespace
#include "reference.h"
#include "position.h"
#include "rendersettings.h"
#include "render/frustumclip.h"
#include "render/renderblock.h"
#include "render/renderheightmap.h"
#include "collection.h"

// gpumisc
#include "volatileptr.h"

// std
#include <vector>

// boost
#include <boost/shared_ptr.hpp>

class GlTexture;

namespace Heightmap {

class Renderer
{
public:
    Renderer();

    VolatilePtr<Collection>::Ptr    collection;
    RenderSettings                  render_settings;
    glProjection                    gl_projection;

    Reference findRefAtCurrentZoomLevel( Heightmap::Position p );

    void draw( float scaley );
    void drawAxes( float T );
    void drawFrustum();

    void setFractionSize( unsigned divW=1, unsigned divH=1 );
    bool fullMeshResolution();
    unsigned trianglesPerBlock();
    void setSize( unsigned w, unsigned h );
    bool isInitialized();
    void init();

    float redundancy();
    void redundancy(float value);

    void clearCaches();

private:

    enum InitializedLevel {
        NotInitialized,
        Initialized,
        InitializationFailed
    };

    InitializedLevel _initialized;
    bool _draw_flat;
    float _redundancy;
    bool _invalid_frustum;
    Render::FrustumClip _frustum_clip;
    std::vector<GLvector> clippedFrustum;
    Render::RenderBlock _render_block;
    unsigned _mesh_fraction_width;
    unsigned _mesh_fraction_height;

    void drawBlocks(const Render::RenderHeightmap::references_t& R);
    void drawReferences(const Render::RenderHeightmap::references_t& R);
};
typedef boost::shared_ptr<Renderer> pRenderer;

} // namespace Heightmap

#endif // HEIGHTMAPRENDERER_H
