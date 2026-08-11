        Call3 &call3( double sx, double sy, double sz, std::uint64_t salt )
        {
            Call3 &call = calls3[static_cast<std::size_t>( salt ) & ( CACHE_SIZE - 1u )];
            if( !call.valid || call.salt != salt || call.sx != sx || call.sy != sy || call.sz != sz )
            {
                validateScale( sx ); validateScale( sy ); validateScale( sz );
                call.salt = salt; call.sx = sx; call.sy = sy; call.sz = sz;
                call.c = {sx*(1.0+ROTATE_S2),sy*-ROTATE_Y,sz*ROTATE_S2,sx*ROTATE_Y,sy*ROTATE_Y,sz*ROTATE_Y,sx*ROTATE_S2,sy*-ROTATE_Y,sz*(1.0+ROTATE_S2)};
                call.valid=true;call.groupEpoch=0u;
            }
            if(call.groupEpoch!=groupEpoch){call.phaseXr=sumPhase(coefficientPhase(groupOriginX,call.c[0]),coefficientPhase(groupOriginY,call.c[1]),coefficientPhase(groupOriginZ,call.c[2]));call.phaseYr=sumPhase(coefficientPhase(groupOriginX,call.c[3]),coefficientPhase(groupOriginY,call.c[4]),coefficientPhase(groupOriginZ,call.c[5]));call.phaseZr=sumPhase(coefficientPhase(groupOriginX,call.c[6]),coefficientPhase(groupOriginY,call.c[7]),coefficientPhase(groupOriginZ,call.c[8]));call.groupEpoch=groupEpoch;}return call;
        }
        double noise2(double scale,std::uint64_t salt,double ox,double oz){if(!sampleValid)throw std::logic_error("mapped noise sample was not selected");if(!std::isfinite(ox)||!std::isfinite(oz))throw std::invalid_argument("mapped noise offset must be finite");Call2&f=call2(scale,salt);const double x=static_cast<double>(localGroupX)+macroWarpX+ox,z=static_cast<double>(localGroupZ)+macroWarpZ+oz;const double xs=wrapLattice(f.phaseXs+f.cxx*x+f.cxz*z),ys=wrapLattice(f.phaseYs+f.cxz*x+f.cxx*z);return noiseFor(salt).noise2Lattice(xs,ys);}
        double noise3(double sx,double sy,double sz,std::uint64_t salt,double ox,double oy,double oz){if(!sampleValid)throw std::logic_error("mapped noise sample was not selected");if(!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz))throw std::invalid_argument("mapped noise offset must be finite");Call3&f=call3(sx,sy,sz,salt);const double x=static_cast<double>(localGroupX)+macroWarpX+ox,y=static_cast<double>(localGroupY)+oy,z=static_cast<double>(localGroupZ)+macroWarpZ+oz;const double xr=wrapLattice(f.phaseXr+f.c[0]*x+f.c[1]*y+f.c[2]*z),yr=wrapLattice(f.phaseYr+f.c[3]*x+f.c[4]*y+f.c[5]*z),zr=wrapLattice(f.phaseZr+f.c[6]*x+f.c[7]*y+f.c[8]*z);return noiseFor(salt).noise3BccLattice(xr,yr,zr);}
    };
    MappedOpenSimplexNoise::MappedOpenSimplexNoise(std::uint64_t worldSeed,std::uint64_t fieldSeed,MacroWarpSettings settings):mImpl(std::make_unique<Impl>(worldSeed,fieldSeed,settings)){}
    MappedOpenSimplexNoise::~MappedOpenSimplexNoise()=default;MappedOpenSimplexNoise::MappedOpenSimplexNoise(MappedOpenSimplexNoise&&)noexcept=default;MappedOpenSimplexNoise&MappedOpenSimplexNoise::operator=(MappedOpenSimplexNoise&&)noexcept=default;
    void MappedOpenSimplexNoise::setSample(const world::BlockAddress&sample)const{mImpl->setSample(sample);} void MappedOpenSimplexNoise::beginChunkXSlice(const world::ChunkAddress&chunk,std::int64_t localX)const{mImpl->beginChunkXSlice(chunk,localX);} void MappedOpenSimplexNoise::setSliceSample(std::int64_t localY,std::int64_t localZ)const{mImpl->setSliceSample(localY,localZ);} double MappedOpenSimplexNoise::noise2(double scale,std::uint64_t salt,double ox,double oz)const{return mImpl->noise2(scale,salt,ox,oz);} double MappedOpenSimplexNoise::noise3(double sx,double sy,double sz,std::uint64_t salt,double ox,double oy,double oz)const{return mImpl->noise3(sx,sy,sz,salt,ox,oy,oz);} const MacroWarpSettings&MappedOpenSimplexNoise::macroWarpSettings()const{return mImpl->settings;}
} // namespace worldgen
