#include "debug/DebugHudFormatter.h"

#include <iomanip>
#include <sstream>

namespace debug
{
    namespace
    {
        void addressRow( std::ostringstream &out, const char *label,
                         std::int64_t x, std::int64_t y, std::int64_t z )
        {
            constexpr int LABEL_WIDTH = 10;
            constexpr int AXIS_WIDTH = 21;
            out << std::left << std::setw( LABEL_WIDTH ) << label
                << std::right << std::setw( AXIS_WIDTH ) << x
                << std::setw( AXIS_WIDTH ) << y
                << std::setw( AXIS_WIDTH ) << z << '\n';
        }

        void fractionRow( std::ostringstream &out, const char *label,
                          float x, float y, float z )
        {
            constexpr int LABEL_WIDTH = 10;
            constexpr int AXIS_WIDTH = 21;
            out << std::left << std::setw( LABEL_WIDTH ) << label
                << std::right << std::setw( AXIS_WIDTH ) << x
                << std::setw( AXIS_WIDTH ) << y
                << std::setw( AXIS_WIDTH ) << z << '\n';
        }
    } // namespace

    std::string formatDebugHud( const DebugHudSnapshot &s )
    {
        std::ostringstream out;
        out.setf( std::ios::fixed );
        out << std::setprecision( 2 );
        out << "Omnigrid debug [F5]\n";
        out << "FPS: " << s.latestFps << "  avg: " << s.averageFps
            << "  frame: " << s.latestFrameMs << " ms (avg " << s.averageFrameMs << ")\n";
        out << "Render XYZ:  " << s.renderLocalX << " / " << s.renderLocalY << " / " << s.renderLocalZ << '\n';
        out << "Dynamic XYZ: " << s.dynamicLocalX << " / " << s.dynamicLocalY << " / " << s.dynamicLocalZ
            << "  edge: " << s.dynamicEdgeBlocks << " blocks\n\n";

        out << std::left << std::setw( 10 ) << "Address"
            << std::right << std::setw( 21 ) << "X"
            << std::setw( 21 ) << "Y"
            << std::setw( 21 ) << "Z" << '\n';
        addressRow( out, "Sector", s.sectorX, s.sectorY, s.sectorZ );
        addressRow( out, "Region", s.regionX, s.regionY, s.regionZ );
        addressRow( out, "Section", s.sectionX, s.sectionY, s.sectionZ );
        addressRow( out, "Group", s.groupX, s.groupY, s.groupZ );
        addressRow( out, "Chunk", s.chunkX, s.chunkY, s.chunkZ );
        addressRow( out, "Block", s.blockX, s.blockY, s.blockZ );
        fractionRow( out, "Sub", s.fractionX, s.fractionY, s.fractionZ );
        out << '\n';

        out << "Render anchor: sector " << s.renderSectorX << " / " << s.renderSectorY << " / " << s.renderSectorZ
            << "  region " << s.renderRegionX << " / " << s.renderRegionY << " / " << s.renderRegionZ
            << "  section " << s.renderSectionX << " / " << s.renderSectionY << " / " << s.renderSectionZ
            << "  group " << s.renderGroupX << " / " << s.renderGroupY << " / " << s.renderGroupZ
            << "  edge: " << s.groupEdgeBlocks << " blocks\n";

        if( !s.biomes.empty() )
        {
            out << "Biome: " << s.biomes.front().displayName << " (" << s.biomes.front().id << ")\n";
            out << "Biome weights:" << std::setprecision( 1 );
            for( const BiomeWeightLine &e : s.biomes )
                out << ' ' << e.displayName << '=' << ( e.weight * 100.0 ) << '%';
            out << "\n" << std::setprecision( 2 );
        }

        if( s.voxelLoaded )
            out << "Voxel here: " << s.voxelId << " (#" << s.voxelRuntimeId << ")\n";
        else
            out << "Voxel here: <unloaded>\n";

        if( s.targetPresent )
        {
            out << "Target: " << s.targetDisplayName << " (" << s.targetId << ", #" << s.targetRuntimeId
                << ")  distance: " << s.targetDistance << " blocks\n";
            out << "  S " << s.targetSectorX << '/' << s.targetSectorY << '/' << s.targetSectorZ
                << " R " << s.targetRegionX << '/' << s.targetRegionY << '/' << s.targetRegionZ
                << " Sec " << s.targetSectionX << '/' << s.targetSectionY << '/' << s.targetSectionZ
                << " G " << s.targetGroupX << '/' << s.targetGroupY << '/' << s.targetGroupZ
                << " C " << s.targetChunkX << '/' << s.targetChunkY << '/' << s.targetChunkZ
                << " B " << s.targetBlockX << '/' << s.targetBlockY << '/' << s.targetBlockZ << '\n';
            out << "  solid=" << ( s.targetSolid ? "yes" : "no" )
                << " opaque=" << ( s.targetOpaque ? "yes" : "no" )
                << " transparent=" << ( s.targetTransparent ? "yes" : "no" );
            if( !s.targetTexture.empty() )
                out << " texture=" << s.targetTexture;
            out << '\n';
            if( !s.targetTags.empty() )
            {
                out << "  tags:";
                for( const std::string &t : s.targetTags )
                    out << ' ' << t;
                out << '\n';
            }
        }
        else
        {
            out << "Target: <none within reach>\n";
        }

        out << "Loaded: " << s.loadedChunks << " chunks / " << s.loadedGroups
            << " groups  radius: " << s.streamingRadius << "  generated: " << s.generatedChunks
            << "  evicted: " << s.evictedChunks << "  queued: " << s.queuedChunks
            << "  ready: " << s.readyChunks << '\n';
        out << "Look: yaw " << s.yawDegrees << " deg  pitch " << s.pitchDegrees << " deg\n";
        out << "Flashlight: " << ( s.flashlightEnabled ? "ON" : "OFF" );
        return out.str();
    }
} // namespace debug
