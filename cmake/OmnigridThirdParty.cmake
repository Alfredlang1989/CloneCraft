# Third-party dependency boundary for OmniGrid.
#
# EnTT is vendored and deterministic. RocksDB is deliberately host-provided;
# this module exposes one stable target without downloading or building it.

set(OMNIGRID_ENTT_VERSION "3.16.0")
set(OMNIGRID_ENTT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt")
set(OMNIGRID_ENTT_HEADER "${OMNIGRID_ENTT_ROOT}/entt/entt.hpp")

if(NOT EXISTS "${OMNIGRID_ENTT_HEADER}")
  message(FATAL_ERROR
    "Vendored EnTT ${OMNIGRID_ENTT_VERSION} is missing: ${OMNIGRID_ENTT_HEADER}")
endif()

add_library(omnigrid_entt INTERFACE)
target_include_directories(omnigrid_entt SYSTEM INTERFACE "${OMNIGRID_ENTT_ROOT}")
target_compile_features(omnigrid_entt INTERFACE cxx_std_20)
add_library(EnTT::EnTT ALIAS omnigrid_entt)

set(OMNIGRID_ROCKSDB_MIN_VERSION "8.9.0" CACHE STRING
    "Minimum supported host RocksDB development version")
option(OMNIGRID_REQUIRE_ROCKSDB
       "Fail configuration unless the host RocksDB development package exists"
       OFF)

set(OMNIGRID_ROCKSDB_AVAILABLE FALSE)
set(OMNIGRID_ROCKSDB_VERSION "unknown")
set(_omnigrid_rocksdb_target "")

find_package(RocksDB ${OMNIGRID_ROCKSDB_MIN_VERSION} CONFIG QUIET)
foreach(_candidate
        RocksDB::rocksdb
        RocksDB::rocksdb-shared
        RocksDB::rocksdb_static
        rocksdb)
  if(TARGET "${_candidate}" AND NOT _omnigrid_rocksdb_target)
    set(_omnigrid_rocksdb_target "${_candidate}")
  endif()
endforeach()

if(_omnigrid_rocksdb_target)
  if(DEFINED RocksDB_VERSION)
    set(OMNIGRID_ROCKSDB_VERSION "${RocksDB_VERSION}")
  elseif(DEFINED ROCKSDB_VERSION)
    set(OMNIGRID_ROCKSDB_VERSION "${ROCKSDB_VERSION}")
  endif()
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(OMNIGRID_HOST_ROCKSDB QUIET IMPORTED_TARGET
      "rocksdb>=${OMNIGRID_ROCKSDB_MIN_VERSION}")
    if(TARGET PkgConfig::OMNIGRID_HOST_ROCKSDB)
      set(_omnigrid_rocksdb_target PkgConfig::OMNIGRID_HOST_ROCKSDB)
      set(OMNIGRID_ROCKSDB_VERSION "${OMNIGRID_HOST_ROCKSDB_VERSION}")
    endif()
  endif()
endif()

if(NOT _omnigrid_rocksdb_target)
  find_path(OMNIGRID_ROCKSDB_INCLUDE_DIR NAMES rocksdb/db.h)
  find_library(OMNIGRID_ROCKSDB_LIBRARY NAMES rocksdb)

  if(OMNIGRID_ROCKSDB_INCLUDE_DIR AND OMNIGRID_ROCKSDB_LIBRARY)
    set(_version_header "${OMNIGRID_ROCKSDB_INCLUDE_DIR}/rocksdb/version.h")
    if(EXISTS "${_version_header}")
      file(STRINGS "${_version_header}" _version_lines
           REGEX "^#define ROCKSDB_(MAJOR|MINOR|PATCH) [0-9]+$")
      foreach(_part MAJOR MINOR PATCH)
        foreach(_line IN LISTS _version_lines)
          if(_line MATCHES "^#define ROCKSDB_${_part} ([0-9]+)$")
            set(_rocksdb_${_part} "${CMAKE_MATCH_1}")
          endif()
        endforeach()
      endforeach()
      if(DEFINED _rocksdb_MAJOR AND DEFINED _rocksdb_MINOR AND
         DEFINED _rocksdb_PATCH)
        set(OMNIGRID_ROCKSDB_VERSION
            "${_rocksdb_MAJOR}.${_rocksdb_MINOR}.${_rocksdb_PATCH}")
      endif()
    endif()

    set(_omnigrid_rocksdb_version_compatible FALSE)
    if(NOT OMNIGRID_ROCKSDB_VERSION STREQUAL "unknown" AND
       NOT OMNIGRID_ROCKSDB_VERSION VERSION_LESS
           OMNIGRID_ROCKSDB_MIN_VERSION)
      set(_omnigrid_rocksdb_version_compatible TRUE)
    endif()

    if(_omnigrid_rocksdb_version_compatible)
      add_library(omnigrid_host_rocksdb UNKNOWN IMPORTED GLOBAL)
      set_target_properties(omnigrid_host_rocksdb PROPERTIES
        IMPORTED_LOCATION "${OMNIGRID_ROCKSDB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OMNIGRID_ROCKSDB_INCLUDE_DIR}")
      set(_omnigrid_rocksdb_target omnigrid_host_rocksdb)
    endif()
  endif()
endif()

if(_omnigrid_rocksdb_target)
  add_library(omnigrid_rocksdb INTERFACE)
  target_link_libraries(omnigrid_rocksdb INTERFACE "${_omnigrid_rocksdb_target}")
  add_library(OmniGrid::RocksDB ALIAS omnigrid_rocksdb)
  set(OMNIGRID_ROCKSDB_AVAILABLE TRUE)
endif()

function(omnigrid_require_rocksdb)
  if(NOT OMNIGRID_ROCKSDB_AVAILABLE)
    message(FATAL_ERROR
      "RocksDB >= ${OMNIGRID_ROCKSDB_MIN_VERSION} development files are required "
      "from the host (Ubuntu Noble: librocksdb-dev). OmniGrid never downloads "
      "or installs this dependency.")
  endif()
endfunction()

if(OMNIGRID_REQUIRE_ROCKSDB)
  omnigrid_require_rocksdb()
endif()

message(STATUS "OmniGrid EnTT: vendored ${OMNIGRID_ENTT_VERSION}")
if(OMNIGRID_ROCKSDB_AVAILABLE)
  message(STATUS "OmniGrid RocksDB: host ${OMNIGRID_ROCKSDB_VERSION}")
else()
  message(STATUS "OmniGrid RocksDB: host dependency not present (required from M05)")
endif()
