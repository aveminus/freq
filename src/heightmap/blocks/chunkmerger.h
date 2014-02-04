#ifndef HEIGHTMAP_BLOCKS_CHUNKMERGER_H
#define HEIGHTMAP_BLOCKS_CHUNKMERGER_H

#include "volatileptr.h"
#include "heightmap/chunkblockfilter.h"
#include "tfr/chunkfilter.h"

#include <list>

namespace Heightmap {
namespace Blocks {

/**
 * @brief The ChunkMerger class should merge chunks into blocks.
 */
class ChunkMerger: public VolatilePtr<ChunkMerger>
{
public:
    ChunkMerger();

    void addChunk(
            MergeChunk::Ptr mergechunk,
            Tfr::ChunkAndInverse pchunk,
            std::vector<pBlock> intersecting_blocks ) volatile;

    void processChunks() volatile;

private:
    struct Job {
        MergeChunk::Ptr merge_chunk;
        Tfr::ChunkAndInverse pchunk;
        std::vector<pBlock> intersecting_blocks;
    };

    std::list<Job> jobs;

    static void processJob(Job& j);
public:
    static void test();
};

} // namespace Blocks
} // namespace Heightmap

#endif // HEIGHTMAP_BLOCKS_CHUNKMERGER_H
