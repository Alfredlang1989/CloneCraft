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

            std::uint32_t workerCount() const
            {
                return mThreads.empty() ? 1u : static_cast<std::uint32_t>( mThreads.size() );
            }

        private:
            void workerLoop()
            {
                std::uint64_t observedEpoch = 0u;
                for( ;; )
                {
                    {
                        std::unique_lock lock( mMutex );
                        mWorkCv.wait( lock, [&]() {
                            return mStopping || mEpoch != observedEpoch;
                        } );
                        if( mStopping ) return;
                        observedEpoch = mEpoch;
                    }

                    while( !mFailed.load( std::memory_order_acquire ) )
                    {
                        const std::size_t index =
                            mNext.fetch_add( 1u, std::memory_order_relaxed );
                        if( index >= mJobs ) break;
                        try
                        {
                            mTask( index );
                        }
                        catch( ... )
                        {
                            {
                                std::lock_guard lock( mMutex );
                                if( !mFirstError ) mFirstError = std::current_exception();
                            }
                            mFailed.store( true, std::memory_order_release );
                            break;
                        }
                    }

                    {
                        std::lock_guard lock( mMutex );
                        ++mFinishedWorkers;
                        if( mFinishedWorkers == mThreads.size() )
                            mDoneCv.notify_one();
                    }
                }
            }

            std::mutex mBatchMutex;
            std::mutex mMutex;
            std::condition_variable mWorkCv;
            std::condition_variable mDoneCv;
            std::vector<std::jthread> mThreads;
            std::function<void( std::size_t )> mTask;
            std::atomic<std::size_t> mNext{ 0u };
            std::atomic<bool> mFailed{ false };
            std::size_t mJobs = 0u;
            std::size_t mFinishedWorkers = 0u;
            std::uint64_t mEpoch = 0u;
            std::exception_ptr mFirstError;
            bool mStopping = false;
        };

        struct RuntimePass
        {
            PassConfig config;
            std::uint16_t blockId = 0;
            std::vector<std::uint16_t> replaceBlockIds;
            std::unordered_set<std::string> replaceTags;
            std::vector<std::uint8_t> replaceMask;
            std::size_t field = NO_FIELD, maskField = NO_FIELD, surfaceField = NO_FIELD,
                        thicknessField = NO_FIELD, bottomField = NO_FIELD;
            std::uint32_t order = 0;
        };

        struct RuntimeBiomeTerrain
        {
            world::BiomeTerrainDef terrain;
            std::size_t maskField = NO_FIELD;
            std::uint64_t salt = 0;
        };

        struct DecorationAnchor
        {
            world::BlockAddress position{};
            std::uint64_t seed = 0;
            double variant = 0.0;
            std::uint16_t supportBlock = std::numeric_limits<std::uint16_t>::max();
        };
        struct RuntimeAnchorCondition { std::size_t field = NO_FIELD; FieldCondition condition; };
        struct RuntimeAnchorSet
        {
            AnchorSetConfig config;
            std::size_t surfaceField = NO_FIELD, densityField = NO_FIELD;
            std::vector<RuntimeAnchorCondition> conditions;
            StructureBounds influenceBounds;
        };
        struct RuntimeDecoration
        {
            DecorationPassConfig config;
            std::size_t anchorSet = NO_FIELD;
            std::uint16_t blockId = 0;
            std::vector<std::uint16_t> paletteIds;
            std::vector<std::uint8_t> replaceMask, supportMask;
            std::unique_ptr<LuaFieldEvaluator> structureEvaluator;
            std::uint32_t order = 0;
        };

        std::uint64_t hashAxis( std::uint64_t h, const world::detail::AxisAddress &a,
                                std::uint64_t tag ) noexcept
        {
            h = mix64( h ^ tag );
            h = mix64( h ^ static_cast<std::uint64_t>( a.sector ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.region ) + 0x11u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.group ) + 0x22u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.chunk ) + 0x33u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.block ) + 0x44u ) );
            return h;
        }

        std::uint64_t hashAnchorCell( std::uint64_t seed, std::uint64_t salt,
                                      const world::detail::AxisAddress &cellX,
                                      const world::detail::AxisAddress &cellZ,
                                      std::uint64_t lane ) noexcept
        {
            std::uint64_t h = mix64( seed ^ mix64( salt ) ^ mix64( lane ) );
            h = hashAxis( h, cellX, 0xA24BAED4963EE407ULL );
            h = hashAxis( h, cellZ, 0x9FB21C651E98DF25ULL );
            return mix64( h );
        }

        world::detail::AxisAddress divideAxis( const world::detail::AxisAddress &value,
                                               std::int64_t divisor,
                                               std::int64_t &remainder )
        {
            world::detail::AxisAddress q{};
            q.sector = world::floorDiv( value.sector, divisor );
            remainder = world::floorMod( value.sector, divisor );
            auto step = [&]( std::int64_t digit, std::int64_t &qd ) {
                const std::int64_t current = remainder * 16 + digit;
                qd = current / divisor;
                remainder = current % divisor;
            };
            step( value.region, q.region ); step( value.group, q.group );
            step( value.chunk, q.chunk ); step( value.block, q.block );
            return q;
        }

        world::detail::AxisAddress originHeightAxis( std::int64_t y )
        {
            return world::blockAxisY( world::fromOriginOffset( 0, y, 0 ) );
        }

        bool localIndexOf( const world::BlockAddress &point, const world::BlockAddress &origin,
                           std::size_t &index ) noexcept
        {
            world::RelativeI64 d{};
            if( !world::blockDeltaWithin( point, origin, EDGE - 1, d ) ||
                d.x < 0 || d.y < 0 || d.z < 0 || d.x >= EDGE || d.y >= EDGE || d.z >= EDGE )
                return false;
            index = denseIndex( d.x, d.y, d.z );
            return true;
        }
    } // namespace
    struct WorldGen::State
    {
        State( const WorldGenConfig &c,
               const world::BlockRegistry &blocks,
               const world::BlockIdTable &idTable,
               const world::BiomeRegistry *biomes )
            : config( c ), table( idTable )
        {
            if( config.fields.empty() )
                throw std::invalid_argument( "WorldGen requires at least one configured field" );
            if( config.passes.empty() )
                throw std::invalid_argument( "WorldGen requires at least one configured pass" );

            fields.reserve( config.fields.size() );
            for( const FieldConfig &field : config.fields )
            {
                if( fieldById.count( field.id ) != 0 )
                    throw std::invalid_argument( "duplicate worldgen field '" + field.id + "'" );
                const std::size_t fieldIndex = fields.size();
                fieldById[field.id] = fieldIndex;
                fields.emplace_back( field, config.seed );
                if( field.dimension == FieldDimension::D2 )
                    field2DIndices.push_back( fieldIndex );
                else
                    field3DIndices.push_back( fieldIndex );
            }

            const auto surfaceIt = fieldById.find( config.surfaceField );
            if( surfaceIt == fieldById.end() )
                throw std::invalid_argument( "unknown worldgen surfaceField '" +
                                             config.surfaceField + "'" );
            surfaceField = surfaceIt->second;
            if( fields[surfaceField].config().dimension != FieldDimension::D2 )
                throw std::invalid_argument( "worldgen surfaceField must be 2D" );

            if( biomes && !biomes->empty() )
            {
                for( const std::string &biomeId : biomes->ids() )
                {
                    const world::BiomeDef &biome = biomes->get( biomeId );
                    RuntimeBiomeTerrain runtime;
                    runtime.terrain = biome.terrain;
                    runtime.salt = stableStringSalt( biome.id );
                    if( biome.terrainMaskField.empty() )
                    {
                        if( fallbackTerrain.has_value() )
                            throw std::invalid_argument(
                                "multiple biomes without terrainMaskField; exactly one fallback terrain is allowed" );
                        fallbackTerrain = runtime;
                        continue;
                    }

                    const auto maskIt = fieldById.find( biome.terrainMaskField );
                    if( maskIt == fieldById.end() )
                        throw std::invalid_argument( "biome '" + biome.id +
                                                     "' references unknown terrainMaskField '" +
                                                     biome.terrainMaskField + "'" );
                    if( fields[maskIt->second].config().dimension != FieldDimension::D2 )
                        throw std::invalid_argument( "biome '" + biome.id +
                                                     "' terrainMaskField must be 2D" );
                    runtime.maskField = maskIt->second;
                    biomeTerrains.push_back( std::move( runtime ) );
                }

                if( !fallbackTerrain.has_value() )
                {
                    RuntimeBiomeTerrain fallback;
                    fallback.terrain = world::BiomeTerrainDef{};
                    fallback.salt = stableStringSalt( "<implicit-default>" );
                    fallbackTerrain = std::move( fallback );
                }
            }

            blockTags.resize( table.size() );
            for( std::size_t i = 0; i < table.size(); ++i )
            {
                const world::BlockDef &def = blocks.get( table.idOf( static_cast<std::uint16_t>( i ) ) );
                blockTags[i].insert( def.tags.begin(), def.tags.end() );
            }

            passes.reserve( config.passes.size() );
            for( std::size_t i = 0; i < config.passes.size(); ++i )
            {
                RuntimePass pass;
                pass.config = config.passes[i];
                pass.order = static_cast<std::uint32_t>( i );
                pass.blockId = table.indexOf( pass.config.blockId );
                for( const std::string &id : pass.config.replaceBlocks )
                    pass.replaceBlockIds.push_back( table.indexOf( id ) );
                pass.replaceTags.insert( pass.config.replaceTags.begin(),
                                         pass.config.replaceTags.end() );

                auto resolve = [&]( const std::string &id, FieldDimension expected,
                                    const char *role ) -> std::size_t {
                    const auto it = fieldById.find( id );
                    if( it == fieldById.end() )
                        throw std::invalid_argument( "pass '" + pass.config.id +
                                                     "' references unknown " + role +
                                                     " field '" + id + "'" );
                    if( fields[it->second].config().dimension != expected )
                        throw std::invalid_argument( "pass '" + pass.config.id + "' " + role +
                                                     " field '" + id + "' has wrong dimension" );
                    return it->second;
                };

                if( !pass.config.maskField.empty() )
                    pass.maskField = resolve( pass.config.maskField, FieldDimension::D2, "mask" );

                switch( pass.config.type )
                {
                case PassType::FillBelow:
                case PassType::Surface:
                    pass.field = resolve( pass.config.field, FieldDimension::D2, "source" );
                    break;
                case PassType::Volume:
                    pass.field = resolve( pass.config.field, FieldDimension::D3, "source" );
                    break;
                case PassType::SurfaceLayer:
                    pass.surfaceField = resolve( pass.config.surfaceField,
                                                 FieldDimension::D2, "surface" );
                    if( !pass.config.thicknessField.empty() )
                        pass.thicknessField = resolve( pass.config.thicknessField,
                                                       FieldDimension::D2, "thickness" );
                    if( !pass.config.bottomField.empty() )
                        pass.bottomField = resolve( pass.config.bottomField,
                                                    FieldDimension::D2, "bottom" );
                    if( pass.bottomField == NO_FIELD && pass.thicknessField == NO_FIELD &&
                        pass.config.thickness < 1 )
                        throw std::invalid_argument( "pass '" + pass.config.id +
                                                     "' thickness must be >= 1" );
                    break;
                }
                pass.replaceMask.assign( table.size(), 0u );
                if( pass.replaceBlockIds.empty() && pass.replaceTags.empty() )
                {
                    std::fill( pass.replaceMask.begin(), pass.replaceMask.end(), 1u );
                }
                else
                {
                    for( const std::uint16_t id : pass.replaceBlockIds )
                        if( id < pass.replaceMask.size() ) pass.replaceMask[id] = 1u;
                    for( std::size_t blockId = 0; blockId < blockTags.size(); ++blockId )
                    {
                        for( const std::string &tag : pass.replaceTags )
                        {
                            if( blockTags[blockId].count( tag ) != 0 )
                            {
                                pass.replaceMask[blockId] = 1u;
                                break;
                            }
                        }
                    }
                }
                if( pass.config.type == PassType::Volume &&
                    !pass.replaceMask.empty() && pass.replaceMask[0] != 0u )
                    volumeCanCreateInAir = true;
                passes.push_back( std::move( pass ) );
            }

            const auto buildStageOrder = [&]( PassStage stage ) {
                std::vector<std::size_t> order;
                order.reserve( passes.size() );
                for( std::size_t i = 0; i < passes.size(); ++i )
                    if( passes[i].config.stage == stage ) order.push_back( i );
                std::stable_sort( order.begin(), order.end(), [&]( std::size_t a, std::size_t b ) {
                    const RuntimePass &pa = passes[a];
                    const RuntimePass &pb = passes[b];
                    if( pa.config.priority != pb.config.priority )
                        return pa.config.priority < pb.config.priority;
                    return pa.order < pb.order;
                } );
                return order;
            };
            terrainMergeOrder = buildStageOrder( PassStage::Terrain );
            addonMergeOrder = buildStageOrder( PassStage::Addon );

            const auto resolve2D = [&]( const std::string &id, const std::string &owner,
                                        const char *role ) -> std::size_t {
                const auto it = fieldById.find( id );
                if( it == fieldById.end() )
                    throw std::invalid_argument( owner + " references unknown " + role +
                                                 " field '" + id + "'" );
                if( fields[it->second].config().dimension != FieldDimension::D2 )
                    throw std::invalid_argument( owner + " " + role + " field '" + id +
                                                 "' must be 2D" );
                return it->second;
            };

            anchorSets.reserve( config.anchorSets.size() );
            for( const AnchorSetConfig &anchorConfig : config.anchorSets )
            {
                if( anchorSetById.count( anchorConfig.id ) != 0u )
                    throw std::invalid_argument( "duplicate decoration anchor set '" +
                                                 anchorConfig.id + "'" );
                RuntimeAnchorSet anchor;
                anchor.config = anchorConfig;
                anchor.surfaceField = resolve2D( anchorConfig.surfaceField,
                                                  "anchor set '" + anchorConfig.id + "'",
                                                  "surface" );
                if( !anchorConfig.densityField.empty() )
                    anchor.densityField = resolve2D( anchorConfig.densityField,
                                                      "anchor set '" + anchorConfig.id + "'",
                                                      "density" );
                for( const AnchorConditionConfig &conditionConfig : anchorConfig.conditions )
                {
                    RuntimeAnchorCondition condition;
                    condition.field = resolve2D( conditionConfig.field,
                                                  "anchor set '" + anchorConfig.id + "'",
                                                  "condition" );
                    condition.condition = conditionConfig.condition;
                    anchor.conditions.push_back( condition );
                }
                anchorSetById.emplace( anchorConfig.id, anchorSets.size() );
                anchorSets.push_back( std::move( anchor ) );
            }

            const auto buildMask = [&]( const std::vector<std::string> &ids,
                                        const std::vector<std::string> &tags,
                                        bool defaultAll ) {
                std::vector<std::uint8_t> mask( table.size(), 0u );
                if( ids.empty() && tags.empty() && defaultAll )
                {
                    std::fill( mask.begin(), mask.end(), 1u );
                    return mask;
                }
                for( const std::string &id : ids )
                {
                    const std::uint16_t runtimeId = table.indexOf( id );
                    if( runtimeId < mask.size() ) mask[runtimeId] = 1u;
                }
                for( std::size_t blockId = 0; blockId < blockTags.size(); ++blockId )
                {
                    for( const std::string &tag : tags )
                    {
                        if( blockTags[blockId].count( tag ) != 0u )
                        {
                            mask[blockId] = 1u;
                            break;
                        }
                    }
                }
                return mask;
            };

            decorations.reserve( config.decorations.size() );
            for( std::size_t i = 0; i < config.decorations.size(); ++i )
            {
                RuntimeDecoration decoration;
                decoration.config = config.decorations[i];
                decoration.order = static_cast<std::uint32_t>( i );
                const auto anchorIt = anchorSetById.find( decoration.config.anchorSet );
                if( anchorIt == anchorSetById.end() )
                    throw std::invalid_argument( "decoration '" + decoration.config.id +
                                                 "' references unknown anchor set '" +
                                                 decoration.config.anchorSet + "'" );
                decoration.anchorSet = anchorIt->second;

                if( decoration.config.type == DecorationType::Structure )
                {
                    for( const std::string &id : decoration.config.palette )
                        decoration.paletteIds.push_back( table.indexOf( id ) );
                    FieldConfig scriptConfig;
                    scriptConfig.id = "decoration:" + decoration.config.id;
                    scriptConfig.dimension = FieldDimension::D3;
                    scriptConfig.scriptPath = decoration.config.scriptPath;
                    scriptConfig.functionName = decoration.config.functionName;
                    scriptConfig.salt = decoration.config.salt;
                    decoration.structureEvaluator =
                        std::make_unique<LuaFieldEvaluator>( scriptConfig, config.seed );
                }
                else
                {
                    decoration.blockId = table.indexOf( decoration.config.blockId );
                }

                decoration.replaceMask = buildMask( decoration.config.replaceBlocks,
                                                     decoration.config.replaceTags, true );
                decoration.supportMask = buildMask( decoration.config.supportBlocks,
                                                     decoration.config.supportTags, true );

                StructureBounds influence;
                if( decoration.config.type == DecorationType::Structure )
                    influence = decoration.config.bounds;
                else if( decoration.config.type == DecorationType::Column )
                    influence.maxY = decoration.config.maxHeight - 1;
                RuntimeAnchorSet &anchor = anchorSets[decoration.anchorSet];
                anchor.influenceBounds.minX = std::min( anchor.influenceBounds.minX, influence.minX );
                anchor.influenceBounds.maxX = std::max( anchor.influenceBounds.maxX, influence.maxX );
                anchor.influenceBounds.minY = std::min( anchor.influenceBounds.minY, influence.minY );
                anchor.influenceBounds.maxY = std::max( anchor.influenceBounds.maxY, influence.maxY );
                anchor.influenceBounds.minZ = std::min( anchor.influenceBounds.minZ, influence.minZ );
                anchor.influenceBounds.maxZ = std::max( anchor.influenceBounds.maxZ, influence.maxZ );
                decorations.push_back( std::move( decoration ) );
            }

            decorationMergeOrder.resize( decorations.size() );
            for( std::size_t i = 0; i < decorationMergeOrder.size(); ++i )
                decorationMergeOrder[i] = i;
            std::stable_sort( decorationMergeOrder.begin(), decorationMergeOrder.end(),
                              [&]( std::size_t a, std::size_t b ) {
                const RuntimeDecoration &da = decorations[a];
                const RuntimeDecoration &db = decorations[b];
                if( da.config.priority != db.config.priority )
                    return da.config.priority < db.config.priority;
                return da.order < db.order;
            } );

            const std::size_t maxJobs = std::max( { field2DIndices.size(), field3DIndices.size(),
                                                   passes.size(), anchorSets.size(), decorations.size(),
                                                   std::size_t( 1 ) } );
            executor = std::make_unique<BatchExecutor>(
                resolvedWorkerCount( config.workerThreads, maxJobs ) );
        }

        WorldGenConfig config;
        world::BlockIdTable table;
        std::vector<LuaFieldEvaluator> fields;
        std::unordered_map<std::string, std::size_t> fieldById;
        std::vector<std::size_t> field2DIndices, field3DIndices;
        std::size_t surfaceField = NO_FIELD;
        std::vector<RuntimeBiomeTerrain> biomeTerrains;
        std::optional<RuntimeBiomeTerrain> fallbackTerrain;
        std::vector<RuntimePass> passes;
        std::vector<std::size_t> terrainMergeOrder;
        std::vector<std::size_t> addonMergeOrder;
        std::vector<std::unordered_set<std::string>> blockTags;
        bool volumeCanCreateInAir = false;
        std::vector<RuntimeAnchorSet> anchorSets;
        std::unordered_map<std::string, std::size_t> anchorSetById;
        std::vector<RuntimeDecoration> decorations;
        std::vector<std::size_t> decorationMergeOrder;
        std::unique_ptr<BatchExecutor> executor;

        double terrainProfileHeight( const RuntimeBiomeTerrain &biome, double rawBase,
                                     MappedOpenSimplexNoise &noise ) const
        {
            const world::BiomeTerrainDef &terrain = biome.terrain;
            double height = terrain.heightOffset + rawBase * terrain.heightMultiplier;

            if( terrain.detailMultiplier > 0.0 )
            {
                const double detail = noise.noise2( terrain.detailScale,
                                                    biome.salt ^ UINT64_C( 0xD37A11A5 ) );
                height += detail * terrain.detailAmplitude * terrain.detailMultiplier;
            }

            if( terrain.ridgeAmplitude > 0.0 )
            {
                const double n = noise.noise2( terrain.ridgeScale,
                                                biome.salt ^ UINT64_C( 0xA17E9D63 ) );
                const double ridge = std::pow( std::clamp( 1.0 - std::abs( n ), 0.0, 1.0 ),
                                               terrain.ridgeSharpness );
                height += ridge * terrain.ridgeAmplitude;
            }

            if( terrain.islandAmplitude > 0.0 )
            {
                const double n = 0.5 + 0.5 * noise.noise2(
                    terrain.islandScale, biome.salt ^ UINT64_C( 0x15A1A6D5 ) );
                if( n > terrain.islandThreshold )
                {
                    const double denom = std::max( 1.0e-9, 1.0 - terrain.islandThreshold );
                    const double island = std::pow(
                        std::clamp( ( n - terrain.islandThreshold ) / denom, 0.0, 1.0 ),
                        terrain.islandSharpness );
                    height += island * terrain.islandAmplitude;
                }
            }
            return height;
        }

        template <typename MaskSampler>
        double adjustedSurfaceHeight( double rawBase, const world::BlockAddress &column,
                                      MappedOpenSimplexNoise &noise,
                                      MaskSampler &&sampleMask ) const
        {
            if( !fallbackTerrain.has_value() ) return rawBase;

            noise.setSample( column );
            double weighted = 0.0;
            double explicitWeight = 0.0;
            for( const RuntimeBiomeTerrain &biome : biomeTerrains )
            {
                const double weight = std::clamp( sampleMask( biome.maskField ), 0.0, 1.0 );
                if( weight <= 0.0 ) continue;
                weighted += weight * terrainProfileHeight( biome, rawBase, noise );
                explicitWeight += weight;
            }

            const double fallbackWeight = std::max( 0.0, 1.0 - explicitWeight );
            if( fallbackWeight > 0.0 )
                weighted += fallbackWeight * terrainProfileHeight(
                    *fallbackTerrain, rawBase, noise );

            const double totalWeight = explicitWeight + fallbackWeight;
            return totalWeight > 0.0 ? weighted / totalWeight : rawBase;
        }

        double adjustedSurfaceAt( const world::BlockAddress &column ) const
        {
            const double rawBase = fields[surfaceField].sample2D( column );
            MappedOpenSimplexNoise noise( config.seed,
                deriveNoiseSeed( config.seed, UINT64_C( 0xB10B1E7E22A1D5E5 ) ) );
            return adjustedSurfaceHeight( rawBase, column, noise, [&]( std::size_t maskField ) {
                return fields[maskField].sample2D( column );
            } );
        }

        void applyBiomeTerrain( const world::ChunkAddress &chunk,
                                std::vector<SampledField> &sampled ) const
        {
            if( !fallbackTerrain.has_value() ) return;
            SampledField &surface = sampled[surfaceField];
            MappedOpenSimplexNoise noise( config.seed,
                deriveNoiseSeed( config.seed, UINT64_C( 0xB10B1E7E22A1D5E5 ) ) );
            for( std::int64_t lx = 0; lx < EDGE; ++lx )
                for( std::int64_t lz = 0; lz < EDGE; ++lz )
                {
                    const world::BlockAddress column = world::blockAt( chunk, { lx, 0, lz } );
                    const std::size_t index = static_cast<std::size_t>( lx * EDGE + lz );
                    const double rawBase = surface.values[index];
                    surface.values[index] = adjustedSurfaceHeight(
                        rawBase, column, noise, [&]( std::size_t maskField ) {
                            return sampled[maskField].at2D( lx, lz );
                        } );
                }
        }

        bool canReplace( std::uint16_t current, const RuntimePass &pass ) const
        {
            return current < pass.replaceMask.size() && pass.replaceMask[current] != 0u;
        }

        bool columnEnabled( const RuntimePass &pass, const std::vector<SampledField> &sampled,
                            std::int64_t lx, std::int64_t lz ) const
        {
            return pass.maskField == NO_FIELD ||
                   matchesCondition( sampled[pass.maskField].at2D( lx, lz ), pass.config.maskCondition );
        }

        std::int64_t shiftedSurface( double raw, const std::string &fieldId,
                                     std::int32_t offset ) const
        {
            const std::int64_t base = floorToBlock( raw, fieldId );
            if( ( offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset ) ||
                ( offset < 0 && base < std::numeric_limits<std::int64_t>::min() - offset ) )
                throw std::overflow_error( "worldgen surface scalar offset exceeds int64" );
            return base + static_cast<std::int64_t>( offset );
        }

        struct TerrainSurfaceSample
        {
            world::BlockAddress position{};
            std::uint16_t blockId = 0;
        };

        std::optional<TerrainSurfaceSample> resolvePostprocessSurface(
            const RuntimeAnchorSet &anchorSet, const world::BlockAddress &column ) const
        {
            std::vector<double> cache2D( fields.size(), 0.0 );
            std::vector<std::uint8_t> cached2D( fields.size(), 0u );
            const auto sample2D = [&]( std::size_t index ) -> double {
                if( cached2D[index] == 0u )
                {
                    cache2D[index] = index == surfaceField
                        ? adjustedSurfaceAt( column )
                        : fields[index].sample2D( column );
                    cached2D[index] = 1u;
                }
                return cache2D[index];
            };
            const auto columnEnabledAt = [&]( const RuntimePass &pass ) {
                return pass.maskField == NO_FIELD ||
                    matchesCondition( sample2D( pass.maskField ), pass.config.maskCondition );
            };
            const auto appliesAt = [&]( const RuntimePass &pass, const world::BlockAddress &point ) {
                if( !columnEnabledAt( pass ) ) return false;
                const auto y = world::blockAxisY( point );
                switch( pass.config.type )
                {
                case PassType::FillBelow:
                    return y <= originHeightAxis( floorToBlock( sample2D( pass.field ), fields[pass.field].config().id ) );
                case PassType::Surface:
                    return y == originHeightAxis( shiftedSurface( sample2D( pass.field ),
                        fields[pass.field].config().id, pass.config.surfaceOffset ) );
                case PassType::SurfaceLayer:
                {
                    const std::int64_t topH = shiftedSurface( sample2D( pass.surfaceField ),
                        fields[pass.surfaceField].config().id, pass.config.surfaceOffset );
                    const auto top = originHeightAxis( topH );
                    if( y > top ) return false;
                    if( pass.bottomField != NO_FIELD )
                    {
                        const std::int64_t bottomH = shiftedSurface(
                            sample2D( pass.bottomField ), fields[pass.bottomField].config().id,
                            pass.config.bottomOffset );
                        const auto bottom = originHeightAxis( bottomH );
                        return y >= bottom;
                    }
                    std::int64_t depth = pass.config.thickness;
                    if( pass.thicknessField != NO_FIELD )
                        depth = std::max<std::int64_t>( 0, floorToBlock(
                            sample2D( pass.thicknessField ), fields[pass.thicknessField].config().id ) );
                    if( depth <= 0 ) return false;
                    std::int64_t delta = 0;
                    return world::detail::blockAxisDeltaWithin( y, top, depth - 1, delta );
                }
                case PassType::Volume:
                    return matchesCondition( fields[pass.field].sample3D( point ), pass.config.condition );
                }
                return false;
            };
            const auto blockAt = [&]( const world::BlockAddress &point ) {
                std::uint16_t current = 0u;
                const auto applyStage = [&]( const std::vector<std::size_t> &order ) {
                    for( const std::size_t passIndex : order )
                    {
                        const RuntimePass &pass = passes[passIndex];
                        if( appliesAt( pass, point ) && canReplace( current, pass ) )
                            current = pass.blockId;
                    }
                };
                applyStage( terrainMergeOrder );
                applyStage( addonMergeOrder );
                return current;
            };

            std::int64_t highestTop = floorToBlock( sample2D( anchorSet.surfaceField ),
                                                    fields[anchorSet.surfaceField].config().id );
            for( const RuntimePass &pass : passes )
            {
                if( pass.config.type == PassType::Volume || !columnEnabledAt( pass ) ) continue;
                std::int64_t top = highestTop;
                if( pass.config.type == PassType::FillBelow )
                    top = floorToBlock( sample2D( pass.field ), fields[pass.field].config().id );
                else if( pass.config.type == PassType::Surface )
                    top = shiftedSurface( sample2D( pass.field ), fields[pass.field].config().id,
                                          pass.config.surfaceOffset );
                else if( pass.config.type == PassType::SurfaceLayer )
                {
                    if( pass.bottomField == NO_FIELD )
                    {
                        std::int64_t depth = pass.config.thickness;
                        if( pass.thicknessField != NO_FIELD )
                            depth = std::max<std::int64_t>( 0, floorToBlock(
                                sample2D( pass.thicknessField ), fields[pass.thicknessField].config().id ) );
                        if( depth <= 0 ) continue;
                    }
                    top = shiftedSurface( sample2D( pass.surfaceField ), fields[pass.surfaceField].config().id,
                                          pass.config.surfaceOffset );
                }
                highestTop = std::max( highestTop, top );
            }

            world::BlockAddress point = world::withOriginRelativeY( column, highestTop );
            for( std::int32_t drop = 0; drop <= anchorSet.config.maxSurfaceDrop; ++drop )
            {
                const std::uint16_t id = blockAt( point );
                if( id != 0u ) return TerrainSurfaceSample{ point, id };
                if( drop == anchorSet.config.maxSurfaceDrop ) break;
                world::BlockAddress next;
                if( !world::tryOffsetBlock( point, 0, -1, 0, next ) ) break;
                point = next;
            }
            return std::nullopt;
        }

        std::vector<BlockProposal> buildPassProposals( const RuntimePass &pass,
            const std::vector<SampledField> &sampled, const world::BlockAddress &origin ) const
        {
            std::vector<BlockProposal> proposals;
            const auto emit = [&]( std::int64_t lx, std::int64_t ly, std::int64_t lz ) {
                proposals.push_back( { denseIndex( lx, ly, lz ), pass.blockId,
                                       pass.config.priority, pass.order } );
            };

            if( pass.config.type == PassType::Volume )
            {
                const SampledField &field = sampled[pass.field];
                for( std::int64_t lx=0; lx<EDGE; ++lx ) for( std::int64_t lz=0; lz<EDGE; ++lz )
                {
                    if( !columnEnabled( pass, sampled, lx, lz ) ) continue;
                    for( std::int64_t ly=0; ly<EDGE; ++ly )
                        if( matchesCondition( field.at3D(lx,ly,lz), pass.config.condition ) ) emit(lx,ly,lz);
                }
                return proposals;
            }

            for( std::int64_t lx=0; lx<EDGE; ++lx ) for( std::int64_t lz=0; lz<EDGE; ++lz )
            {
                if( !columnEnabled( pass, sampled, lx, lz ) ) continue;
                std::int64_t topH = 0;
                std::int64_t bottomH = 0;
                std::int64_t depth = 1;
                bool boundedByBottom = false;
                if( pass.config.type == PassType::SurfaceLayer )
                {
                    const SampledField &surface = sampled[pass.surfaceField];
                    topH = shiftedSurface( surface.at2D(lx,lz), surface.id, pass.config.surfaceOffset );
                    if( pass.bottomField != NO_FIELD )
                    {
                        const SampledField &bottom = sampled[pass.bottomField];
                        bottomH = shiftedSurface( bottom.at2D(lx,lz), bottom.id,
                                                  pass.config.bottomOffset );
                        boundedByBottom = true;
                        if( bottomH > topH ) continue;
                    }
                    else
                    {
                        depth = pass.config.thickness;
                        if( pass.thicknessField != NO_FIELD )
                        {
                            const SampledField &t = sampled[pass.thicknessField];
                            depth = std::max<std::int64_t>( 0, floorToBlock( t.at2D(lx,lz), t.id ) );
                        }
                        if( depth <= 0 ) continue;
                    }
                }
                else
                {
                    const SampledField &field = sampled[pass.field];
                    topH = pass.config.type == PassType::Surface
                        ? shiftedSurface( field.at2D(lx,lz), field.id, pass.config.surfaceOffset )
                        : floorToBlock( field.at2D(lx,lz), field.id );
                }
                const auto topY = originHeightAxis( topH );
                for( std::int64_t ly=0; ly<EDGE; ++ly )
                {
                    const world::BlockAddress point = world::blockAt( origin.chunk, { lx, ly, lz } );
                    const auto y = world::blockAxisY( point );
                    bool applies = false;
                    if( pass.config.type == PassType::FillBelow ) applies = y <= topY;
                    else if( pass.config.type == PassType::Surface ) applies = y == topY;
                    else
                    {
                        if( y <= topY )
                        {
                            if( boundedByBottom )
                            {
                                const auto bottomY = originHeightAxis( bottomH );
                                applies = y >= bottomY;
                            }
                            else
                            {
                                std::int64_t d=0;
                                applies = world::detail::blockAxisDeltaWithin( y, topY, depth-1, d );
                            }
                        }
                    }
                    if( applies ) emit(lx,ly,lz);
                }
            }
            return proposals;
        }

        double fieldValueAt( std::size_t fieldIndex, const world::BlockAddress &point,
                             const world::BlockAddress &chunkOrigin,
                             const std::vector<SampledField> &sampled ) const
        {
            world::RelativeI64 d{};
            if( world::blockDeltaWithin( point, chunkOrigin, EDGE-1, d ) &&
                d.x>=0 && d.x<EDGE && d.z>=0 && d.z<EDGE &&
                fieldIndex < sampled.size() && !sampled[fieldIndex].values.empty() )
                return sampled[fieldIndex].at2D( d.x, d.z );
            return fieldIndex == surfaceField ? adjustedSurfaceAt( point )
                                              : fields[fieldIndex].sample2D( point );
        }

        std::vector<DecorationAnchor> buildAnchors( const RuntimeAnchorSet &anchorSet,
            const world::BlockAddress &origin, const std::vector<SampledField> &sampled ) const
        {
            std::vector<DecorationAnchor> anchors;
            const std::int64_t minDx = -static_cast<std::int64_t>( anchorSet.influenceBounds.maxX );
            const std::int64_t maxDx = EDGE - 1 - static_cast<std::int64_t>( anchorSet.influenceBounds.minX );
            const std::int64_t minDz = -static_cast<std::int64_t>( anchorSet.influenceBounds.maxZ );
            const std::int64_t maxDz = EDGE - 1 - static_cast<std::int64_t>( anchorSet.influenceBounds.minZ );
            const std::int64_t spacing = anchorSet.config.spacing;

            for( std::int64_t dx=minDx; dx<=maxDx; ++dx ) for( std::int64_t dz=minDz; dz<=maxDz; ++dz )
            {
                world::BlockAddress column;
                if( !world::tryOffsetBlock( origin, dx, 0, dz, column ) ) continue;
                std::int64_t remX=0, remZ=0;
                const auto cellX = divideAxis( world::blockAxisX(column), spacing, remX );
                const auto cellZ = divideAxis( world::blockAxisZ(column), spacing, remZ );
                const std::uint64_t h0 = hashAnchorCell( config.seed, anchorSet.config.salt, cellX, cellZ, 0u );
                const std::uint64_t h1 = hashAnchorCell( config.seed, anchorSet.config.salt, cellX, cellZ, 1u );
                if( remX != static_cast<std::int64_t>( h0 % static_cast<std::uint64_t>(spacing) ) ||
                    remZ != static_cast<std::int64_t>( h1 % static_cast<std::uint64_t>(spacing) ) ) continue;

                double probability = anchorSet.config.chance;
                if( anchorSet.densityField != NO_FIELD )
                    probability *= std::clamp( fieldValueAt( anchorSet.densityField, column, origin, sampled ), 0.0, 1.0 );
                bool enabled = unitRandom( hashAnchorCell(config.seed,anchorSet.config.salt,cellX,cellZ,2u) ) < probability;
                for( const RuntimeAnchorCondition &condition : anchorSet.conditions )
                {
                    if( !enabled ) break;
                    enabled = matchesCondition( fieldValueAt(condition.field,column,origin,sampled), condition.condition );
                }
                if( !enabled ) continue;

                world::BlockAddress anchorPos{};
                std::uint16_t supportBlock = std::numeric_limits<std::uint16_t>::max();
                if( anchorSet.config.surfaceMode == AnchorSurfaceMode::Postprocess )
                {
                    const auto snapped = resolvePostprocessSurface( anchorSet, column );
                    if( !snapped ) continue;
                    if( anchorSet.config.surfaceOffset == 0 ) supportBlock = snapped->blockId;
                    if( !world::tryOffsetBlock( snapped->position, 0,
                            static_cast<std::int64_t>(anchorSet.config.surfaceOffset)+1, 0, anchorPos ) ) continue;
                }
                else
                {
                    const double raw = fieldValueAt( anchorSet.surfaceField, column, origin, sampled );
                    const std::int64_t h = floorToBlock( raw, fields[anchorSet.surfaceField].config().id );
                    if( ( anchorSet.config.surfaceOffset > 0 && h > std::numeric_limits<std::int64_t>::max() - anchorSet.config.surfaceOffset - 1LL ) ||
                        ( anchorSet.config.surfaceOffset < 0 && h < std::numeric_limits<std::int64_t>::min() - anchorSet.config.surfaceOffset - 1LL ) )
                        continue;
                    anchorPos = world::withOriginRelativeY( column, h + anchorSet.config.surfaceOffset + 1LL );
                }
                DecorationAnchor a;
                a.position = anchorPos;
                a.seed = hashAnchorCell(config.seed,anchorSet.config.salt,cellX,cellZ,3u);
                a.variant = unitRandom(hashAnchorCell(config.seed,anchorSet.config.salt,cellX,cellZ,4u));
                a.supportBlock = supportBlock;
                anchors.push_back(a);
            }
            return anchors;
        }

        bool supportSatisfied( const RuntimeDecoration &decoration, const DecorationAnchor &anchor,
                               const world::BlockAddress &origin,
                               std::span<const std::uint16_t> base ) const
        {
            if( decoration.config.supportBlocks.empty() && decoration.config.supportTags.empty() ) return true;
            if( anchor.supportBlock != std::numeric_limits<std::uint16_t>::max() )
                return anchor.supportBlock < decoration.supportMask.size() && decoration.supportMask[anchor.supportBlock] != 0u;
            world::BlockAddress support;
            if( !world::tryOffsetBlock(anchor.position,0,-1,0,support) ) return false;
            std::size_t index=0;
            if( !localIndexOf(support,origin,index) ) return true;
            const std::uint16_t id=base[index];
            return id < decoration.supportMask.size() && decoration.supportMask[id] != 0u;
        }

        std::vector<BlockProposal> buildDecorationProposals( const RuntimeDecoration &decoration,
            const std::vector<DecorationAnchor> &anchors, const world::BlockAddress &origin,
            std::span<const std::uint16_t> base ) const
        {
            std::vector<BlockProposal> proposals;
            const auto emit = [&]( const world::BlockAddress &point, std::uint16_t blockId ) {
                std::size_t index=0;
                if( localIndexOf(point,origin,index) )
                    proposals.push_back({index,blockId,decoration.config.priority,decoration.order});
            };
            for( const DecorationAnchor &anchor : anchors )
            {
                if( anchor.variant < decoration.config.anchorMin || anchor.variant >= decoration.config.anchorMax ) continue;
                if( decoration.config.type != DecorationType::Structure &&
                    !supportSatisfied(decoration,anchor,origin,base) ) continue;
                if( decoration.config.type == DecorationType::Scatter )
                {
                    emit(anchor.position,decoration.blockId); continue;
                }
                if( decoration.config.type == DecorationType::Column )
                {
                    const std::uint64_t span = static_cast<std::uint64_t>(decoration.config.maxHeight) -
                                               static_cast<std::uint64_t>(decoration.config.minHeight) + 1ULL;
                    const std::int32_t height = decoration.config.minHeight +
                        static_cast<std::int32_t>(mix64(anchor.seed ^ decoration.config.salt)%span);
                    for( std::int32_t dy=0; dy<height; ++dy )
                    {
                        world::BlockAddress p;
                        if( world::tryOffsetBlock(anchor.position,0,dy,0,p) ) emit(p,decoration.blockId);
                    }
                    continue;
                }
                const auto &b=decoration.config.bounds;
                const std::uint64_t structureSeed=mix64(anchor.seed ^ decoration.config.salt);
                for(std::int32_t dx=b.minX;dx<=b.maxX;++dx) for(std::int32_t dy=b.minY;dy<=b.maxY;++dy)
                    for(std::int32_t dz=b.minZ;dz<=b.maxZ;++dz)
                    {
                        world::BlockAddress p;
                        if(!world::tryOffsetBlock(anchor.position,dx,dy,dz,p)) continue;
                        std::size_t dummy=0; if(!localIndexOf(p,origin,dummy)) continue;
                        const double raw=decoration.structureEvaluator->sample3DWithSeed(dx,dy,dz,structureSeed);
                        if(raw<=0.0) continue;
                        const double rounded=std::round(raw);
                        if(std::fabs(raw-rounded)>1e-9) throw std::runtime_error("structure palette result must be integer");
                        const std::int64_t pi=static_cast<std::int64_t>(rounded);
                        if(pi<1 || pi>static_cast<std::int64_t>(decoration.paletteIds.size()))
                            throw std::runtime_error("structure palette index out of range");
                        emit(p,decoration.paletteIds[static_cast<std::size_t>(pi-1)]);
                    }
            }
            return proposals;
        }
    };

    WorldGen::WorldGen( const WorldGenConfig &config, const world::BlockRegistry &blocks,
                        const world::BlockIdTable &table )
        : mState( std::make_unique<State>( config, blocks, table, nullptr ) ) {}

    WorldGen::WorldGen( const WorldGenConfig &config, const world::BlockRegistry &blocks,
                        const world::BlockIdTable &table, const world::BiomeRegistry &biomes )
        : mState( std::make_unique<State>( config, blocks, table, &biomes ) ) {}
    WorldGen::~WorldGen() = default;

    std::uint32_t WorldGen::generateChunkIds( const world::ChunkAddress &chunk,
                                               std::span<std::uint16_t> out ) const
    {
        const std::size_t expected=static_cast<std::size_t>(world::chunkVolume());
        if(out.size()!=expected) throw std::invalid_argument("WorldGen::generateChunkIds: wrong output size");
        const world::BlockAddress origin=world::chunkOrigin(chunk);
        std::fill(out.begin(),out.end(),0u);

        std::vector<SampledField> sampled(mState->fields.size());
        mState->executor->run(mState->field2DIndices.size(),[&](std::size_t job){
            const std::size_t i=mState->field2DIndices[job]; sampled[i]=mState->fields[i].sampleChunk(chunk);
        });

        mState->applyBiomeTerrain( chunk, sampled );

        std::vector<std::vector<DecorationAnchor>> anchors(mState->anchorSets.size());
        if(!mState->decorations.empty())
            mState->executor->run(mState->anchorSets.size(),[&](std::size_t i){
                anchors[i]=mState->buildAnchors(mState->anchorSets[i],origin,sampled);
            });

        const auto decorationTouchesChunkY=[&](){
            const auto chunkMin=world::blockAxisY(origin);
            const auto chunkMax=world::blockAxisY(world::blockAt(chunk,{0,EDGE-1,0}));
            for(std::size_t i=0;i<mState->anchorSets.size();++i)
            {
                const auto &bounds=mState->anchorSets[i].influenceBounds;
                for(const DecorationAnchor &a: anchors[i])
                {
                    world::BlockAddress lo,hi;
                    if(!world::tryOffsetBlock(a.position,0,bounds.minY,0,lo) ||
                       !world::tryOffsetBlock(a.position,0,bounds.maxY,0,hi)) continue;
                    if(world::blockAxisY(hi)>=chunkMin && world::blockAxisY(lo)<=chunkMax) return true;
                }
            }
            return false;
        };

        bool aboveBaseTerrain=false;
        if(!mState->volumeCanCreateInAir)
        {
            std::int64_t highest=std::numeric_limits<std::int64_t>::min();
            for(const RuntimePass &pass:mState->passes)
            {
                if(pass.config.type==PassType::Volume) continue;
                const SampledField *topField=nullptr; std::int32_t offset=0;
                if(pass.config.type==PassType::SurfaceLayer){topField=&sampled[pass.surfaceField];offset=pass.config.surfaceOffset;}
                else {topField=&sampled[pass.field]; if(pass.config.type==PassType::Surface) offset=pass.config.surfaceOffset;}
                for(std::int64_t lx=0;lx<EDGE;++lx) for(std::int64_t lz=0;lz<EDGE;++lz)
                {
                    if(!mState->columnEnabled(pass,sampled,lx,lz)) continue;
                    highest=std::max(highest,mState->shiftedSurface(topField->at2D(lx,lz),topField->id,offset));
                }
            }
            aboveBaseTerrain=world::blockAxisY(origin)>originHeightAxis(highest);
            if(aboveBaseTerrain && !decorationTouchesChunkY()) return 0u;
        }

        if(!aboveBaseTerrain)
        {
            for( const std::size_t i : mState->field3DIndices )
            {
                sampled[i] = SampledField{ mState->fields[i].config().id, FieldDimension::D3, {} };
                sampled[i].values.resize( static_cast<std::size_t>( world::chunkVolume() ) );
            }
            const std::size_t sliceJobs = mState->field3DIndices.size() *
                                          static_cast<std::size_t>( EDGE );
            mState->executor->run(sliceJobs,[&](std::size_t job){
                const std::size_t fieldJob = job / static_cast<std::size_t>( EDGE );
                const std::int64_t lx = static_cast<std::int64_t>(
                    job % static_cast<std::size_t>( EDGE ) );
                const std::size_t i = mState->field3DIndices[fieldJob];
                mState->fields[i].sampleChunkXSlice( chunk, lx, sampled[i] );
            });
            std::vector<std::vector<BlockProposal>> proposals(mState->passes.size());
            mState->executor->run(mState->passes.size(),[&](std::size_t i){
                proposals[i]=mState->buildPassProposals(mState->passes[i],sampled,origin);
            });
            const auto applyStage = [&]( const std::vector<std::size_t> &order ) {
                for( const std::size_t passIndex : order )
                {
                    const RuntimePass &pass = mState->passes[passIndex];
                    for( const BlockProposal &proposal : proposals[passIndex] )
                    {
                        if( mState->canReplace( out[proposal.localIndex], pass ) )
                            out[proposal.localIndex] = proposal.blockId;
                    }
                }
            };
            // Hard worldgen barrier: construct terrain first, then let addons
            // carve/replace it. Decoration is merged only after both stages.
            applyStage( mState->terrainMergeOrder );
            applyStage( mState->addonMergeOrder );
        }

        if(!mState->decorations.empty())
        {
            std::vector<std::vector<BlockProposal>> dprops(mState->decorations.size());
            mState->executor->run(mState->decorations.size(),[&](std::size_t i){
                const auto &d=mState->decorations[i];
                dprops[i]=mState->buildDecorationProposals(d,anchors[d.anchorSet],origin,out);
            });
            for(const std::size_t di:mState->decorationMergeOrder)
            {
                const auto &d=mState->decorations[di];
                for(const BlockProposal &p:dprops[di])
                    if(p.localIndex<out.size() && p.blockId!=0u && out[p.localIndex]<d.replaceMask.size() &&
                       d.replaceMask[out[p.localIndex]]!=0u) out[p.localIndex]=p.blockId;
            }
        }
        std::uint32_t nonAir=0; for(std::uint16_t id:out) nonAir += id!=0u ? 1u:0u; return nonAir;
    }

    void WorldGen::generateChunk( const world::ChunkAddress &chunk, std::vector<BlockDelta> &out ) const
    {
        std::vector<std::uint16_t> ids(static_cast<std::size_t>(world::chunkVolume()),0u);
        const std::uint32_t nonAir=generateChunkIds(chunk,ids); out.clear(); out.reserve(nonAir);
        for(std::int64_t lx=0;lx<EDGE;++lx) for(std::int64_t ly=0;ly<EDGE;++ly) for(std::int64_t lz=0;lz<EDGE;++lz)
        {
            const std::uint16_t id=ids[denseIndex(lx,ly,lz)]; if(id==0u) continue;
            out.push_back({world::blockAt(chunk,{lx,ly,lz}),mState->table.idOf(id)});
        }
    }

    std::int64_t WorldGen::surfaceHeight( const world::BlockAddress &column ) const
    {
        const LuaFieldEvaluator &surface=mState->fields[mState->surfaceField];
        const double height = mState->fallbackTerrain.has_value()
            ? mState->adjustedSurfaceAt( column )
            : surface.sample2D( column );
        return floorToBlock( height, surface.config().id );
    }
    std::uint32_t WorldGen::workerThreads() const { return mState->executor->workerCount(); }
    std::size_t WorldGen::fieldCount() const { return mState->fields.size(); }
    std::size_t WorldGen::passCount() const { return mState->passes.size(); }
    std::size_t WorldGen::anchorSetCount() const { return mState->anchorSets.size(); }
    std::size_t WorldGen::decorationPassCount() const { return mState->decorations.size(); }
} // namespace worldgen
