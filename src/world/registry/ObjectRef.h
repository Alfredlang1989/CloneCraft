#pragma once

#include "world/coordinates/Coords.h"
#include "world/registry/Registry.h"

#include <string>

namespace world
{
    /**
     * Generic logical reference to a world object.
     *
     * This is the M03 foundation of the unified world-state API (M05):
     * the *identity* of an object is a stable namespaced prototype id
     * (e.g. "default:cactus"), while its *location* is a physical
     * BlockAddress. Voxel storage stays physical (block index); gameplay
     * stays logical (prototype). M04+ will attach sidecar state to the
     * ref, M05 the get/set/has/emit/call surface.
     */
    struct WorldObjectRef
    {
        BlockAddress position;  // physical location
        std::string prototypeId; // logical identity, e.g. "default:cactus"
    };

    /**
     * Bridges physical blocks back to logical prototypes: returns the
     * prototype whose blockId matches `blockId`, or nullptr when the block
     * has no prototype (most blocks are pure scenery and stay unreferenced).
     */
    inline const PrototypeDef *prototypeForBlock( const PrototypeRegistry &prototypes,
                                                  const std::string &blockId )
    {
        for( const std::string &id : prototypes.ids() )
        {
            const PrototypeDef &def = prototypes.get( id );
            if( def.blockId == blockId )
                return &def;
        }
        return nullptr;
    }
} // namespace world
