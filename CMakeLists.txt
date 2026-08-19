cmake_minimum_required(VERSION 3.20)
project(Omnigrid VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(OMNIGRID_CHUNK_EDGE 16 CACHE STRING "Blocks per chunk edge (e.g. 16 or 32)")
set(OMNIGRID_GROUP_EDGE 16 CACHE STRING "Chunks per chunk-group edge")
set(OMNIGRID_SECTION_EDGE 256 CACHE STRING "ChunkGroups per logical section edge")
set(OMNIGRID_REGION_EDGE 9000000000000000000 CACHE STRING "Sections per logical region edge")
set(OMNIGRID_SECTOR_EDGE 9000000000000000000 CACHE STRING "Regions per logical sector edge")
if(OMNIGRID_CHUNK_EDGE LESS 1 OR OMNIGRID_GROUP_EDGE LESS 1 OR
   OMNIGRID_SECTION_EDGE LESS 1 OR OMNIGRID_REGION_EDGE LESS 1 OR
   OMNIGRID_SECTOR_EDGE LESS 1)
  message(FATAL_ERROR "all coordinate radices must be positive")
endif()
add_compile_definitions(
  OMNIGRID_CHUNK_EDGE=${OMNIGRID_CHUNK_EDGE}
  OMNIGRID_GROUP_EDGE=${OMNIGRID_GROUP_EDGE}
  OMNIGRID_SECTION_EDGE=${OMNIGRID_SECTION_EDGE}
  OMNIGRID_REGION_EDGE=${OMNIGRID_REGION_EDGE}
  OMNIGRID_SECTOR_EDGE=${OMNIGRID_SECTOR_EDGE})

option(OMNIGRID_BUILD_APP "Build the SDL3/OgreNext graphical application" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# Header-only nlohmann/json is intentionally vendored. Omnigrid must build
# identically whether or not the host has a distro nlohmann-json package.
add_library(omnigrid_nlohmann_json INTERFACE)
target_include_directories(omnigrid_nlohmann_json INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
add_library(nlohmann_json::nlohmann_json ALIAS omnigrid_nlohmann_json)

if(OMNIGRID_BUILD_APP)
  find_package(SDL3 3.0 REQUIRED)

  find_package(PkgConfig REQUIRED)
  pkg_check_modules(OGRE_NEXT REQUIRED IMPORTED_TARGET OGRE-Next)
  pkg_check_modules(OGRE_NEXT_HLMS REQUIRED IMPORTED_TARGET OGRE-Next-Hlms)
  pkg_check_modules(OGRE_NEXT_OVERLAY REQUIRED IMPORTED_TARGET OGRE-Next-Overlay)

  pkg_get_variable(OGRE_NEXT_PREFIX OGRE-Next prefix)
  set(OGRE_NEXT_MEDIA_DIR "${OGRE_NEXT_PREFIX}/share/OGRE-Next/Media"
      CACHE STRING "OgreNext Media root (HLMS, debug packs and overlay resources)")
  set(OGRE_NEXT_HLMS_DIR "${OGRE_NEXT_MEDIA_DIR}/Hlms"
      CACHE STRING "Directory containing the OGRE-Next HLMS shader sources")

  execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=libdir OGRE-Next
    OUTPUT_VARIABLE OGRE_NEXT_LIBDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(OGRE_NEXT_PLUGIN_DIR "${OGRE_NEXT_LIBDIR}/OGRE-Next"
      CACHE STRING "Directory containing OgreNext render system plugins")

  if(NOT EXISTS "${OGRE_NEXT_PLUGIN_DIR}/RenderSystem_GL3Plus.so")
    message(FATAL_ERROR "RenderSystem_GL3Plus.so not found in ${OGRE_NEXT_PLUGIN_DIR}")
  endif()
endif()

add_library(omnigrid_config STATIC
  src/config/Settings.cpp
  src/config/ContentRoot.cpp)
target_include_directories(omnigrid_config PUBLIC src)
set_target_properties(omnigrid_config PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_config PUBLIC nlohmann_json::nlohmann_json)

add_library(omnigrid_world STATIC
  src/world/coordinates/WorldPosition.cpp
  src/world/coordinates/StickyGroupAnchor.cpp)
target_include_directories(omnigrid_world PUBLIC src)
set_target_properties(omnigrid_world PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_library(omnigrid_dynamic STATIC
  src/spatial/dynamic/DynamicSpace.cpp)
target_include_directories(omnigrid_dynamic PUBLIC src)
set_target_properties(omnigrid_dynamic PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_library(omnigrid_spatial_bridge STATIC
  src/spatial/bridge/WorldDynamicBridge.cpp)
target_include_directories(omnigrid_spatial_bridge PUBLIC src)
set_target_properties(omnigrid_spatial_bridge PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_spatial_bridge PUBLIC omnigrid_dynamic omnigrid_world)

add_library(omnigrid_ui STATIC
  src/ui/UiConfig.cpp)
target_include_directories(omnigrid_ui PUBLIC src)
set_target_properties(omnigrid_ui PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_ui PUBLIC nlohmann_json::nlohmann_json)

add_library(omnigrid_registry STATIC
  src/world/registry/BlockIdTable.cpp
  src/world/registry/RegistryLoader.cpp
  src/world/registry/PrototypeIdTable.cpp)
target_include_directories(omnigrid_registry PUBLIC src)
set_target_properties(omnigrid_registry PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_registry PUBLIC nlohmann_json::nlohmann_json)

find_library(OMNIGRID_LUA_LIBRARY
  NAMES lua5.4 lua54 lua lua5.4.so.0 liblua5.4.so.0)
if(NOT OMNIGRID_LUA_LIBRARY)
  message(FATAL_ERROR "Lua 5.4 runtime library not found (required for data-driven worldgen)")
endif()

add_library(omnigrid_worldgen STATIC
  src/world/worldgen/NoiseSource.cpp
  src/world/worldgen/MappedOpenSimplexNoise.cpp
  src/world/worldgen/LuaFieldEvaluator.cpp
  src/world/worldgen/WorldGen.cpp
  src/world/worldgen/WorldGenConfigLoader.cpp
  third_party/OpenSimplex2/OpenSimplex2S.cpp)
target_include_directories(omnigrid_worldgen PUBLIC src PRIVATE third_party)
set_target_properties(omnigrid_worldgen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_worldgen PUBLIC omnigrid_registry Threads::Threads PRIVATE "${OMNIGRID_LUA_LIBRARY}")

add_library(omnigrid_chunk STATIC
  src/world/chunk/ChunkManager.cpp
  src/world/chunk/ChunkGroup.cpp
  src/world/chunk/ChunkStreamingManager.cpp)
target_include_directories(omnigrid_chunk PUBLIC src)
set_target_properties(omnigrid_chunk PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_chunk PUBLIC omnigrid_world omnigrid_worldgen)

add_library(omnigrid_interaction STATIC
  src/world/interaction/BlockPicker.cpp
  src/world/interaction/PlayerInteractionController.cpp)
target_include_directories(omnigrid_interaction PUBLIC src)
set_target_properties(omnigrid_interaction PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_interaction PUBLIC omnigrid_chunk omnigrid_communication)

add_library(omnigrid_state STATIC
  src/world/state/WorldState.cpp
  src/world/state/HierarchySidecarStore.cpp
  src/world/state/MemoryPersistenceSink.cpp)
target_include_directories(omnigrid_state PUBLIC src)
set_target_properties(omnigrid_state PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_state PUBLIC omnigrid_chunk omnigrid_registry)

add_library(omnigrid_camera STATIC
  src/camera/FreeCameraController.cpp)
target_include_directories(omnigrid_camera PUBLIC src)
set_target_properties(omnigrid_camera PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_camera PUBLIC omnigrid_dynamic)

add_library(omnigrid_mesh STATIC
  src/world/mesh/ChunkMeshBuilder.cpp)
target_include_directories(omnigrid_mesh PUBLIC src)
set_target_properties(omnigrid_mesh PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_mesh PUBLIC omnigrid_chunk)

add_library(omnigrid_communication STATIC
  src/world/communication/CommunicationEnvelope.cpp
  src/world/communication/CommunicationRouter.cpp
  src/world/communication/CommunicationRegistries.cpp
  src/world/communication/CommunicationRuntime.cpp
  src/world/communication/DelayedMessageScheduler.cpp
  src/world/communication/BlockCommandHandlers.cpp)
target_include_directories(omnigrid_communication PUBLIC src)
set_target_properties(omnigrid_communication PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_communication PUBLIC omnigrid_state)

add_library(omnigrid_scripting STATIC
  src/world/scripting/GameplayLuaRuntime.cpp
  src/world/scripting/GameplayContentRuntime.cpp)
target_include_directories(omnigrid_scripting PUBLIC src)
set_target_properties(omnigrid_scripting PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(omnigrid_scripting PUBLIC omnigrid_communication omnigrid_worldgen PRIVATE "${OMNIGRID_LUA_LIBRARY}")

add_library(omnigrid_debug STATIC
  src/debug/DebugHudFormatter.cpp)
target_include_directories(omnigrid_debug PUBLIC src)
set_target_properties(omnigrid_debug PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

if(OMNIGRID_BUILD_APP)
  add_executable(omnigrid
    src/main.cpp
    src/app/Application.cpp
    src/core/Logging.cpp
    src/input/InputManager.cpp
    src/platform/PlatformWindowBridge.cpp
    src/render/BlockSelectionRenderer.cpp
    src/render/ChunkWorldRenderer.cpp
    src/render/CrosshairOverlay.cpp
    src/render/DebugOverlay.cpp
    src/render/OgreRenderer.cpp)

  target_include_directories(omnigrid PRIVATE src
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Common")

  target_compile_definitions(omnigrid PRIVATE
    OGRE_NEXT_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    OGRE_NEXT_MEDIA_DIR="${OGRE_NEXT_MEDIA_DIR}"
    OGRE_NEXT_HLMS_DIR="${OGRE_NEXT_HLMS_DIR}")

  target_link_libraries(omnigrid PRIVATE
    SDL3::SDL3
    PkgConfig::OGRE_NEXT
    PkgConfig::OGRE_NEXT_HLMS
    PkgConfig::OGRE_NEXT_OVERLAY
    omnigrid_world
    omnigrid_config
    omnigrid_debug
    omnigrid_ui
    omnigrid_mesh
    omnigrid_chunk
    omnigrid_state
    omnigrid_interaction
    omnigrid_camera
    omnigrid_dynamic
    omnigrid_spatial_bridge
    omnigrid_scripting)

  # Content root contract: the app resolves MODS next to its own binary
  # first (see config::contentRootCandidates). Keep a symlink in the build
  # directory so the game finds its content regardless of the launching cwd.
  if(UNIX AND NOT APPLE)
    add_custom_command(TARGET omnigrid POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -f
              $<TARGET_FILE_DIR:omnigrid>/MODS
      COMMAND ${CMAKE_COMMAND} -E create_symlink
              "${CMAKE_CURRENT_SOURCE_DIR}/MODS"
              $<TARGET_FILE_DIR:omnigrid>/MODS
      COMMENT "Symlinking MODS content root next to omnigrid binary")
  endif()
endif()

enable_testing()

add_executable(test_settings tests/TestSettings.cpp)
target_include_directories(test_settings PRIVATE src)
target_link_libraries(test_settings PRIVATE omnigrid_config)
add_test(NAME settings COMMAND test_settings)

add_executable(test_content_root tests/TestContentRoot.cpp)
target_include_directories(test_content_root PRIVATE src)
target_link_libraries(test_content_root PRIVATE omnigrid_config)
add_test(NAME content_root COMMAND test_content_root)

add_executable(test_coordinates tests/TestCoordinates.cpp)
target_link_libraries(test_coordinates PRIVATE omnigrid_world)
add_test(NAME coordinates COMMAND test_coordinates)

add_executable(test_registries tests/TestRegistries.cpp)
target_include_directories(test_registries PRIVATE src)
target_compile_definitions(test_registries PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_registries PRIVATE omnigrid_registry)
add_test(NAME registries COMMAND test_registries)

add_executable(test_prototypes tests/TestPrototypes.cpp)
target_include_directories(test_prototypes PRIVATE src)
target_compile_definitions(test_prototypes PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_prototypes PRIVATE omnigrid_registry)
add_test(NAME prototypes COMMAND test_prototypes)

add_executable(test_sidecars tests/TestSidecars.cpp)
target_include_directories(test_sidecars PRIVATE src)
target_compile_definitions(test_sidecars PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_sidecars PRIVATE omnigrid_chunk)
add_test(NAME sidecars COMMAND test_sidecars)

add_executable(test_world_state tests/TestWorldState.cpp)
target_include_directories(test_world_state PRIVATE src)
target_compile_definitions(test_world_state PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_world_state PRIVATE omnigrid_state)
add_test(NAME world_state COMMAND test_world_state)

add_executable(test_hierarchy_sidecars tests/TestHierarchySidecars.cpp)
target_include_directories(test_hierarchy_sidecars PRIVATE src)
target_compile_definitions(test_hierarchy_sidecars PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_hierarchy_sidecars PRIVATE omnigrid_state)
add_test(NAME hierarchy_sidecars COMMAND test_hierarchy_sidecars)

add_executable(test_communication tests/TestCommunication.cpp)
target_include_directories(test_communication PRIVATE src)
target_link_libraries(test_communication PRIVATE omnigrid_communication)
add_test(NAME communication COMMAND test_communication)

add_executable(test_router tests/TestRouter.cpp)
target_include_directories(test_router PRIVATE src)
target_compile_definitions(test_router PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_router PRIVATE omnigrid_communication omnigrid_state)
add_test(NAME router COMMAND test_router)

add_executable(test_m03_router tests/TestM03Router.cpp)
target_include_directories(test_m03_router PRIVATE src)
target_link_libraries(test_m03_router PRIVATE omnigrid_communication)
add_test(NAME m03_router COMMAND test_m03_router)

add_executable(test_m03_scheduler tests/TestM03Scheduler.cpp)
target_include_directories(test_m03_scheduler PRIVATE src)
target_link_libraries(test_m03_scheduler PRIVATE omnigrid_communication)
add_test(NAME m03_scheduler COMMAND test_m03_scheduler)

add_executable(test_m03_gameplay_lua tests/TestM03GameplayLua.cpp)
target_include_directories(test_m03_gameplay_lua PRIVATE src)
target_link_libraries(test_m03_gameplay_lua PRIVATE omnigrid_scripting omnigrid_state omnigrid_chunk)
add_test(NAME m03_gameplay_lua COMMAND test_m03_gameplay_lua)

add_executable(test_m03_round4 tests/TestM03Round4.cpp)
target_include_directories(test_m03_round4 PRIVATE src)
target_compile_definitions(test_m03_round4 PRIVATE
  OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_m03_round4 PRIVATE omnigrid_scripting omnigrid_state omnigrid_chunk)
add_test(NAME m03_round4 COMMAND test_m03_round4)

add_executable(test_interaction_flow tests/TestInteractionFlow.cpp)
target_include_directories(test_interaction_flow PRIVATE src)
target_link_libraries(test_interaction_flow PRIVATE omnigrid_communication omnigrid_interaction omnigrid_state)
add_test(NAME interaction_flow COMMAND test_interaction_flow)

add_executable(test_worldgen tests/TestWorldGen.cpp)
target_include_directories(test_worldgen PRIVATE src)
target_compile_definitions(test_worldgen PRIVATE
  OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default"
  OMNIGRID_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(test_worldgen PRIVATE omnigrid_worldgen)
add_test(NAME worldgen COMMAND test_worldgen)

add_executable(test_chunk tests/TestChunk.cpp)
target_include_directories(test_chunk PRIVATE src)
target_compile_definitions(test_chunk PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_chunk PRIVATE omnigrid_chunk)
add_test(NAME chunk COMMAND test_chunk)

add_executable(test_streaming tests/TestStreaming.cpp)
target_include_directories(test_streaming PRIVATE src)
target_compile_definitions(test_streaming PRIVATE
  OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default"
  OMNIGRID_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(test_streaming PRIVATE omnigrid_chunk)
add_test(NAME streaming COMMAND test_streaming)

add_executable(test_chunkmesh tests/TestChunkMesh.cpp)
target_include_directories(test_chunkmesh PRIVATE src)
target_compile_definitions(test_chunkmesh PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_chunkmesh PRIVATE omnigrid_mesh)
add_test(NAME chunkmesh COMMAND test_chunkmesh)

add_executable(test_debug_hud tests/TestDebugHudFormatter.cpp)
target_link_libraries(test_debug_hud PRIVATE omnigrid_debug)
add_test(NAME debug_hud COMMAND test_debug_hud)

add_executable(test_worldgen_config tests/TestWorldGenConfig.cpp)
target_link_libraries(test_worldgen_config PRIVATE omnigrid_worldgen)
add_test(NAME worldgen_config COMMAND test_worldgen_config)

add_executable(test_worldgen_features tests/TestMappedOpenSimplexNoise.cpp)
target_include_directories(test_worldgen_features PRIVATE src third_party)
target_link_libraries(test_worldgen_features PRIVATE omnigrid_worldgen)
add_test(NAME worldgen_features COMMAND test_worldgen_features)

add_executable(test_worldgen_morphology tests/TestWorldGenMorphology.cpp)
target_include_directories(test_worldgen_morphology PRIVATE src third_party)
target_compile_definitions(test_worldgen_morphology PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_worldgen_morphology PRIVATE omnigrid_worldgen)
add_test(NAME worldgen_morphology COMMAND test_worldgen_morphology)

add_executable(test_biome_distribution tests/TestBiomeDistribution.cpp)
target_include_directories(test_biome_distribution PRIVATE src third_party)
target_compile_definitions(test_biome_distribution PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_biome_distribution PRIVATE omnigrid_worldgen)
add_test(NAME biome_distribution COMMAND test_biome_distribution)

add_executable(test_camera tests/TestCamera.cpp)
target_link_libraries(test_camera PRIVATE omnigrid_camera)
add_test(NAME camera COMMAND test_camera)

add_executable(test_dynamic_space tests/TestDynamicSpace.cpp)
target_link_libraries(test_dynamic_space PRIVATE omnigrid_dynamic omnigrid_spatial_bridge)
add_test(NAME dynamic_space COMMAND test_dynamic_space)

add_executable(test_ui_config tests/TestUiConfig.cpp)
target_include_directories(test_ui_config PRIVATE src)
target_compile_definitions(test_ui_config PRIVATE OMNIGRID_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/MODS/Default")
target_link_libraries(test_ui_config PRIVATE omnigrid_ui)
add_test(NAME ui_config COMMAND test_ui_config)

add_executable(test_block_picker tests/TestBlockPicker.cpp)
target_include_directories(test_block_picker PRIVATE src)
target_link_libraries(test_block_picker PRIVATE omnigrid_interaction)
add_test(NAME block_picker COMMAND test_block_picker)
set_tests_properties(block_picker PROPERTIES TIMEOUT 60)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(architecture_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/architecture_check.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --report ${CMAKE_CURRENT_BINARY_DIR}/architecture-report.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking Omnigrid module dependency architecture")

  find_program(OMNIGRID_CLANG_TIDY NAMES
    clang-tidy clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15 clang-tidy-14)
  if(OMNIGRID_CLANG_TIDY)
    add_custom_target(static_analysis
      COMMAND ${CMAKE_COMMAND} -E env CLANG_TIDY=${OMNIGRID_CLANG_TIDY}
              ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_static_analysis.py
              --root ${CMAKE_CURRENT_SOURCE_DIR}
              --build-dir ${CMAKE_CURRENT_BINARY_DIR}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running clang-tidy AST/static analysis")
  endif()
endif()

# Developer diagnostics. Keep these independent of the SDL/Ogre application.
option(OMNIGRID_BUILD_TOOLS "Build OmniGrid developer diagnostic tools" ON)
if(OMNIGRID_BUILD_TOOLS)
  find_package(ZLIB REQUIRED)
  add_executable(omnigrid_biomemap tools/BiomeMap.cpp)
  target_include_directories(omnigrid_biomemap PRIVATE src)
  target_link_libraries(omnigrid_biomemap PRIVATE omnigrid_worldgen ZLIB::ZLIB)
endif()
