#pragma once
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include "world/coordinates/CoordMath.h"

namespace world {
#if defined(__SIZEOF_INT128__)
__extension__ typedef __int128 clonecraft_i128;
#else
#error "Clonecraft phase-2 coordinates require __int128"
#endif
#ifndef CLONECRAFT_CHUNK_EDGE
#define CLONECRAFT_CHUNK_EDGE 16
#endif
#ifndef CLONECRAFT_GROUP_EDGE
#define CLONECRAFT_GROUP_EDGE 16
#endif
#ifndef CLONECRAFT_SECTION_EDGE
#define CLONECRAFT_SECTION_EDGE 256
#endif
#ifndef CLONECRAFT_REGION_EDGE
#define CLONECRAFT_REGION_EDGE 9000000000000000000LL
#endif
#ifndef CLONECRAFT_SECTOR_EDGE
#define CLONECRAFT_SECTOR_EDGE 9000000000000000000LL
#endif
inline constexpr std::int64_t BLOCKS_PER_CHUNK_EDGE=CLONECRAFT_CHUNK_EDGE;
inline constexpr std::int64_t CHUNKS_PER_GROUP_EDGE=CLONECRAFT_GROUP_EDGE;
inline constexpr std::int64_t GROUPS_PER_SECTION_EDGE=CLONECRAFT_SECTION_EDGE;
inline constexpr std::int64_t SECTIONS_PER_REGION_EDGE=CLONECRAFT_REGION_EDGE;
inline constexpr std::int64_t REGIONS_PER_SECTOR_EDGE=CLONECRAFT_SECTOR_EDGE;
inline constexpr std::int64_t BLOCKS_PER_GROUP_EDGE=BLOCKS_PER_CHUNK_EDGE*CHUNKS_PER_GROUP_EDGE;
static_assert(BLOCKS_PER_CHUNK_EDGE>0&&CHUNKS_PER_GROUP_EDGE>0&&GROUPS_PER_SECTION_EDGE>0&&SECTIONS_PER_REGION_EDGE>0&&REGIONS_PER_SECTOR_EDGE>0);
static_assert(BLOCKS_PER_CHUNK_EDGE*BLOCKS_PER_CHUNK_EDGE*BLOCKS_PER_CHUNK_EDGE<=static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
static_assert(CHUNKS_PER_GROUP_EDGE*CHUNKS_PER_GROUP_EDGE*CHUNKS_PER_GROUP_EDGE<=static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));

#define CC_COORD3(name) struct name { std::int64_t x=0,y=0,z=0; friend constexpr auto operator<=>(const name&,const name&)=default; }
CC_COORD3(SectorCoord); CC_COORD3(LocalRegionCoord); CC_COORD3(LocalSectionCoord); CC_COORD3(LocalGroupCoord); CC_COORD3(LocalChunkCoord); CC_COORD3(LocalBlockCoord); CC_COORD3(LocalGroupBlockCoord); CC_COORD3(RelativeI64);
#undef CC_COORD3
struct GroupAddress { SectorCoord sector{}; LocalRegionCoord region{}; LocalSectionCoord section{}; LocalGroupCoord group{}; friend constexpr auto operator<=>(const GroupAddress&,const GroupAddress&)=default; };
struct ChunkAddress { GroupAddress group{}; LocalChunkCoord chunk{}; friend constexpr auto operator<=>(const ChunkAddress&,const ChunkAddress&)=default; };
struct BlockAddress { ChunkAddress chunk{}; LocalBlockCoord block{}; friend constexpr auto operator<=>(const BlockAddress&,const BlockAddress&)=default; };
inline constexpr GroupAddress originGroupAddress(){return{};} inline constexpr ChunkAddress originChunkAddress(){return{};} inline constexpr BlockAddress originBlockAddress(){return{};}
inline constexpr std::int64_t chunkVolume(){return BLOCKS_PER_CHUNK_EDGE*BLOCKS_PER_CHUNK_EDGE*BLOCKS_PER_CHUNK_EDGE;}
inline constexpr std::int64_t groupVolume(){return CHUNKS_PER_GROUP_EDGE*CHUNKS_PER_GROUP_EDGE*CHUNKS_PER_GROUP_EDGE;}
inline bool validLocal(std::int64_t v,std::int64_t r) noexcept{return v>=0&&v<r;}
inline void requireCanonical(const GroupAddress&a){if(!validLocal(a.region.x,REGIONS_PER_SECTOR_EDGE)||!validLocal(a.region.y,REGIONS_PER_SECTOR_EDGE)||!validLocal(a.region.z,REGIONS_PER_SECTOR_EDGE)||!validLocal(a.section.x,SECTIONS_PER_REGION_EDGE)||!validLocal(a.section.y,SECTIONS_PER_REGION_EDGE)||!validLocal(a.section.z,SECTIONS_PER_REGION_EDGE)||!validLocal(a.group.x,GROUPS_PER_SECTION_EDGE)||!validLocal(a.group.y,GROUPS_PER_SECTION_EDGE)||!validLocal(a.group.z,GROUPS_PER_SECTION_EDGE))throw std::invalid_argument("non-canonical GroupAddress");}
inline void requireCanonical(const ChunkAddress&a){requireCanonical(a.group);if(!validLocal(a.chunk.x,CHUNKS_PER_GROUP_EDGE)||!validLocal(a.chunk.y,CHUNKS_PER_GROUP_EDGE)||!validLocal(a.chunk.z,CHUNKS_PER_GROUP_EDGE))throw std::invalid_argument("non-canonical ChunkAddress");}
inline void requireCanonical(const BlockAddress&a){requireCanonical(a.chunk);if(!validLocal(a.block.x,BLOCKS_PER_CHUNK_EDGE)||!validLocal(a.block.y,BLOCKS_PER_CHUNK_EDGE)||!validLocal(a.block.z,BLOCKS_PER_CHUNK_EDGE))throw std::invalid_argument("non-canonical BlockAddress");}

namespace detail {
struct AxisAddress{std::int64_t sector=0,region=0,section=0,group=0,chunk=0,block=0;friend constexpr auto operator<=>(const AxisAddress&,const AxisAddress&)=default;};
inline bool tryAddI64(std::int64_t a,std::int64_t b,std::int64_t&out)noexcept{if((b>0&&a>std::numeric_limits<std::int64_t>::max()-b)||(b<0&&a<std::numeric_limits<std::int64_t>::min()-b))return false;out=a+b;return true;}
inline bool carryDigit(std::int64_t&digit,std::int64_t radix,std::int64_t&carry)noexcept{const std::int64_t q=floorDiv(carry,radix),r=floorMod(carry,radix);const clonecraft_i128 s=static_cast<clonecraft_i128>(digit)+r;digit=static_cast<std::int64_t>(s%radix);return tryAddI64(q,static_cast<std::int64_t>(s/radix),carry);}
inline bool tryOffsetAxis(AxisAddress a,std::int64_t delta,AxisAddress&out)noexcept{std::int64_t c=floorDiv(delta,BLOCKS_PER_CHUNK_EDGE);const auto r=floorMod(delta,BLOCKS_PER_CHUNK_EDGE);const clonecraft_i128 s=static_cast<clonecraft_i128>(a.block)+r;a.block=static_cast<std::int64_t>(s%BLOCKS_PER_CHUNK_EDGE);if(!tryAddI64(c,static_cast<std::int64_t>(s/BLOCKS_PER_CHUNK_EDGE),c)||!carryDigit(a.chunk,CHUNKS_PER_GROUP_EDGE,c)||!carryDigit(a.group,GROUPS_PER_SECTION_EDGE,c)||!carryDigit(a.section,SECTIONS_PER_REGION_EDGE,c)||!carryDigit(a.region,REGIONS_PER_SECTOR_EDGE,c)||!tryAddI64(a.sector,c,a.sector))return false;out=a;return true;}
inline bool tryOffsetChunkAxis(AxisAddress a,std::int64_t delta,AxisAddress&out)noexcept{a.block=0;std::int64_t c=floorDiv(delta,CHUNKS_PER_GROUP_EDGE);const auto r=floorMod(delta,CHUNKS_PER_GROUP_EDGE);const clonecraft_i128 s=static_cast<clonecraft_i128>(a.chunk)+r;a.chunk=static_cast<std::int64_t>(s%CHUNKS_PER_GROUP_EDGE);if(!tryAddI64(c,static_cast<std::int64_t>(s/CHUNKS_PER_GROUP_EDGE),c)||!carryDigit(a.group,GROUPS_PER_SECTION_EDGE,c)||!carryDigit(a.section,SECTIONS_PER_REGION_EDGE,c)||!carryDigit(a.region,REGIONS_PER_SECTOR_EDGE,c)||!tryAddI64(a.sector,c,a.sector))return false;out=a;return true;}
inline bool tryOffsetGroupAxis(AxisAddress a,std::int64_t delta,AxisAddress&out)noexcept{a.block=a.chunk=0;std::int64_t c=floorDiv(delta,GROUPS_PER_SECTION_EDGE);const auto r=floorMod(delta,GROUPS_PER_SECTION_EDGE);const clonecraft_i128 s=static_cast<clonecraft_i128>(a.group)+r;a.group=static_cast<std::int64_t>(s%GROUPS_PER_SECTION_EDGE);if(!tryAddI64(c,static_cast<std::int64_t>(s/GROUPS_PER_SECTION_EDGE),c)||!carryDigit(a.section,SECTIONS_PER_REGION_EDGE,c)||!carryDigit(a.region,REGIONS_PER_SECTOR_EDGE,c)||!tryAddI64(a.sector,c,a.sector))return false;out=a;return true;}
template<class F> inline bool boundedDelta(const AxisAddress&p,const AxisAddress&o,std::int64_t max,std::int64_t&out,F f)noexcept{if(max<0)return false;clonecraft_i128 lo=-static_cast<clonecraft_i128>(max),hi=max;while(lo<=hi){const auto mw=lo+(hi-lo)/2;const auto m=static_cast<std::int64_t>(mw);AxisAddress c{};if(!f(o,m,c)){if(m<0)lo=mw+1;else hi=mw-1;continue;}if(c==p){out=m;return true;}if(c<p)lo=mw+1;else hi=mw-1;}return false;}
inline bool blockAxisDeltaWithin(const AxisAddress&p,const AxisAddress&o,std::int64_t max,std::int64_t&out)noexcept{if(p.sector==o.sector&&p.region==o.region&&p.section==o.section&&p.group==o.group){const clonecraft_i128 d=(static_cast<clonecraft_i128>(p.chunk-o.chunk)*BLOCKS_PER_CHUNK_EDGE)+(p.block-o.block);if(d>=-static_cast<clonecraft_i128>(max)&&d<=max){out=static_cast<std::int64_t>(d);return true;}return false;}return boundedDelta(p,o,max,out,[](auto a,auto d,auto&r){return tryOffsetAxis(a,d,r);});}
inline bool chunkAxisDeltaWithin(const AxisAddress&p,const AxisAddress&o,std::int64_t max,std::int64_t&out)noexcept{if(p.sector==o.sector&&p.region==o.region&&p.section==o.section&&p.group==o.group){const auto d=p.chunk-o.chunk;if(d>=-max&&d<=max){out=d;return true;}return false;}return boundedDelta(p,o,max,out,[](auto a,auto d,auto&r){return tryOffsetChunkAxis(a,d,r);});}
inline bool groupAxisDeltaWithin(const AxisAddress&p,const AxisAddress&o,std::int64_t max,std::int64_t&out)noexcept{if(p.sector==o.sector&&p.region==o.region&&p.section==o.section){const auto d=p.group-o.group;if(d>=-max&&d<=max){out=d;return true;}return false;}return boundedDelta(p,o,max,out,[](auto a,auto d,auto&r){return tryOffsetGroupAxis(a,d,r);});}
}
inline detail::AxisAddress blockAxisX(const BlockAddress&a)noexcept{return{a.chunk.group.sector.x,a.chunk.group.region.x,a.chunk.group.section.x,a.chunk.group.group.x,a.chunk.chunk.x,a.block.x};}
inline detail::AxisAddress blockAxisY(const BlockAddress&a)noexcept{return{a.chunk.group.sector.y,a.chunk.group.region.y,a.chunk.group.section.y,a.chunk.group.group.y,a.chunk.chunk.y,a.block.y};}
inline detail::AxisAddress blockAxisZ(const BlockAddress&a)noexcept{return{a.chunk.group.sector.z,a.chunk.group.region.z,a.chunk.group.section.z,a.chunk.group.group.z,a.chunk.chunk.z,a.block.z};}
inline BlockAddress fromAxes(const detail::AxisAddress&x,const detail::AxisAddress&y,const detail::AxisAddress&z)noexcept{return{{{{x.sector,y.sector,z.sector},{x.region,y.region,z.region},{x.section,y.section,z.section},{x.group,y.group,z.group}},{x.chunk,y.chunk,z.chunk}},{x.block,y.block,z.block}};}
inline ChunkAddress chunkOf(const BlockAddress&a)noexcept{return a.chunk;} inline GroupAddress groupOf(const ChunkAddress&a)noexcept{return a.group;} inline GroupAddress groupOfBlock(const BlockAddress&a)noexcept{return a.chunk.group;} inline LocalBlockCoord localInChunk(const BlockAddress&a)noexcept{return a.block;} inline LocalChunkCoord localInGroup(const ChunkAddress&a)noexcept{return a.chunk;}
inline LocalGroupBlockCoord localBlockInGroup(const BlockAddress&a)noexcept{return{a.chunk.chunk.x*BLOCKS_PER_CHUNK_EDGE+a.block.x,a.chunk.chunk.y*BLOCKS_PER_CHUNK_EDGE+a.block.y,a.chunk.chunk.z*BLOCKS_PER_CHUNK_EDGE+a.block.z};}
inline BlockAddress chunkOrigin(const ChunkAddress&a)noexcept{return{a,{}};} inline ChunkAddress chunkAt(const GroupAddress&g,const LocalChunkCoord&c)noexcept{return{g,c};} inline BlockAddress blockAt(const ChunkAddress&c,const LocalBlockCoord&b)noexcept{return{c,b};}
inline bool tryOffsetBlock(const BlockAddress&b,std::int64_t dx,std::int64_t dy,std::int64_t dz,BlockAddress&out)noexcept{detail::AxisAddress x{},y{},z{};if(!detail::tryOffsetAxis(blockAxisX(b),dx,x)||!detail::tryOffsetAxis(blockAxisY(b),dy,y)||!detail::tryOffsetAxis(blockAxisZ(b),dz,z))return false;out=fromAxes(x,y,z);return true;}
inline BlockAddress offsetBlock(const BlockAddress&b,std::int64_t dx,std::int64_t dy,std::int64_t dz){BlockAddress o{};if(!tryOffsetBlock(b,dx,dy,dz,o))throw std::overflow_error("BlockAddress sector overflow");return o;}
inline bool tryOffsetChunk(const ChunkAddress&b,std::int64_t dx,std::int64_t dy,std::int64_t dz,ChunkAddress&out)noexcept{const auto bb=chunkOrigin(b);detail::AxisAddress x{},y{},z{};if(!detail::tryOffsetChunkAxis(blockAxisX(bb),dx,x)||!detail::tryOffsetChunkAxis(blockAxisY(bb),dy,y)||!detail::tryOffsetChunkAxis(blockAxisZ(bb),dz,z))return false;out=fromAxes(x,y,z).chunk;return true;}
inline ChunkAddress offsetChunk(const ChunkAddress&b,std::int64_t dx,std::int64_t dy,std::int64_t dz){ChunkAddress o{};if(!tryOffsetChunk(b,dx,dy,dz,o))throw std::overflow_error("ChunkAddress sector overflow");return o;}
inline bool tryOffsetGroup(const GroupAddress&b,std::int64_t dx,std::int64_t dy,std::int64_t dz,GroupAddress&out)noexcept{const auto bb=chunkOrigin({b,{}});detail::AxisAddress x{},y{},z{};if(!detail::tryOffsetGroupAxis(blockAxisX(bb),dx,x)||!detail::tryOffsetGroupAxis(blockAxisY(bb),dy,y)||!detail::tryOffsetGroupAxis(blockAxisZ(bb),dz,z))return false;out=fromAxes(x,y,z).chunk.group;return true;}
inline GroupAddress offsetGroup(const GroupAddress&b,std::int64_t dx,std::int64_t dy,std::int64_t dz){GroupAddress o{};if(!tryOffsetGroup(b,dx,dy,dz,o))throw std::overflow_error("GroupAddress sector overflow");return o;}
inline bool blockDeltaWithin(const BlockAddress&p,const BlockAddress&o,std::int64_t max,RelativeI64&d)noexcept{return detail::blockAxisDeltaWithin(blockAxisX(p),blockAxisX(o),max,d.x)&&detail::blockAxisDeltaWithin(blockAxisY(p),blockAxisY(o),max,d.y)&&detail::blockAxisDeltaWithin(blockAxisZ(p),blockAxisZ(o),max,d.z);}
inline bool chunkDeltaWithin(const ChunkAddress&p,const ChunkAddress&o,std::int64_t max,RelativeI64&d)noexcept{const auto pb=chunkOrigin(p),ob=chunkOrigin(o);return detail::chunkAxisDeltaWithin(blockAxisX(pb),blockAxisX(ob),max,d.x)&&detail::chunkAxisDeltaWithin(blockAxisY(pb),blockAxisY(ob),max,d.y)&&detail::chunkAxisDeltaWithin(blockAxisZ(pb),blockAxisZ(ob),max,d.z);}
inline bool groupDeltaWithin(const GroupAddress&p,const GroupAddress&o,std::int64_t max,RelativeI64&d)noexcept{const auto pb=chunkOrigin({p,{}}),ob=chunkOrigin({o,{}});return detail::groupAxisDeltaWithin(blockAxisX(pb),blockAxisX(ob),max,d.x)&&detail::groupAxisDeltaWithin(blockAxisY(pb),blockAxisY(ob),max,d.y)&&detail::groupAxisDeltaWithin(blockAxisZ(pb),blockAxisZ(ob),max,d.z);}
inline bool chunkWithinChebyshev(const ChunkAddress&a,const ChunkAddress&b,std::int64_t r)noexcept{RelativeI64 d{};return r>=0&&chunkDeltaWithin(a,b,r,d);}
inline std::uint32_t blockIndex(const LocalBlockCoord&l){if(!validLocal(l.x,BLOCKS_PER_CHUNK_EDGE)||!validLocal(l.y,BLOCKS_PER_CHUNK_EDGE)||!validLocal(l.z,BLOCKS_PER_CHUNK_EDGE))throw std::out_of_range("blockIndex requires canonical local block coordinate");return static_cast<std::uint32_t>((l.x*BLOCKS_PER_CHUNK_EDGE+l.y)*BLOCKS_PER_CHUNK_EDGE+l.z);}
inline std::uint32_t chunkIndex(const LocalChunkCoord&l){if(!validLocal(l.x,CHUNKS_PER_GROUP_EDGE)||!validLocal(l.y,CHUNKS_PER_GROUP_EDGE)||!validLocal(l.z,CHUNKS_PER_GROUP_EDGE))throw std::out_of_range("chunkIndex requires canonical local chunk coordinate");return static_cast<std::uint32_t>((l.x*CHUNKS_PER_GROUP_EDGE+l.y)*CHUNKS_PER_GROUP_EDGE+l.z);}
inline BlockAddress fromOriginOffset(std::int64_t x,std::int64_t y,std::int64_t z){return offsetBlock(originBlockAddress(),x,y,z);} inline BlockAddress withOriginRelativeY(const BlockAddress&c,std::int64_t y){const auto oy=fromOriginOffset(0,y,0);return fromAxes(blockAxisX(c),blockAxisY(oy),blockAxisZ(c));}
inline RelativeI64 chunkOriginRelativeToGroup(const ChunkAddress&c,const GroupAddress&r){RelativeI64 gd{};constexpr std::int64_t max=64;if(!groupDeltaWithin(c.group,r,max,gd))throw std::overflow_error("render-group origin is not local");return{gd.x*BLOCKS_PER_GROUP_EDGE+c.chunk.x*BLOCKS_PER_CHUNK_EDGE,gd.y*BLOCKS_PER_GROUP_EDGE+c.chunk.y*BLOCKS_PER_CHUNK_EDGE,gd.z*BLOCKS_PER_GROUP_EDGE+c.chunk.z*BLOCKS_PER_CHUNK_EDGE};}
} // namespace world
