#include "TestHarness.h"
#include "debug/DebugHudFormatter.h"
#include <string>

TEST_CASE(hud_prints_hierarchical_address)
{
    debug::DebugHudSnapshot s; s.sectorX=42; s.regionX=3; s.groupX=4; s.chunkX=5; s.blockX=6;
    const std::string out=debug::formatDebugHud(s);
    CHECK(out.find("Sector: 42")!=std::string::npos);
    CHECK(out.find("Region local: 3")!=std::string::npos);
    CHECK(out.find("Chunk local: 5")!=std::string::npos);
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
    const std::string out = debug::formatDebugHud( s );
    CHECK( out.find( "Target: Stone (core:stone, #7)" ) != std::string::npos );
    CHECK( out.find( "distance: 4.25 blocks" ) != std::string::npos );
    CHECK( out.find( "texture=textures/stone.png" ) != std::string::npos );
    CHECK( out.find( "tags: terrain:rock terrain:carvable" ) != std::string::npos );
}
TEST_CASE(hud_prints_temporary_v16_comparison_coordinates)
{
    debug::DebugHudSnapshot s;
    s.comparisonGlobalX = "123456";
    s.comparisonGlobalY = "64";
    s.comparisonGlobalZ = "-987654";
    s.targetPresent = true;
    s.targetDisplayName = "Water";
    s.targetId = "core:water";
    s.targetComparisonGlobalX = "123460";
    s.targetComparisonGlobalY = "61";
    s.targetComparisonGlobalZ = "-987650";

    const std::string out = debug::formatDebugHud( s );
    CHECK( out.find( "Global block XYZ [TEMP v16 compare]: 123456 / 64 / -987654" ) != std::string::npos );
    CHECK( out.find( "Global target XYZ [TEMP]: 123460 / 61 / -987650" ) != std::string::npos );
}

int main(){return test::runAll();}
