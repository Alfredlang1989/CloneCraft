
        int pinFieldFunction( lua_State *L, const FieldConfig &config )
        {
            if( lua_getglobal( L, config.functionName.c_str() ) != lua54::TFUNCTION )
                throw std::runtime_error( config.scriptPath.string() + ": field function vanished" );
            return lua_gettop( L );
        }

        double invokeWorldField( lua_State *L, int functionIndex, const FieldConfig &config,
                                 std::uint64_t seed )
        {
            lua_pushvalue( L, functionIndex );
            lua_pushinteger( L, static_cast<lua_Integer>( seed & 0x7FFFFFFFFFFFFFFFULL ) );
            if( lua54::pcall( L, 1, 1 ) != lua54::OK )
                throw std::runtime_error( config.scriptPath.string() + ": " + luaError( L ) );
            int ok = 0;
            const double result = lua_tonumberx( L, -1, &ok );
            lua54::pop( L, 1 );
            if( !ok || !std::isfinite( result ) )
                throw std::runtime_error( config.scriptPath.string() + ": field function must return a finite number" );
            return result;
        }

        double invokeStructure( lua_State *L, int functionIndex, const FieldConfig &config,
                                std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z )
        {
            lua_pushvalue( L, functionIndex );
            lua_pushinteger( L, static_cast<lua_Integer>( x ) );
            lua_pushinteger( L, static_cast<lua_Integer>( y ) );
            lua_pushinteger( L, static_cast<lua_Integer>( z ) );
            lua_pushinteger( L, static_cast<lua_Integer>( seed & 0x7FFFFFFFFFFFFFFFULL ) );
            if( lua54::pcall( L, 4, 1 ) != lua54::OK )
                throw std::runtime_error( config.scriptPath.string() + ": " + luaError( L ) );
            int ok = 0;
            const double result = lua_tonumberx( L, -1, &ok );
            lua54::pop( L, 1 );
            if( !ok || !std::isfinite( result ) )
                throw std::runtime_error( config.scriptPath.string() + ": structure function must return a finite number" );
            return result;
        }
    } // namespace

    double SampledField::at2D( std::int64_t lx, std::int64_t lz ) const
    {
        if( dimension != FieldDimension::D2 ) throw std::logic_error( "2D lookup on 3D field '" + id + "'" );
        return values[static_cast<std::size_t>( lx * EDGE + lz )];
    }
    double SampledField::at3D( std::int64_t lx, std::int64_t ly, std::int64_t lz ) const
    {
        if( dimension != FieldDimension::D3 ) throw std::logic_error( "3D lookup on 2D field '" + id + "'" );
        return values[static_cast<std::size_t>( ( lx * EDGE + ly ) * EDGE + lz )];
    }

    LuaFieldEvaluator::LuaFieldEvaluator( const FieldConfig &config, std::uint64_t worldSeed )
        : mConfig( config ), mWorldSeed( worldSeed ),
          mInstanceId( gNextEvaluatorId.fetch_add( 1u, std::memory_order_relaxed ) ),
          mLifetimeToken( std::make_shared<int>( 0 ) )
    {
        std::ifstream file( mConfig.scriptPath, std::ios::binary );
        if( !file ) throw std::runtime_error( "could not open worldgen Lua field '" + mConfig.scriptPath.string() + "'" );
        std::ostringstream buffer; buffer << file.rdbuf(); mScriptSource = buffer.str();
        LuaContext context;
        (void)createState( mConfig, mWorldSeed, mScriptSource, context );
    }

    void LuaFieldEvaluator::sampleChunkXSlice( const world::ChunkAddress &chunk,
                                                   std::int64_t lx,
                                                   SampledField &sampled ) const
    {
        if( lx < 0 || lx >= EDGE )
            throw std::out_of_range( "worldgen X slice outside chunk" );
        if( sampled.dimension != mConfig.dimension || sampled.id != mConfig.id )
            throw std::invalid_argument( "worldgen sampled field does not match evaluator" );
        const std::size_t expected = mConfig.dimension == FieldDimension::D2
            ? static_cast<std::size_t>( EDGE * EDGE )
            : static_cast<std::size_t>( world::chunkVolume() );
        if( sampled.values.size() != expected )
            throw std::invalid_argument( "worldgen sampled field has wrong storage size" );

        ThreadVm &vm = threadVmFor( mInstanceId, mLifetimeToken, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        lua_State *state = vm.state.get();
        const std::uint64_t scriptSeed = deriveNoiseSeed( mWorldSeed, mConfig.salt );
        const int functionIndex = pinFieldFunction( state, mConfig );
        vm.context.noise->beginChunkXSlice( chunk, lx );
        vm.context.hasSample = true;

        if( mConfig.dimension == FieldDimension::D2 )
        {
            for( std::int64_t lz = 0; lz < EDGE; ++lz )
            {
                vm.context.noise->setSliceSample( 0, lz );
                sampled.values[static_cast<std::size_t>( lx * EDGE + lz )] =
                    invokeWorldField( state, functionIndex, mConfig, scriptSeed );
            }
        }
        else
        {
            // Z before Y lets the mapper reuse the precomputed horizontal
            // macro warp for all 16 vertical samples in a column.
            for( std::int64_t lz = 0; lz < EDGE; ++lz )
                for( std::int64_t ly = 0; ly < EDGE; ++ly )
                {
                    vm.context.noise->setSliceSample( ly, lz );
                    sampled.values[static_cast<std::size_t>( ( lx * EDGE + ly ) * EDGE + lz )] =
                        invokeWorldField( state, functionIndex, mConfig, scriptSeed );
                }
        }
        vm.context.hasSample = false;
        lua54::pop( state, 1 );
    }

    SampledField LuaFieldEvaluator::sampleChunk( const world::ChunkAddress &chunk ) const
    {
        SampledField sampled{ mConfig.id, mConfig.dimension, {} };
        sampled.values.resize( mConfig.dimension == FieldDimension::D2
            ? static_cast<std::size_t>( EDGE * EDGE )
            : static_cast<std::size_t>( world::chunkVolume() ) );
        for( std::int64_t lx = 0; lx < EDGE; ++lx )
            sampleChunkXSlice( chunk, lx, sampled );
        return sampled;
    }

    double LuaFieldEvaluator::sample2D( const world::BlockAddress &point ) const
    {
        if( mConfig.dimension != FieldDimension::D2 ) throw std::logic_error( "2D sample on non-2D field" );
        ThreadVm &vm = threadVmFor( mInstanceId, mLifetimeToken, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        vm.context.sample = point; vm.context.noise->setSample( point ); vm.context.hasSample = true;
        lua_State *state = vm.state.get();
        const int functionIndex = pinFieldFunction( state, mConfig );
        const double result = invokeWorldField( state, functionIndex, mConfig,
                                                deriveNoiseSeed( mWorldSeed, mConfig.salt ) );
        vm.context.hasSample = false;
        lua54::pop( state, 1 );
        return result;
    }

    double LuaFieldEvaluator::sample3D( const world::BlockAddress &point ) const
    {
        if( mConfig.dimension != FieldDimension::D3 ) throw std::logic_error( "3D sample on non-3D field" );
        ThreadVm &vm = threadVmFor( mInstanceId, mLifetimeToken, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        vm.context.sample = point; vm.context.noise->setSample( point ); vm.context.hasSample = true;
        lua_State *state = vm.state.get();
        const int functionIndex = pinFieldFunction( state, mConfig );
        const double result = invokeWorldField( state, functionIndex, mConfig,
                                                deriveNoiseSeed( mWorldSeed, mConfig.salt ) );
        vm.context.hasSample = false;
        lua54::pop( state, 1 );
        return result;
    }

    double LuaFieldEvaluator::sample3DWithSeed( std::int64_t x, std::int64_t y, std::int64_t z,
                                                 std::uint64_t sampleSeed ) const
    {
        ThreadVm &vm = threadVmFor( mInstanceId, mLifetimeToken, mConfig, mWorldSeed, mScriptSource );
        LuaContextScope scope( vm.context );
        vm.context.hasSample = false;
        lua_State *state = vm.state.get();
        const int functionIndex = pinFieldFunction( state, mConfig );
        const double result = invokeStructure( state, functionIndex, mConfig, sampleSeed, x, y, z );
        lua54::pop( state, 1 );
        return result;
    }
} // namespace worldgen
