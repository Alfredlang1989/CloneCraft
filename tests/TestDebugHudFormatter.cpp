#include "TestHarness.h"
#include "debug/DebugHudFormatter.h"

#include <string>

TEST_CASE(hud_prints_hierarchical_address_as_axis_table)
{
    debug::DebugHudSnapshot s;
    s.sectorX = 42;
    s.regionX = 3;
    s.sectionX = 7;
    s.groupX = 4;
    s.chunkX = 5;
    s.blockX = 6;
    s.sectorZ = -1;
    s.regionZ = 8999999999999999999LL;
    s.dynamicLocalX = 123.5f;
    s.dynamicEdgeBlocks = 65536;

    const std::string out = debug::formatDebugHud( s );
    CHECK( out.find( "Address" ) != std::string::npos );
    CHECK( out.find( "Sector" ) != std::string::npos );
    CHECK( out.find( "Region" ) != std::string::npos );
    CHECK( out.find( "Section" ) != std::string::npos );
    CHECK( out.find( "8999999999999999999" ) != std::string::npos );
    CHECK( out.find( "Dynamic XYZ: 123.50" ) != std::string::npos );
    CHECK( out.find( "edge: 65536 blocks" ) != std::string::npos );
}

TEST_CASE(hud_prints_hovered_block_information)
{
    debug::DebugHudSnapshot s;
    s.targetPresent = true;
    s.targetDisplayName = "Stone";
    s.targetId = "core:stone";
    s.targetRuntimeId = 7;
    s.targetDistance = 4.25;
    s.targetSolid = true;
    s.targetOpaque = true;
    s.targetTexture = "textures/stone.png";
    s.targetTags = { "terrain:rock", "terrain:carvable" };
    s.targetSectionX = 9;
    const std::string out = debug::formatDebugHud( s );
    CHECK( out.find( "Target: Stone (core:stone, #7)" ) != std::string::npos );
    CHECK( out.find( "distance: 4.25 blocks" ) != std::string::npos );
    CHECK( out.find( "Sec 9/0/0" ) != std::string::npos );
    CHECK( out.find( "texture=textures/stone.png" ) != std::string::npos );
    CHECK( out.find( "tags: terrain:rock terrain:carvable" ) != std::string::npos );
}

int main() { return test::runAll(); }
