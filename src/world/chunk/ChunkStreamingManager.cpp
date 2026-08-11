#include "world/chunk/ChunkStreamingManager.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace world
{
    namespace
    {
        std::int64_t validatedViewRadius( std::int64_t viewRadius )
        {
            if( viewRadius < 0 ) return 0;
            constexpr std::int64_t kMaxDiameterSafeRadius =
                ( std::numeric_limits<std::int64_t>::max() - 1 ) / 2;
            if( viewRadius > kMaxDiameterSafeRadius )
                throw std::invalid_argument(
                    "ChunkStreamingManager view radius is too large for a signed diameter" );
            return viewRadius;
        }

        void reserveCubeIfRepresentable( std::vector<ChunkAddress> &items, std::int64_t diameter )
        {
            if( diameter <= 0 ) return;
            const std::size_t max = std::numeric_limits<std::size_t>::max();
            const std::uint64_t d64 = static_cast<std::uint64_t>( diameter );
            if( d64 > static_cast<std::uint64_t>( max ) ) return;
            const std::size_t d = static_cast<std::size_t>( d64 );
            if( d != 0u && d > max / d ) return;
            const std::size_t square = d * d;
            if( square != 0u && d > max / square ) return;
            items.reserve( square * d );
        }
    }

    ChunkStreamingManager::ChunkStreamingManager( worldgen::WorldGen &gen,
                                                   ChunkManager &chunks,
                                                   std::int64_t viewRadius,
                                                   std::size_t commitsPerUpdate )
        : mGen( gen ),
          mChunks( chunks ),
          mViewRadius( validatedViewRadius( viewRadius ) ),
          mCommitsPerUpdate( std::max<std::size_t>( 1u, commitsPerUpdate ) ),
          mWorker( [this]() { workerLoop(); } )
    {
    }

    ChunkStreamingManager::~ChunkStreamingManager()
    {
        {
            std::lock_guard lock( mWorkMutex );
            mStopping = true;
            mQueue.clear();
        }
        mWorkCv.notify_all();
        mReadySpaceCv.notify_all();
        mCompletionCv.notify_all();
        if( mWorker.joinable() ) mWorker.join();
    }

    void ChunkStreamingManager::rethrowWorkerError()
    {
        std::exception_ptr error;
        {
            std::lock_guard lock( mWorkMutex );
            error = mWorkerError;
        }
        if( error ) std::rethrow_exception( error );
    }

    bool ChunkStreamingManager::insideCurrentRadius( const ChunkAddress &coord ) const
    {
        return mHasPlan && chunkWithinChebyshev( coord, mLastCenter, mViewRadius );
    }

    void ChunkStreamingManager::update( const BlockAddress &playerBlock )
    {
        rethrowWorkerError();

        const ChunkAddress center = playerBlock.chunk;
        if( !mHasPlan || center != mLastCenter )
        {
            mLastCenter = center;
            mHasPlan = true;

            // Keep memory bounded immediately, but never generate the incoming
            // slab synchronously in this frame.
            evictOutside( center );
            replan( center );
        }

        commitCompleted( mCommitsPerUpdate );
        rethrowWorkerError();
    }

    void ChunkStreamingManager::replan( const ChunkAddress &center )
    {
        std::uint64_t epoch = 0u;
        {
            std::lock_guard lock( mWorkMutex );
            epoch = ++mPlanEpoch;
            mQueue.clear();

            // Results for the old center are no longer useful. Recycle their
            // buffers instead of materializing chunks that may be evicted in
            // the same frame.
            while( !mCompleted.empty() )
            {
                mFreeBuffers.push_back( std::move( mCompleted.front().blocks ) );
                mCompleted.pop_front();
            }
        }
        mReadySpaceCv.notify_all();

        std::vector<ChunkAddress> missing;
        // Constructor validation guarantees this signed expression cannot overflow.
        const std::int64_t diameter = mViewRadius * 2 + 1;
        // Reserve only when the cube volume itself is representable in size_t;
        // checked multiplication avoids unsigned wrap in the capacity estimate.
        reserveCubeIfRepresentable( missing, diameter );

        for( std::int64_t dx = -mViewRadius; dx <= mViewRadius; ++dx )
            for( std::int64_t dy = -mViewRadius; dy <= mViewRadius; ++dy )
                for( std::int64_t dz = -mViewRadius; dz <= mViewRadius; ++dz )
                {
                    ChunkAddress cc;
                    if( !tryOffsetChunk( center, dx, dy, dz, cc ) ) continue;
                    if( !mChunks.chunkAt( cc ) ) missing.push_back( cc );
                }

        // Center/near chunks first. Chebyshev distance matches the cubic view
        // volume; Manhattan is a deterministic tie-breaker before coordinates.
        std::sort( missing.begin(), missing.end(), [&]( const ChunkAddress &a, const ChunkAddress &b ) {
            const auto key = [&]( const ChunkAddress &c ) {
                RelativeI64 d{};
                if( !chunkDeltaWithin( c, center, mViewRadius, d ) )
                    return std::tuple{ std::int64_t{999999}, std::int64_t{999999}, c };
                const std::int64_t ax = d.x < 0 ? -d.x : d.x;
                const std::int64_t ay = d.y < 0 ? -d.y : d.y;
                const std::int64_t az = d.z < 0 ? -d.z : d.z;
                return std::tuple{ std::max( { ax, ay, az } ), ax + ay + az, c };
            };
            return key( a ) < key( b );
        } );

        {
            std::lock_guard lock( mWorkMutex );
            // A second replan cannot happen concurrently on the main thread,
            // but keep the epoch check explicit for future callers.
            if( epoch != mPlanEpoch ) return;
            for( const ChunkAddress &coord : missing )
                mQueue.push_back( { coord, epoch } );
        }
        mWorkCv.notify_one();
        mCompletionCv.notify_all();
    }

    void ChunkStreamingManager::workerLoop()
    {
        for( ;; )
        {
            Job job;
            std::vector<std::uint16_t> buffer;
            {
                std::unique_lock lock( mWorkMutex );
                mWorkCv.wait( lock, [&]() { return mStopping || !mQueue.empty(); } );
                if( mStopping ) return;

                job = mQueue.front();
                mQueue.pop_front();
                ++mInFlight;

                if( !mFreeBuffers.empty() )
                {
                    buffer = std::move( mFreeBuffers.front() );
                    mFreeBuffers.pop_front();
                }
            }

            if( buffer.size() != static_cast<std::size_t>( world::chunkVolume() ) )
                buffer.assign( static_cast<std::size_t>( world::chunkVolume() ), 0u );

            try
            {
                const std::uint32_t nonAir = mGen.generateChunkIds( job.coord, buffer );

                std::unique_lock lock( mWorkMutex );
                --mInFlight;

                // Drop stale work immediately after a camera replan.
                if( job.epoch != mPlanEpoch )
                {
                    mFreeBuffers.push_back( std::move( buffer ) );
                    mCompletionCv.notify_all();
                    continue;
                }

                mReadySpaceCv.wait( lock, [&]() {
                    return mStopping || job.epoch != mPlanEpoch ||
                           mCompleted.size() < MAX_READY_CHUNKS;
                } );
                if( mStopping ) return;
                if( job.epoch != mPlanEpoch )
                {
                    mFreeBuffers.push_back( std::move( buffer ) );
                    mCompletionCv.notify_all();
                    continue;
                }

                mCompleted.push_back( { job.coord, job.epoch, std::move( buffer ), nonAir } );
                lock.unlock();
                mCompletionCv.notify_all();
            }
            catch( ... )
            {
                std::lock_guard lock( mWorkMutex );
                --mInFlight;
                if( !mWorkerError ) mWorkerError = std::current_exception();
                mQueue.clear();
                mCompletionCv.notify_all();
            }
        }
    }

    void ChunkStreamingManager::commitCompleted( std::size_t budget )
    {
        std::deque<CompletedChunk> ready;
        std::uint64_t currentEpoch = 0u;
        {
            std::lock_guard lock( mWorkMutex );
            currentEpoch = mPlanEpoch;
            while( budget > 0u && !mCompleted.empty() )
            {
                ready.push_back( std::move( mCompleted.front() ) );
                mCompleted.pop_front();
                --budget;
            }
        }
        mReadySpaceCv.notify_all();

        for( CompletedChunk &completed : ready )
        {
            if( completed.epoch == currentEpoch && insideCurrentRadius( completed.coord ) &&
                !mChunks.chunkAt( completed.coord ) )
            {
                Chunk *target = mChunks.loadChunk( completed.coord );
                target->assignBlocks( completed.blocks, completed.nonAirCount );
                ++mGenerated;
            }

            std::lock_guard lock( mWorkMutex );
            mFreeBuffers.push_back( std::move( completed.blocks ) );
        }
        mCompletionCv.notify_all();
    }

    void ChunkStreamingManager::flush()
    {
        for( ;; )
        {
            rethrowWorkerError();
            commitCompleted( std::numeric_limits<std::size_t>::max() );

            std::unique_lock lock( mWorkMutex );
            if( mQueue.empty() && mInFlight == 0u && mCompleted.empty() )
                break;
            mCompletionCv.wait( lock, [&]() {
                return mWorkerError || !mCompleted.empty() ||
                       ( mQueue.empty() && mInFlight == 0u );
            } );
        }
        rethrowWorkerError();
    }

    std::size_t ChunkStreamingManager::queuedCount() const
    {
        std::lock_guard lock( mWorkMutex );
        return mQueue.size() + mInFlight;
    }

    std::size_t ChunkStreamingManager::readyCount() const
    {
        std::lock_guard lock( mWorkMutex );
        return mCompleted.size();
    }

    void ChunkStreamingManager::evictOutside( const ChunkAddress &center )
    {
        std::vector<ChunkAddress> toUnload;
        mChunks.forEachChunk( [this, &center, &toUnload]( Chunk &chunk ) {
            const ChunkAddress c = chunk.address();
            if( !chunkWithinChebyshev( c, center, mViewRadius ) )
                toUnload.push_back( c );
        } );

        for( const ChunkAddress &c : toUnload )
        {
            if( mChunks.unloadChunk( c ) ) ++mEvicted;
        }
    }
} // namespace world
