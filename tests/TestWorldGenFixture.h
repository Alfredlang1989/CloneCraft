#pragma once
#include "world/registry/BlockIdTable.h"
#include "world/registry/Registry.h"
#include "world/worldgen/WorldGenConfig.h"
#include <filesystem>

namespace testfixture
{
    inline world::BlockRegistry blocks()
    {
        world::BlockRegistry r;
        world::BlockDef air; air.id="core:air"; air.displayName="Air"; air.transparent=true; air.opaque=false; r.insert(air);
        world::BlockDef stone; stone.id="core:stone"; stone.displayName="Stone"; stone.solid=true; stone.opaque=true; stone.tags={"terrain:rock","terrain:carvable"}; r.insert(stone);
        world::BlockDef grass; grass.id="core:grass"; grass.displayName="Grass"; grass.solid=true; grass.opaque=true; grass.tags={"terrain:soil"}; r.insert(grass);
        world::BlockDef flower; flower.id="core:flower"; flower.displayName="Flower"; flower.opaque=false; flower.transparent=true; flower.renderShape=world::BlockRenderShape::Cross; r.insert(flower);
        return r;
    }

    inline worldgen::WorldGenConfig config()
    {
        worldgen::WorldGenConfig c;
        c.seed=1337; c.workerThreads=2; c.surfaceField="height";
        c.stages.push_back( worldgen::StageConfig{ "terrain", 0 } );
        c.stages.push_back( worldgen::StageConfig{ "addon", 1 } );
        worldgen::FieldConfig height;
        height.id="height"; height.dimension=worldgen::FieldDimension::D2;
        height.scriptPath=std::filesystem::path(CLONECRAFT_TEST_DATA_DIR)/"simple_height.lua";
        height.functionName="sample"; height.salt=1;
        c.fields.push_back(height);
        worldgen::PassConfig stone;
        stone.id="stone"; stone.type=worldgen::PassType::FillBelow; stone.stage="terrain"; stone.blockId="core:stone";
        stone.field="height"; stone.priority=0;
        c.passes.push_back(stone);
        worldgen::PassConfig top;
        top.id="top"; top.type=worldgen::PassType::Surface; top.stage="terrain"; top.blockId="core:grass";
        top.field="height"; top.priority=10; top.replaceBlocks={"core:stone"};
        c.passes.push_back(top);
        return c;
    }
}
