#pragma once

#include "world/chunk/ChunkManager.h"
#include "world/registry/BlockIdTable.h"
#include "world/worldgen/WorldGen.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace world
{
    /**
     * Asynchronous 3D chunk streamer.
     *
     * The old implementation generated the whole incoming slab synchronously
     * inside the frame update. At radius 3, a one-chunk move can require 49 new
     * chunks, which produced visible flight hitches. This version keeps all
     * expensive WorldGen work on one background coordinator thread; WorldGen
     * itself owns its persistent field worker pool.
     *
     * Completed chunks are materialized on the main thread in a small per-frame
     * budget. ChunkManager and renderer callbacks therefore remain single-
     * threaded while frame time is decoupled from generation latency.
     */
    class ChunkStreamingManager
    {
    public:
        ChunkStreamingManager( worldgen::WorldGen &gen,
                               ChunkManager &chunks,
                               std::int64_t viewRadius,
                               std::size_t commitsPerUpdate = 4u );
        ~ChunkStreamingManager();

        ChunkStreamingManager( const ChunkStreamingManager & ) = delete;
        ChunkStreamingManager &operator=( const ChunkStreamingManager & ) = delete;

        /** Queue/re-plan streaming and commit a bounded number of ready chunks. */
        void update( const BlockAddress &playerBlock );

        /**
         * Blocks until the current plan is fully generated and committed.
         * Intended for deterministic tests/tools, not the real-time frame loop.
         */
        void flush();

        std::int64_t viewRadius() const { return mViewRadius; }
        std::size_t loadedChunks() const { return mChunks.chunkCount(); }
        std::size_t generatedCount() const { return mGenerated; }
        std::size_t evictedCount() const { return mEvicted; }

        std::size_t queuedCount() const;
        std::size_t readyCount() const;

    private:
        struct Job
        {
            ChunkAddress coord{};
            std::uint64_t epoch = 0u;
        };

        struct CompletedChunk
        {
            ChunkAddress coord{};
            std::uint64_t epoch = 0u;
            std::vector<std::uint16_t> blocks;
            std::uint32_t nonAirCount = 0u;
        };

        void workerLoop();
        void replan( const ChunkAddress &center );
        void commitCompleted( std::size_t budget );
        void evictOutside( const ChunkAddress &center );
        bool insideCurrentRadius( const ChunkAddress &coord ) const;
        void rethrowWorkerError();

        worldgen::WorldGen &mGen;
        ChunkManager &mChunks;

        std::int64_t mViewRadius;
        std::size_t mCommitsPerUpdate;
        ChunkAddress mLastCenter{};
        bool mHasPlan = false;

        std::size_t mGenerated = 0;
        std::size_t mEvicted = 0;

        mutable std::mutex mWorkMutex;
        std::condition_variable mWorkCv;
        std::condition_variable mCompletionCv;
        std::condition_variable mReadySpaceCv;
        std::deque<Job> mQueue;
        std::deque<CompletedChunk> mCompleted;
        std::deque<std::vector<std::uint16_t>> mFreeBuffers;
        std::size_t mInFlight = 0u;
        std::uint64_t mPlanEpoch = 0u;
        std::exception_ptr mWorkerError;
        bool mStopping = false;
        std::jthread mWorker;

        // Bound generated-but-not-yet-materialized memory and latency.
        static constexpr std::size_t MAX_READY_CHUNKS = 16u;
    };
} // namespace world
