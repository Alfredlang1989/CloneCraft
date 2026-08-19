        return CHUNKS_PER_GROUP_EDGE * CHUNKS_PER_GROUP_EDGE * CHUNKS_PER_GROUP_EDGE;
    }

    inline bool validLocal( std::int64_t value, std::int64_t radix ) noexcept
    {
        return value >= 0 && value < radix;
    }

    inline void requireCanonical( const GroupAddress &a )
    {
        if( !validLocal( a.region.x, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.region.y, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.region.z, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.section.x, SECTIONS_PER_REGION_EDGE ) ||
            !validLocal( a.section.y, SECTIONS_PER_REGION_EDGE ) ||
            !validLocal( a.section.z, SECTIONS_PER_REGION_EDGE ) ||
            !validLocal( a.group.x, GROUPS_PER_SECTION_EDGE ) ||
            !validLocal( a.group.y, GROUPS_PER_SECTION_EDGE ) ||
            !validLocal( a.group.z, GROUPS_PER_SECTION_EDGE ) )
            throw std::invalid_argument( "non-canonical GroupAddress" );
    }
    inline void requireCanonical( const ChunkAddress &a )
    {
        requireCanonical( a.group );
        if( !validLocal( a.chunk.x, CHUNKS_PER_GROUP_EDGE ) ||
            !validLocal( a.chunk.y, CHUNKS_PER_GROUP_EDGE ) ||
            !validLocal( a.chunk.z, CHUNKS_PER_GROUP_EDGE ) )
            throw std::invalid_argument( "non-canonical ChunkAddress" );
    }
    inline void requireCanonical( const BlockAddress &a )
    {
        requireCanonical( a.chunk );
        if( !validLocal( a.block.x, BLOCKS_PER_CHUNK_EDGE ) ||
            !validLocal( a.block.y, BLOCKS_PER_CHUNK_EDGE ) ||
            !validLocal( a.block.z, BLOCKS_PER_CHUNK_EDGE ) )
            throw std::invalid_argument( "non-canonical BlockAddress" );
    }
    // The logical super-tier views validate their local digits exactly like
    // GroupAddress does (M01-B #20); Sector is the unbounded outermost digit.
    inline void requireCanonical( const RegionAddress &a )
    {
        if( !validLocal( a.region.x, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.region.y, REGIONS_PER_SECTOR_EDGE ) ||
            !validLocal( a.region.z, REGIONS_PER_SECTOR_EDGE ) )
            throw std::invalid_argument( "non-canonical RegionAddress" );
    }
    inline void requireCanonical( const SectionAddress &a )
    {
        requireCanonical( RegionAddress{ a.sector, a.region } );
        if( !validLocal( a.section.x, SECTIONS_PER_REGION_EDGE ) ||
            !validLocal( a.section.y, SECTIONS_PER_REGION_EDGE ) ||
            !validLocal( a.section.z, SECTIONS_PER_REGION_EDGE ) )
            throw std::invalid_argument( "non-canonical SectionAddress" );
    }
    inline void requireCanonical( const SectorAddress & ) {}

    namespace detail
    {
        struct AxisAddress
        {
            std::int64_t sector = 0;
            std::int64_t region = 0;
            std::int64_t section = 0;
            std::int64_t group = 0;
            std::int64_t chunk = 0;
            std::int64_t block = 0;
            friend constexpr auto operator<=>( const AxisAddress &, const AxisAddress & ) = default;
        };

        inline bool tryAddI64( std::int64_t a, std::int64_t b, std::int64_t &out ) noexcept
        {
            if( ( b > 0 && a > std::numeric_limits<std::int64_t>::max() - b ) ||
                ( b < 0 && a < std::numeric_limits<std::int64_t>::min() - b ) )
                return false;
            out = a + b;
            return true;
        }

        inline bool propagateDigit( std::int64_t &digit, std::int64_t radix,
                                    std::int64_t &carry ) noexcept
        {
            const std::int64_t quotient = floorDiv( carry, radix );
            const std::int64_t remainder = floorMod( carry, radix );
            const omnigrid_i128 sum = static_cast<omnigrid_i128>( digit ) + remainder;
            digit = static_cast<std::int64_t>( sum % radix );
            const std::int64_t extra = static_cast<std::int64_t>( sum / radix );
            return tryAddI64( quotient, extra, carry );
        }

        inline bool tryOffsetAxis( AxisAddress base, std::int64_t delta, AxisAddress &out ) noexcept
        {
            std::int64_t carry = floorDiv( delta, BLOCKS_PER_CHUNK_EDGE );
            const std::int64_t remainder = floorMod( delta, BLOCKS_PER_CHUNK_EDGE );
            const omnigrid_i128 blockSum = static_cast<omnigrid_i128>( base.block ) + remainder;
            base.block = static_cast<std::int64_t>( blockSum % BLOCKS_PER_CHUNK_EDGE );
            if( !tryAddI64( carry,
                            static_cast<std::int64_t>( blockSum / BLOCKS_PER_CHUNK_EDGE ),
                            carry ) )
                return false;
            if( !propagateDigit( base.chunk, CHUNKS_PER_GROUP_EDGE, carry ) ||
                !propagateDigit( base.group, GROUPS_PER_SECTION_EDGE, carry ) ||
                !propagateDigit( base.section, SECTIONS_PER_REGION_EDGE, carry ) ||
                !propagateDigit( base.region, REGIONS_PER_SECTOR_EDGE, carry ) )
                return false;
            if( !tryAddI64( base.sector, carry, base.sector ) ) return false;
            out = base;
            return true;
        }

        inline bool tryOffsetChunkAxis( AxisAddress base, std::int64_t delta,
                                        AxisAddress &out ) noexcept
        {
            base.block = 0;
            std::int64_t carry = floorDiv( delta, CHUNKS_PER_GROUP_EDGE );
            const std::int64_t remainder = floorMod( delta, CHUNKS_PER_GROUP_EDGE );
            const omnigrid_i128 chunkSum = static_cast<omnigrid_i128>( base.chunk ) + remainder;
            base.chunk = static_cast<std::int64_t>( chunkSum % CHUNKS_PER_GROUP_EDGE );
            if( !tryAddI64( carry,
                            static_cast<std::int64_t>( chunkSum / CHUNKS_PER_GROUP_EDGE ),
                            carry ) )
                return false;
            if( !propagateDigit( base.group, GROUPS_PER_SECTION_EDGE, carry ) ||
                !propagateDigit( base.section, SECTIONS_PER_REGION_EDGE, carry ) ||
                !propagateDigit( base.region, REGIONS_PER_SECTOR_EDGE, carry ) )
                return false;
            if( !tryAddI64( base.sector, carry, base.sector ) ) return false;
            out = base;
            return true;
        }

        inline bool tryOffsetGroupAxis( AxisAddress base, std::int64_t delta,
                                        AxisAddress &out ) noexcept
        {
            base.block = 0;
            base.chunk = 0;
            std::int64_t carry = floorDiv( delta, GROUPS_PER_SECTION_EDGE );
            const std::int64_t remainder = floorMod( delta, GROUPS_PER_SECTION_EDGE );
            const omnigrid_i128 groupSum = static_cast<omnigrid_i128>( base.group ) + remainder;
            base.group = static_cast<std::int64_t>( groupSum % GROUPS_PER_SECTION_EDGE );
            if( !tryAddI64( carry,
                            static_cast<std::int64_t>( groupSum / GROUPS_PER_SECTION_EDGE ),
                            carry ) )
                return false;
            if( !propagateDigit( base.section, SECTIONS_PER_REGION_EDGE, carry ) ||
                !propagateDigit( base.region, REGIONS_PER_SECTOR_EDGE, carry ) ) return false;
            if( !tryAddI64( base.sector, carry, base.sector ) ) return false;
            out = base;
            return true;
