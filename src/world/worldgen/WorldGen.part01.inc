
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
            std::size_t stageIndex = NO_FIELD;
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
            // Hash every mixed-radix digit separately. In particular, Section
            // must participate or decoration cells repeat every 65,536 blocks.
            h = mix64( h ^ tag );
            h = mix64( h ^ static_cast<std::uint64_t>( a.sector ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.region ) + 0x11u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.section ) + 0x22u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.group ) + 0x33u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.chunk ) + 0x44u ) );
            h = mix64( h ^ ( static_cast<std::uint64_t>( a.block ) + 0x55u ) );
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
            if( divisor <= 0 )
                throw std::invalid_argument( "decoration anchor divisor must be positive" );

            // Long division directly over the mixed-radix hierarchy. Never
            // flatten the astronomical address. A 128-bit intermediate is
            // required because remainder * a 9e18 logical radix may exceed i64.
            world::detail::AxisAddress q{};
            q.sector = world::floorDiv( value.sector, divisor );
            remainder = world::floorMod( value.sector, divisor );
            auto step = [&]( std::int64_t radix, std::int64_t digit, std::int64_t &qd ) {
                const world::clonecraft_i128 current =
                    static_cast<world::clonecraft_i128>( remainder ) * radix + digit;
                qd = static_cast<std::int64_t>( current / divisor );
                remainder = static_cast<std::int64_t>( current % divisor );
            };
            step( world::REGIONS_PER_SECTOR_EDGE, value.region, q.region );
            step( world::SECTIONS_PER_REGION_EDGE, value.section, q.section );
            step( world::GROUPS_PER_SECTION_EDGE, value.group, q.group );
            step( world::CHUNKS_PER_GROUP_EDGE, value.chunk, q.chunk );
            step( world::BLOCKS_PER_CHUNK_EDGE, value.block, q.block );
            return q;
        }

        world::detail::AxisAddress originHeightAxis( std::int64_t y )
        {
            return world::blockAxisY( world::fromOriginOffset( 0, y, 0 ) );
        }

        bool localIndexOf( const world::BlockAddress &point, const world::BlockAddress &origin,
