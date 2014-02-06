#ifndef HEIGHTMAP_BLOCKS_CHUNKMERGER_H
#define HEIGHTMAP_BLOCKS_CHUNKMERGER_H

#include "volatileptr.h"
#include "heightmap/chunkblockfilter.h"
#include "tfr/chunkfilter.h"

#include <queue>

namespace Heightmap {
namespace Blocks {

/**
 * @brief The ChunkMerger class should merge chunks into blocks.
 */
class ChunkMerger: public VolatilePtr<ChunkMerger>
{
public:
    ChunkMerger();

    void clear();
    void addChunk( MergeChunk::Ptr merge_chunk,
                   Tfr::ChunkAndInverse chunk,
                   std::vector<pBlock> intersecting_blocks );

    /**
     * @brief processChunks
     * @param timeout
     * @return true if finished within timeout.
     */
    bool processChunks(float timeout) volatile;

private:
    struct Job {
        MergeChunk::Ptr merge_chunk;
        Tfr::ChunkAndInverse chunk;
        std::vector<pBlock> intersecting_blocks;
    };

    std::queue<Job> jobs;

    static void processJob(Job& j);
public:
    static void test();
};

} // namespace Blocks
} // namespace Heightmap

#endif // HEIGHTMAP_BLOCKS_CHUNKMERGER_H
