#include "world/worldgen/WorldGen.h"

#include "world/worldgen/LuaFieldEvaluator.h"
#include "world/worldgen/MappedOpenSimplexNoise.h"
#include "world/worldgen/NoiseSeed.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace worldgen
{
    namespace
    {
        constexpr std::int64_t EDGE = world::BLOCKS_PER_CHUNK_EDGE;
        constexpr std::size_t NO_FIELD = std::numeric_limits<std::size_t>::max();

        std::size_t denseIndex( std::int64_t lx, std::int64_t ly, std::int64_t lz )
        {
            return static_cast<std::size_t>( ( lx * EDGE + ly ) * EDGE + lz );
        }

        std::int64_t floorToBlock( double value, const std::string &fieldId )
        {
            if( !std::isfinite( value ) )
                throw std::runtime_error( "worldgen field '" + fieldId + "' returned non-finite value" );
            const double floored = std::floor( value );
            constexpr double I64_MIN = -9223372036854775808.0;
            constexpr double I64_MAX_EXCLUSIVE = 9223372036854775808.0;
            if( floored < I64_MIN || floored >= I64_MAX_EXCLUSIVE )
                throw std::overflow_error( "worldgen scalar height exceeds int64" );
            return static_cast<std::int64_t>( floored );
        }

        bool matchesCondition( double value, const FieldCondition &condition )
        {
            switch( condition.op )
            {
            case CompareOp::Always: return true;
            case CompareOp::Greater: return value > condition.value;
            case CompareOp::GreaterEqual: return value >= condition.value;
            case CompareOp::Less: return value < condition.value;
            case CompareOp::LessEqual: return value <= condition.value;
            case CompareOp::Between: return value >= condition.value && value <= condition.maxValue;
            }
            return false;
        }

        std::uint64_t mix64( std::uint64_t x ) noexcept
        {
            x += 0x9E3779B97F4A7C15ULL;
            x = ( x ^ ( x >> 30u ) ) * 0xBF58476D1CE4E5B9ULL;
            x = ( x ^ ( x >> 27u ) ) * 0x94D049BB133111EBULL;
            return x ^ ( x >> 31u );
        }

        double unitRandom( std::uint64_t value ) noexcept
        {
            return static_cast<double>( value >> 11u ) * ( 1.0 / 9007199254740992.0 );
        }

        std::uint64_t stableStringSalt( std::string_view text ) noexcept
        {
            std::uint64_t hash = UINT64_C( 1469598103934665603 );
            for( const unsigned char c : text )
            {
                hash ^= static_cast<std::uint64_t>( c );
                hash *= UINT64_C( 1099511628211 );
            }
            return hash;
        }

        std::uint32_t resolvedWorkerCount( std::uint32_t configured, std::size_t jobs )
        {
            if( jobs == 0 ) return 1u;

            std::uint32_t workers = configured;
            if( workers == 0 )
            {
                workers = std::thread::hardware_concurrency();
                if( workers == 0 ) workers = 1u;
                // Worldgen runs behind the real-time renderer. Leaving one
                // logical CPU free reduces scheduler contention and, on the
                // benchmark host, was faster than saturating every CPU.
                if( workers > 1u ) --workers;
            }

            // Keep the comparison in size_t. Casting an arbitrarily large job
            // count down to uint32_t before min() could wrap and accidentally
            // reduce the worker count to an unrelated value.
            const std::size_t bounded =
                std::min<std::size_t>( static_cast<std::size_t>( workers ), jobs );
            return static_cast<std::uint32_t>( std::max<std::size_t>( 1u, bounded ) );
        }

        class BatchExecutor
        {
        public:
            explicit BatchExecutor( std::uint32_t workerCount )
            {
                if( workerCount <= 1u ) return;
                mThreads.reserve( workerCount );
                for( std::uint32_t i = 0; i < workerCount; ++i )
                    mThreads.emplace_back( [this]() { workerLoop(); } );
            }

            ~BatchExecutor()
            {
                {
                    std::lock_guard lock( mMutex );
                    mStopping = true;
                    ++mEpoch;
                }
                mWorkCv.notify_all();
                mThreads.clear(); // jthread joins
            }

            BatchExecutor( const BatchExecutor & ) = delete;
            BatchExecutor &operator=( const BatchExecutor & ) = delete;

            template <typename Fn>
            void run( std::size_t jobs, Fn &&fn )
            {
                if( jobs == 0 ) return;
                if( mThreads.empty() )
                {
                    for( std::size_t i = 0; i < jobs; ++i ) fn( i );
                    return;
                }

                // A WorldGen instance may be queried from more than one
                // caller. Serialize batches while the workers execute the
                // individual jobs in parallel.
                std::unique_lock batchLock( mBatchMutex );
                {
                    std::lock_guard lock( mMutex );
                    mTask = std::forward<Fn>( fn );
                    mJobs = jobs;
                    mNext.store( 0u, std::memory_order_relaxed );
                    mFailed.store( false, std::memory_order_relaxed );
                    mFirstError = nullptr;
                    mFinishedWorkers = 0u;
                    ++mEpoch;
                }
                mWorkCv.notify_all();

                std::unique_lock lock( mMutex );
                mDoneCv.wait( lock, [&]() {
                    return mFinishedWorkers == mThreads.size();
                } );
                const std::exception_ptr error = mFirstError;
                mTask = {};
                lock.unlock();
                if( error ) std::rethrow_exception( error );
            }
