#include "chunkmergerthread.h"

#include "tasktimer.h"
#include "timer.h"
#include "tools/applicationerrorlogcontroller.h"
#include "tfr/chunk.h"

#include <QGLWidget>

//#define INFO
#define INFO if(0)

namespace Heightmap {
namespace Blocks {

ChunkMergerThread::
        ChunkMergerThread(QGLWidget*shared_gl_context)
    :
      jobs(new std::queue<Job>),
      shared_gl_context(shared_gl_context)
{
    // Check for clean exit
    connect(this, SIGNAL(finished()), SLOT(threadFinished()));

    // Start the worker thread as a background thread
    start (LowPriority);
}


ChunkMergerThread::
        ~ChunkMergerThread()
{
    TaskInfo ti("~ChunkMergerThread");

    bool was_idle = isEmpty ();
    requestInterruption ();
    clear ();
    semaphore.release (1);

    if (!was_idle)
      {
        TaskTimer ti("Waiting");
        QThread::wait ();
      }

    QThread::wait ();
}


void ChunkMergerThread::
        clear()
{
    INFO TaskTimer ti("ChunkMergerThread::clear");

    auto jobs = this->jobs.write ();

    while (!jobs->empty ())
        jobs->pop ();
}


void ChunkMergerThread::
        addChunk( MergeChunk::ptr merge_chunk,
                  Tfr::ChunkAndInverse chunk,
                  std::vector<pBlock> intersecting_blocks )
{
    if (intersecting_blocks.empty ())
    {
        TaskInfo(boost::format("Discarding chunk since there are no longer any intersecting_blocks with %s")
                 % chunk.chunk->getCoveredInterval());
        return;
    }

    EXCEPTION_ASSERT( merge_chunk );
    EXCEPTION_ASSERT( chunk.chunk );

    Job j;
    j.merge_chunk = merge_chunk;
    j.chunk = chunk;
    j.intersecting_blocks = intersecting_blocks;

    if (!isInterruptionRequested ())
        jobs->push (j);

    semaphore.release (1);
}


bool ChunkMergerThread::
        processChunks(float timeout)
{
    if (0 <= timeout)
      {
        // return immediately
        return isEmpty ();
      }

    // Requested wait until done
    return wait(timeout);
}


bool ChunkMergerThread::
        wait(float timeout)
{
    Timer T;

    if (timeout < 0)
        timeout = FLT_MAX;

    bool empty;
    while (!(empty = isEmpty ()) && T.elapsed () < timeout && isRunning ())
      {
        QThread::wait (5); // Sleep 5 ms
      }

    return empty;
}


void ChunkMergerThread::
        threadFinished()
{
    TaskInfo("ChunkMergerThread::threadFinished");

    try {
        EXCEPTION_ASSERTX(isInterruptionRequested (), "Thread quit unexpectedly");
        EXCEPTION_ASSERTX(isEmpty(), "Thread quit with jobs left");
    } catch (...) {
        Tools::ApplicationErrorLogController::registerException (boost::current_exception());
    }
}


bool ChunkMergerThread::
        isEmpty() const
{
    return jobs.read ()->empty();
}


void ChunkMergerThread::
        run()
{
    try
      {
        QGLWidget w(0, shared_gl_context);
        w.makeCurrent ();

        while (!isInterruptionRequested ())
          {
            while (semaphore.tryAcquire ()) {}

            while (true)
              {
                Job job;
                  {
                    TaskTimer tt("read job");
                    auto jobsr = jobs.read ();
                    if (jobsr->empty ())
                        break;
                    job = jobsr->front ();
                  }

                processJob (job);

                  {
                    // Want processChunks(-1) and self->isEmpty () to return false until
                    // the job has finished processing.

                    auto jobsw = jobs.write ();
                    // Both 'clear' and 'addChunk' may have been called in between, so only
                    // pop the queue if the first job is still the same.
                    if (!jobsw->empty() && job.chunk.chunk == jobsw->front().chunk.chunk)
                        jobsw->pop ();

                    // Release OpenGL resources before releasing the memory held by chunk
                    job.merge_chunk = MergeChunk::ptr ();
                  }
              }

            // Make sure any texture upload is complete
//            {
//                INFO TaskTimer tt("glFinish");
//                glFinish ();
//            }

            semaphore.acquire ();
          }
      }
    catch (...)
      {
        Tools::ApplicationErrorLogController::registerException (boost::current_exception());
      }
}


void ChunkMergerThread::
        processJob(Job& j)
{
    TaskTimer tt0("processJob");
    // TODO refactor to do one thing at a time
    // 1. prepare to draw from chunks (i.e copy to OpenGL texture and create vertex buffers)
    // 1.1. Same chunk_scale and display_scale for all chunks and all blocks
    // 2. For each block.
    // 2.1. prepare to draw into block
    // 2.2. draw all chunks
    // 2.3. update whatever needs to be updated
    // And rename all these "chunk block merger to merge block chunk merge"

    std::vector<IChunkToBlock::ptr> chunk_to_blocks;
    {
//        TaskTimer tt("creating chunk to blocks");
        chunk_to_blocks = j.merge_chunk->createChunkToBlock( j.chunk );
    }

    {
//        TaskTimer tt("init");
        for (IChunkToBlock::ptr chunk_to_block : chunk_to_blocks)
            chunk_to_block->init ();
    }
    {
//        TaskTimer tt("prepareTransfer");
        for (IChunkToBlock::ptr chunk_to_block : chunk_to_blocks)
            chunk_to_block->prepareTransfer ();
    }
    // j.intersecting_blocks is non-empty from addChunk
    pBlock example_block = j.intersecting_blocks.front ();
    BlockLayout bl = example_block->block_layout ();
    Tfr::FreqAxis display_scale = example_block->visualization_params ()->display_scale ();
    AmplitudeAxis amplitude_axis = example_block->visualization_params ()->amplitude_axis ();

    for (IChunkToBlock::ptr chunk_to_block : chunk_to_blocks)
        chunk_to_block->prepareMerge (amplitude_axis, display_scale, bl);

//    TaskTimer tt(boost::format("processing job %s") % j.chunk.chunk->getCoveredInterval ());
    for (IChunkToBlock::ptr chunk_to_block : chunk_to_blocks)
      {
        for (pBlock block : j.intersecting_blocks)
          {
            if (!block->frame_number_last_used)
                continue;

            INFO TaskTimer tt(boost::format("block %s") % block->getRegion ());
            chunk_to_block->mergeChunk (block);
          }
      }
}


} // namespace Blocks
} // namespace Heightmap
