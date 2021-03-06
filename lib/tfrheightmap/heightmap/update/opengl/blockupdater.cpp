#include "blockupdater.h"
#include "heightmap/update/tfrblockupdater.h"
#include "tfr/chunk.h"
#include "heightmap/render/blocktextures.h"
#include "heightmap/blockmanagement/blockupdater.h"

#include "pbo2texture.h"
#include "source2pbo.h"
#include "texture2fbo.h"
#include "texturepool.h"
#include "thread_pool.h"

#include "tasktimer.h"
#include "timer.h"
#include "log.h"
#include "neat_math.h"
#include "lazy.h"
#include "GlException.h"
#include "glgroupmarker.h"

#include <thread>
#include <future>
#include <unordered_map>

//#define INFO
#define INFO if(0)

#ifdef LEGACY_OPENGL
// the current implementation of PBOs doesn't reuse allocated memory
 #define USE_PBO
#endif

using namespace std;
using namespace JustMisc;

namespace std {
    template<class T>
    struct hash<boost::shared_ptr<T>>
    {
        std::size_t operator()(boost::shared_ptr<T> const& t) const
        {
            return std::hash<T*>()(t.get ());
        }
    };
}

namespace Heightmap {
namespace Update {
namespace OpenGL {

int gl_max_texture_size() {
    static int v = 0;
    if (0 == v)
        glGetIntegerv (GL_MAX_TEXTURE_SIZE, &v);
    return v;
}

TexturePool::FloatSize texture_storage() {
    return TfrBlockUpdater::Job::type == TfrBlockUpdater::Job::Data_F32
            ? TexturePool::Float32
            : TexturePool::Float16;
}

class BlockUpdaterPrivate
{
public:
    Shaders shaders;

    TexturePool texturePool
    {
        4, //gl_max_texture_size(),
        4, //std::min(gl_max_texture_size(),1024),
        texture_storage()
    };

#ifdef USE_PBO
    JustMisc::thread_pool memcpythread;

    BlockUpdaterPrivate()
        :
          memcpythread(1, "BlockUpdater")
    {}
#endif
};

BlockUpdater::
        BlockUpdater()
    :
      p(new BlockUpdaterPrivate)
{
}


BlockUpdater::
        ~BlockUpdater()
{
    delete p;
}


void BlockUpdater::
        processJobs( queue<UpdateQueue::Job>& jobs )
{
    // Select subset to work on, must consume jobs in order
    vector<UpdateQueue::Job> myjobs;
    while (!jobs.empty ())
    {
        UpdateQueue::Job& j = jobs.front ();
        if (dynamic_cast<const TfrBlockUpdater::Job*>(j.updatejob.get ()))
        {
            myjobs.push_back (std::move(j)); // Steal it
            jobs.pop ();
        }
        else
            break;
    }

    if (!myjobs.empty ())
        processJobs (myjobs);
}


void BlockUpdater::
        processJobs( vector<UpdateQueue::Job>& myjobs )
{
    GlGroupMarker gpm("BlockUpdater");

#ifdef USE_PBO
    // Begin chunk transfer to gpu right away
    // PBOs are not supported on OpenGL ES (< 3.0)
    unordered_map<Tfr::pChunk,lazy<Source2Pbo>> source2pbo;
    for (const UpdateQueue::Job& j : myjobs)
    {
        auto job = dynamic_cast<const TfrBlockUpdater::Job*>(j.updatejob.get ());

        Source2Pbo sp(job->chunk, job->type==TfrBlockUpdater::Job::Data_F32);
        p->memcpythread.addTask (sp.transferData(job->p));

        source2pbo[job->chunk] = move(sp);
    }
#endif

    // Prepare to draw with transferred chunk
    unordered_map<Tfr::pChunk, shared_ptr<Pbo2Texture>> pbo2texture;
#ifndef USE_PBO
    for (const UpdateQueue::Job& j : myjobs)
    {
        auto job = dynamic_cast<const TfrBlockUpdater::Job*>(j.updatejob.get ());

        auto chunk = job->chunk;
#else
    for (auto& sp : source2pbo)
    {
        auto chunk = sp.first;
#endif
        bool transpose = chunk->order == Tfr::Chunk::Order_column_major;
        int data_width  = transpose ? chunk->nScales ()  : chunk->nSamples ();
        int data_height = transpose ? chunk->nSamples () : chunk->nScales ();
        int w = std::min(gl_max_texture_size(), (int)spo2g(data_width-1));
        int h = std::min(gl_max_texture_size(), (int)spo2g(data_height-1));
        int tw = w, th = h;
        if (data_height!=h)
            tw *= int_div_ceil (data_height,h-1);
        if (data_width!=w)
            th *= int_div_ceil (data_width,w-1);
        EXCEPTION_ASSERT_LESS_OR_EQUAL(tw, gl_max_texture_size());
        EXCEPTION_ASSERT_LESS_OR_EQUAL(th, gl_max_texture_size());

        // Resize texture pool if it's too small or way too big.
        if (!(tw <= p->texturePool.width () && p->texturePool.width () <= 4*tw &&
              th <= p->texturePool.height () && p->texturePool.height () <= 4*th))
        {
            p->texturePool = TexturePool {
                tw,
                th,
                texture_storage ()
            };
        }

#ifndef USE_PBO
        pbo2texture[chunk].reset(new Pbo2Texture(p->shaders,
                                              p->texturePool.get1 (),
                                              chunk,
                                              job->p,
                                              job->type == TfrBlockUpdater::Job::Data_F32));
#else
        pbo2texture[chunk].reset(new Pbo2Texture(p->shaders,
                                            p->texturePool.get1 (),
                                            chunk,
                                            sp.second->getPboWhenReady(),
                                            sp.second->f32()));
#endif
    }

#ifdef PAINT_BLOCKS_FROM_UPDATE_THREAD
    // do flush if separate render thread
    glFlush();
#endif

    // Draw to each block
    for (const UpdateQueue::Job& j : myjobs)
    {
        auto job = dynamic_cast<const TfrBlockUpdater::Job*>(j.updatejob.get ());

        for (const pBlock& block : j.intersecting_blocks)
        {
            // Begin transfer of vbo data to gpu
            const auto& vp = block->visualization_params();
            Texture2Fbo::Params p(job->chunk,
                                  block->getOverlappingRegion (),
                                  vp->display_scale (),
                                  block->block_layout ());

            std::shared_ptr<Texture2Fbo> t2f( new Texture2Fbo(p, job->normalization_factor));

            block->updater ()->queueUpdate (block,
                                            [
                                                shader = pbo2texture[job->chunk],
                                                t2f,
                                                amplitude_axis = vp->amplitude_axis ()
                                            ]
                                            (const glProjection& M)
                                            {
                                                int vertex_attrib, tex_attrib;
                                                shader->map(
                                                            t2f->normalization_factor(),
                                                            amplitude_axis,
                                                            M, vertex_attrib, tex_attrib);

                                                t2f->draw(vertex_attrib, tex_attrib);
                                                return true;
                                            });

#ifdef PAINT_BLOCKS_FROM_UPDATE_THREAD
            block->updater ()->processUpdates (false);
#endif
        }
    }

#ifdef USE_PBO
    source2pbo.clear ();
#endif

    for (UpdateQueue::Job& j : myjobs)
        j.promise.set_value ();
}

} // namespace OpenGL
} // namespace Update
} // namespace Heightmap


namespace Heightmap {
namespace Update {
namespace OpenGL {

void BlockUpdater::
        test()
{
    // It should update blocks with chunk data
    {

    }
}

} // namespace OpenGL
} // namespace Update
} // namespace Heightmap
