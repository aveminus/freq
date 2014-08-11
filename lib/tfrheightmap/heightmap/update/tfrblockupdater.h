#ifndef HEIGHTMAP_UPDATE_TFRBLOCKUPDATER_H
#define HEIGHTMAP_UPDATE_TFRBLOCKUPDATER_H

#include "updatequeue.h"
#include "tfr/chunk.h"

namespace Heightmap {
namespace Update {

class TfrBlockUpdaterPrivate;
class TfrBlockUpdater
{
public:
    class Job: public IUpdateJob {
    public:
        Job(Tfr::pChunk chunk, float normalization_factor, float largest_fs=0);

        Tfr::pChunk chunk;
        enum Data {
            Data_F32,
            Data_F16
        } const type = Data_F16;
        void *p;

        float normalization_factor;

        Signal::Interval getCoveredInterval() const override;

    private:
        Tfr::ChunkData::ptr memory;
    };

    TfrBlockUpdater();
    TfrBlockUpdater(const TfrBlockUpdater&) = delete;
    TfrBlockUpdater& operator=(const TfrBlockUpdater&) = delete;
    ~TfrBlockUpdater();

    void processJobs( std::queue<UpdateQueue::Job>& jobs );

private:
    TfrBlockUpdaterPrivate* p;
};

} // namespace Update
} // namespace Heightmap

#endif // HEIGHTMAP_UPDATE_TFRBLOCKUPDATER_H
