cmake_minimum_required(VERSION 3.20)
project(Clonecraft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CLONECRAFT_CHUNK_EDGE 16 CACHE STRING "Blocks per chunk edge (e.g. 16 or 32)")
set(CLONECRAFT_GROUP_EDGE 16 CACHE STRING "Chunks per chunk-group edge")
set(CLONECRAFT_SECTION_EDGE 256 CACHE STRING "ChunkGroups per logical section edge")
set(CLONECRAFT_REGION_EDGE 9000000000000000000 CACHE STRING "Sections per logical region edge")
set(CLONECRAFT_SECTOR_EDGE 9000000000000000000 CACHE STRING "Regions per logical sector edge")
if(CLONECRAFT_CHUNK_EDGE LESS 1 OR CLONECRAFT_GROUP_EDGE LESS 1 OR
   CLONECRAFT_SECTION_EDGE LESS 1 OR CLONECRAFT_REGION_EDGE LESS 1 OR
   CLONECRAFT_SECTOR_EDGE LESS 1)
  message(FATAL_ERROR "all coordinate radices must be positive")
endif()
add_compile_definitions(
  CLONECRAFT_CHUNK_EDGE=${CLONECRAFT_CHUNK_EDGE}
  CLONECRAFT_GROUP_EDGE=${CLONECRAFT_GROUP_EDGE}
  CLONECRAFT_SECTION_EDGE=${CLONECRAFT_SECTION_EDGE}
  CLONECRAFT_REGION_EDGE=${CLONECRAFT_REGION_EDGE}
  CLONECRAFT_SECTOR_EDGE=${CLONECRAFT_SECTOR_EDGE})

option(CLONECRAFT_BUILD_APP "Build the SDL3/OgreNext graphical application" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# Header-only nlohmann/json is intentionally vendored. Clonecraft must build
# identically whether or not the host has a distro nlohmann-json package.
add_library(clonecraft_nlohmann_json INTERFACE)
target_include_directories(clonecraft_nlohmann_json INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
add_library(nlohmann_json::nlohmann_json ALIAS clonecraft_nlohmann_json)

if(CLONECRAFT_BUILD_APP)
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

add_library(clonecraft_config STATIC
  src/config/Settings.cpp)
target_include_directories(clonecraft_config PUBLIC src)
set_target_properties(clonecraft_config PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_config PUBLIC nlohmann_json::nlohmann_json)

add_library(clonecraft_world STATIC
  src/world/coordinates/WorldPosition.cpp
  src/world/coordinates/StickyGroupAnchor.cpp)
target_include_directories(clonecraft_world PUBLIC src)
set_target_properties(clonecraft_world PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_library(clonecraft_dynamic STATIC
  src/spatial/dynamic/DynamicSpace.cpp)
target_include_directories(clonecraft_dynamic PUBLIC src)
set_target_properties(clonecraft_dynamic PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_library(clonecraft_spatial_bridge STATIC
  src/spatial/bridge/WorldDynamicBridge.cpp)
target_include_directories(clonecraft_spatial_bridge PUBLIC src)
set_target_properties(clonecraft_spatial_bridge PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_spatial_bridge PUBLIC clonecraft_dynamic clonecraft_world)

add_library(clonecraft_ui STATIC
  src/ui/UiConfig.cpp)
target_include_directories(clonecraft_ui PUBLIC src)
set_target_properties(clonecraft_ui PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_ui PUBLIC nlohmann_json::nlohmann_json)

add_library(clonecraft_registry STATIC
  src/world/registry/BlockIdTable.cpp
  src/world/registry/RegistryLoader.cpp)
target_include_directories(clonecraft_registry PUBLIC src)
set_target_properties(clonecraft_registry PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_registry PUBLIC nlohmann_json::nlohmann_json)

find_library(CLONECRAFT_LUA_LIBRARY
  NAMES lua5.4 lua54 lua lua5.4.so.0 liblua5.4.so.0)
if(NOT CLONECRAFT_LUA_LIBRARY)
  message(FATAL_ERROR "Lua 5.4 runtime library not found (required for data-driven worldgen)")
endif()

add_library(clonecraft_worldgen STATIC
  src/world/worldgen/NoiseSource.cpp
  src/world/worldgen/MappedOpenSimplexNoise.cpp
  src/world/worldgen/LuaFieldEvaluator.cpp
  src/world/worldgen/WorldGen.cpp
  src/world/worldgen/WorldGenConfigLoader.cpp
  third_party/OpenSimplex2/OpenSimplex2S.cpp)
target_include_directories(clonecraft_worldgen PUBLIC src PRIVATE third_party)
set_target_properties(clonecraft_worldgen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_worldgen PUBLIC clonecraft_registry Threads::Threads PRIVATE "${CLONECRAFT_LUA_LIBRARY}")

add_library(clonecraft_chunk STATIC
  src/world/chunk/ChunkManager.cpp
  src/world/chunk/ChunkGroup.cpp
  src/world/chunk/ChunkStreamingManager.cpp)
target_include_directories(clonecraft_chunk PUBLIC src)
set_target_properties(clonecraft_chunk PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_chunk PUBLIC clonecraft_world clonecraft_worldgen)

add_library(clonecraft_interaction STATIC
  src/world/interaction/BlockPicker.cpp)
target_include_directories(clonecraft_interaction PUBLIC src)
set_target_properties(clonecraft_interaction PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_interaction PUBLIC clonecraft_chunk)

add_library(clonecraft_camera STATIC
  src/camera/FreeCameraController.cpp)
target_include_directories(clonecraft_camera PUBLIC src)
set_target_properties(clonecraft_camera PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_camera PUBLIC clonecraft_dynamic)

add_library(clonecraft_mesh STATIC
  src/world/mesh/ChunkMeshBuilder.cpp)
target_include_directories(clonecraft_mesh PUBLIC src)
set_target_properties(clonecraft_mesh PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(clonecraft_mesh PUBLIC clonecraft_chunk)

add_library(clonecraft_debug STATIC
  src/debug/DebugHudFormatter.cpp)
target_include_directories(clonecraft_debug PUBLIC src)
set_target_properties(clonecraft_debug PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

if(CLONECRAFT_BUILD_APP)
  add_executable(clonecraft
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

  target_include_directories(clonecraft PRIVATE src
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Common")

  target_compile_definitions(clonecraft PRIVATE
    OGRE_NEXT_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    OGRE_NEXT_MEDIA_DIR="${OGRE_NEXT_MEDIA_DIR}"
    OGRE_NEXT_HLMS_DIR="${OGRE_NEXT_HLMS_DIR}")

  target_link_libraries(clonecraft PRIVATE
    SDL3::SDL3
    PkgConfig::OGRE_NEXT
    PkgConfig::OGRE_NEXT_HLMS
    PkgConfig::OGRE_NEXT_OVERLAY
    clonecraft_world
    clonecraft_config
    clonecraft_debug
    clonecraft_ui
    clonecraft_mesh
    clonecraft_chunk
    clonecraft_interaction
    clonecraft_camera
    clonecraft_dynamic
    clonecraft_spatial_bridge)
endif()

enable_testing()

add_executable(test_settings tests/TestSettings.cpp)
target_include_directories(test_settings PRIVATE src)
target_link_libraries(test_settings PRIVATE clonecraft_config)
add_test(NAME settings COMMAND test_settings)

add_executable(test_coordinates tests/TestCoordinates.cpp)
target_link_libraries(test_coordinates PRIVATE clonecraft_world)
add_test(NAME coordinates COMMAND test_coordinates)

add_executable(test_registries tests/TestRegistries.cpp)
target_include_directories(test_registries PRIVATE src)
target_compile_definitions(test_registries PRIVATE CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_registries PRIVATE clonecraft_registry)
add_test(NAME registries COMMAND test_registries)

add_executable(test_worldgen tests/TestWorldGen.cpp)
target_include_directories(test_worldgen PRIVATE src)
target_compile_definitions(test_worldgen PRIVATE
  CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  CLONECRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(test_worldgen PRIVATE clonecraft_worldgen)
add_test(NAME worldgen COMMAND test_worldgen)

add_executable(test_chunk tests/TestChunk.cpp)
target_include_directories(test_chunk PRIVATE src)
target_compile_definitions(test_chunk PRIVATE CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_chunk PRIVATE clonecraft_chunk)
add_test(NAME chunk COMMAND test_chunk)

add_executable(test_streaming tests/TestStreaming.cpp)
target_include_directories(test_streaming PRIVATE src)
target_compile_definitions(test_streaming PRIVATE
  CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  CLONECRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(test_streaming PRIVATE clonecraft_chunk)
add_test(NAME streaming COMMAND test_streaming)

add_executable(test_chunkmesh tests/TestChunkMesh.cpp)
target_include_directories(test_chunkmesh PRIVATE src)
target_compile_definitions(test_chunkmesh PRIVATE CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_chunkmesh PRIVATE clonecraft_mesh)
add_test(NAME chunkmesh COMMAND test_chunkmesh)

add_executable(test_debug_hud tests/TestDebugHudFormatter.cpp)
target_link_libraries(test_debug_hud PRIVATE clonecraft_debug)
add_test(NAME debug_hud COMMAND test_debug_hud)

add_executable(test_worldgen_config tests/TestWorldGenConfig.cpp)
target_link_libraries(test_worldgen_config PRIVATE clonecraft_worldgen)
add_test(NAME worldgen_config COMMAND test_worldgen_config)

add_executable(test_worldgen_features tests/TestMappedOpenSimplexNoise.cpp)
target_include_directories(test_worldgen_features PRIVATE src third_party)
target_link_libraries(test_worldgen_features PRIVATE clonecraft_worldgen)
add_test(NAME worldgen_features COMMAND test_worldgen_features)

add_executable(test_worldgen_morphology tests/TestWorldGenMorphology.cpp)
target_include_directories(test_worldgen_morphology PRIVATE src third_party)
target_compile_definitions(test_worldgen_morphology PRIVATE CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_worldgen_morphology PRIVATE clonecraft_worldgen)
add_test(NAME worldgen_morphology COMMAND test_worldgen_morphology)

add_executable(test_camera tests/TestCamera.cpp)
target_link_libraries(test_camera PRIVATE clonecraft_camera)
add_test(NAME camera COMMAND test_camera)

add_executable(test_dynamic_space tests/TestDynamicSpace.cpp)
target_link_libraries(test_dynamic_space PRIVATE clonecraft_dynamic clonecraft_spatial_bridge)
add_test(NAME dynamic_space COMMAND test_dynamic_space)

add_executable(test_ui_config tests/TestUiConfig.cpp)
target_include_directories(test_ui_config PRIVATE src)
target_compile_definitions(test_ui_config PRIVATE CLONECRAFT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_ui_config PRIVATE clonecraft_ui)
add_test(NAME ui_config COMMAND test_ui_config)

add_executable(test_block_picker tests/TestBlockPicker.cpp)
target_include_directories(test_block_picker PRIVATE src)
target_link_libraries(test_block_picker PRIVATE clonecraft_interaction)
add_test(NAME block_picker COMMAND test_block_picker)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(architecture_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/architecture_check.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --report ${CMAKE_CURRENT_BINARY_DIR}/architecture-report.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking Clonecraft module dependency architecture")

  find_program(CLONECRAFT_CLANG_TIDY NAMES
    clang-tidy clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15 clang-tidy-14)
  if(CLONECRAFT_CLANG_TIDY)
    add_custom_target(static_analysis
      COMMAND ${CMAKE_COMMAND} -E env CLANG_TIDY=${CLONECRAFT_CLANG_TIDY}
              ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_static_analysis.py
              --root ${CMAKE_CURRENT_SOURCE_DIR}
              --build-dir ${CMAKE_CURRENT_BINARY_DIR}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running clang-tidy AST/static analysis")
  endif()
endif()
