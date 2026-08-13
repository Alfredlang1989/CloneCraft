#!/usr/bin/env bash
set -Eeuo pipefail

# Omnigrid build helper.
# IMPORTANT: This script NEVER installs or modifies OS packages.
# It only inspects the current toolchain and builds inside the repository.

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

BUILD_TYPE="Debug"
CHUNK_EDGE="${OMNIGRID_CHUNK_EDGE:-16}"
GROUP_EDGE="${OMNIGRID_GROUP_EDGE:-16}"
JOBS="${OMNIGRID_BUILD_JOBS:-}"
DO_TEST=1
DO_RUN=0
DO_CLEAN=0
VERBOSE=0
FINGERPRINT_ONLY=0
DO_STATIC_ANALYSIS=1
ANALYZE_ONLY=0
GENERATOR=""
BUILD_DIR=""

usage() {
    cat <<'USAGE'
Omnigrid compile helper

Usage:
  ./compile.sh [options]

Options:
  --debug                 Debug build (default)
  --release               Release build
  --relwithdebinfo         RelWithDebInfo build
  --chunk N               Chunk edge length (default: 16)
  --group N               Chunk-group edge in chunks (default: 16)
  --jobs N                Parallel build jobs
  --build-dir DIR          Override build directory
  --generator NAME         Override CMake generator
  --clean                  Remove selected build directory before configure
  --no-test                Do not run ctest after build
  --test                   Run tests after build (default)
  --run                    Run ./omnigrid after successful build/tests
  --verbose                Verbose compiler/build output
  --fingerprint            Print host/toolchain fingerprint and exit
  --analyze-only           Configure + architecture check + clang-tidy, then exit
  --no-static-analysis     Emergency escape hatch: skip clang-tidy (architecture check still runs)
  -h, --help               Show this help

Examples:
  ./compile.sh
  ./compile.sh --release --jobs 16
  ./compile.sh --chunk 32 --group 16 --clean
  ./compile.sh --debug --run

Environment overrides:
  OMNIGRID_CHUNK_EDGE
  OMNIGRID_GROUP_EDGE
  OMNIGRID_BUILD_JOBS
  CLANG_TIDY               Optional path/name override for clang-tidy
  CMAKE_PREFIX_PATH
  PKG_CONFIG_PATH

Policy:
  This script never runs apt/dnf/yum/pacman/zypper/brew/sudo or installs
  anything into the operating system. If a dependency is missing, it stops
  and tells you what failed.
USAGE
}

fatal() {
    printf '\n[ERROR] %s\n' "$*" >&2
    printf '[ERROR] No system packages were installed or modified.\n' >&2
    exit 1
}

info() {
    printf '[omnigrid] %s\n' "$*"
}

warn() {
    printf '[omnigrid] WARNING: %s\n' "$*" >&2
}

need_command() {
    local cmd="$1"
    command -v "$cmd" >/dev/null 2>&1 || fatal "Required command not found: $cmd"
}

is_positive_integer() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

while (($#)); do
    case "$1" in
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --release)
            BUILD_TYPE="Release"
            ;;
        --relwithdebinfo)
            BUILD_TYPE="RelWithDebInfo"
            ;;
        --chunk)
            shift
            (($#)) || fatal "--chunk requires a value"
            CHUNK_EDGE="$1"
            ;;
        --group)
            shift
            (($#)) || fatal "--group requires a value"
            GROUP_EDGE="$1"
            ;;
        --jobs)
            shift
            (($#)) || fatal "--jobs requires a value"
            JOBS="$1"
            ;;
        --build-dir)
            shift
            (($#)) || fatal "--build-dir requires a value"
            BUILD_DIR="$1"
            ;;
        --generator)
            shift
            (($#)) || fatal "--generator requires a value"
            GENERATOR="$1"
            ;;
        --clean)
            DO_CLEAN=1
            ;;
        --test)
            DO_TEST=1
            ;;
        --no-test)
            DO_TEST=0
            ;;
        --run)
            DO_RUN=1
            ;;
        --verbose)
            VERBOSE=1
            ;;
        --fingerprint)
            FINGERPRINT_ONLY=1
            ;;
        --analyze-only)
            ANALYZE_ONLY=1
            ;;
        --no-static-analysis)
            DO_STATIC_ANALYSIS=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fatal "Unknown option: $1 (use --help)"
            ;;
    esac
    shift
done

is_positive_integer "$CHUNK_EDGE" || fatal "Chunk edge must be a positive integer: $CHUNK_EDGE"
is_positive_integer "$GROUP_EDGE" || fatal "Group edge must be a positive integer: $GROUP_EDGE"
if [[ -n "$JOBS" ]]; then
    is_positive_integer "$JOBS" || fatal "--jobs must be a positive integer: $JOBS"
fi

need_command cmake
need_command git
need_command pkg-config
need_command python3

# Choose a build backend without changing the OS.
if [[ -z "$GENERATOR" ]]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
        need_command make
    fi
fi

# Basic compiler sanity check. CMake will perform the authoritative compiler check.
if command -v c++ >/dev/null 2>&1; then
    CXX_VERSION="$(c++ --version 2>/dev/null | head -n1 || true)"
elif command -v g++ >/dev/null 2>&1; then
    CXX_VERSION="$(g++ --version 2>/dev/null | head -n1 || true)"
elif command -v clang++ >/dev/null 2>&1; then
    CXX_VERSION="$(clang++ --version 2>/dev/null | head -n1 || true)"
else
    fatal "No C++ compiler found (c++, g++, or clang++)"
fi

# SDL3 is consumed through CMake, but the target machine also exposes the
# pkg-config module. Print its exact version so remote/harness builds can mirror
# the user's environment instead of guessing which SDL3 revision is installed.
SDL3_VERSION="unknown (CMake will perform the authoritative check)"
if pkg-config --exists sdl3; then
    SDL3_VERSION="$(pkg-config --modversion sdl3)"
fi

# OgreNext is found by pkg-config in CMake, so fail with a clear diagnostic here.
if ! pkg-config --exists OGRE-Next; then
    fatal "pkg-config module 'OGRE-Next' not found. PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-<unset>}"
fi
if ! pkg-config --exists OGRE-Next-Hlms; then
    fatal "pkg-config module 'OGRE-Next-Hlms' not found. PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-<unset>}"
fi
if ! pkg-config --exists OGRE-Next-Overlay; then
    fatal "pkg-config module 'OGRE-Next-Overlay' not found. The F5 debug HUD uses Ogre Overlay. PKG_CONFIG_PATH=${PKG_CONFIG_PATH:-<unset>}"
fi

if [[ -z "$BUILD_DIR" ]]; then
    type_slug="$(printf '%s' "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
    BUILD_DIR="build/${type_slug}-c${CHUNK_EDGE}-g${GROUP_EDGE}"
fi

info "Project      : $PROJECT_ROOT"
info "Build type   : $BUILD_TYPE"
info "Chunk edge   : $CHUNK_EDGE"
info "Group edge   : $GROUP_EDGE"
info "Generator    : $GENERATOR"
info "Build dir    : $BUILD_DIR"
if [[ -r /etc/os-release ]]; then
    OS_PRETTY="$(. /etc/os-release; printf '%s' "${PRETTY_NAME:-unknown}")"
else
    OS_PRETTY="unknown"
fi
info "OS           : $OS_PRETTY"
info "Kernel       : $(uname -srmo 2>/dev/null || uname -a)"
info "Compiler     : ${CXX_VERSION:-unknown}"
info "CMake        : $(cmake --version | head -n1)"
info "Ninja        : $(ninja --version 2>/dev/null || printf 'not used/not found')"
info "pkg-config   : $(pkg-config --version)"
info "SDL3         : $SDL3_VERSION"
info "OgreNext     : $(pkg-config --modversion OGRE-Next)"
info "Ogre HLMS    : $(pkg-config --modversion OGRE-Next-Hlms)"
info "Ogre Overlay : $(pkg-config --modversion OGRE-Next-Overlay)"
OGRE_PREFIX="$(pkg-config --variable=prefix OGRE-Next)"
OGRE_MEDIA_DIR="${OGRE_PREFIX}/share/OGRE-Next/Media"
info "Ogre Media   : $OGRE_MEDIA_DIR"
if [[ -d "$OGRE_MEDIA_DIR/2.0/scripts/materials/Common" ]]; then
    info "Debug Common : present"
else
    warn "Debug Common : MISSING ($OGRE_MEDIA_DIR/2.0/scripts/materials/Common)"
fi
if [[ -f "$OGRE_MEDIA_DIR/packs/DebugPack.zip" ]]; then
    info "DebugPack    : present"
else
    warn "DebugPack    : MISSING ($OGRE_MEDIA_DIR/packs/DebugPack.zip); HUD will try an existing system TTF"
fi
CLANG_TIDY_DISPLAY=""
if [[ -n "${CLANG_TIDY:-}" ]]; then
    CLANG_TIDY_DISPLAY="$CLANG_TIDY"
else
    for tidy_candidate in clang-tidy clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15 clang-tidy-14; do
        if command -v "$tidy_candidate" >/dev/null 2>&1; then
            CLANG_TIDY_DISPLAY="$(command -v "$tidy_candidate")"
            break
        fi
    done
fi
if [[ -z "$CLANG_TIDY_DISPLAY" ]]; then
    CLANG_TIDY_DISPLAY="not found (required for normal builds; set CLANG_TIDY=...)"
fi
info "clang-tidy   : $CLANG_TIDY_DISPLAY"

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    branch="$(git branch --show-current 2>/dev/null || true)"
    commit="$(git rev-parse --short HEAD 2>/dev/null || true)"
    info "Git          : ${branch:-detached}@${commit:-unknown}"
    if ! git diff --quiet || ! git diff --cached --quiet; then
        info "Git status   : working tree contains local changes (preserved)"
    else
        info "Git status   : clean"
    fi
fi

if (( FINGERPRINT_ONLY )); then
    info "Fingerprint complete; no build performed."
    exit 0
fi

info "Architecture : checking module boundaries/include cycles..."
if ! python3 tools/architecture_check.py --root "$PROJECT_ROOT"; then
    fatal "Architecture check failed. Fix the dependency violation before compiling."
fi

# Resolve the build directory before any destructive operation.  Cleaning is
# intentionally restricted to directories inside the repository.
if command -v realpath >/dev/null 2>&1; then
    BUILD_DIR_ABS="$(realpath -m -- "$BUILD_DIR")"
else
    BUILD_DIR_ABS="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$BUILD_DIR")"
fi

if (( DO_CLEAN )); then
    case "$BUILD_DIR_ABS" in
        "$PROJECT_ROOT"/*) ;;
        *) fatal "Refusing --clean outside the repository: $BUILD_DIR_ABS" ;;
    esac
    case "$BUILD_DIR_ABS" in
        "$PROJECT_ROOT"|"$PROJECT_ROOT/.")
            fatal "Refusing to clean the repository root"
            ;;
    esac
    info "Cleaning build directory: $BUILD_DIR_ABS"
    rm -rf -- "$BUILD_DIR_ABS"
fi

BUILD_DIR="$BUILD_DIR_ABS"
mkdir -p "$BUILD_DIR"

info "Configuring..."
cmake_args=(
    -S .
    -B "$BUILD_DIR"
    -G "$GENERATOR"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DOMNIGRID_CHUNK_EDGE=$CHUNK_EDGE"
    "-DOMNIGRID_GROUP_EDGE=$GROUP_EDGE"
)

if ! cmake "${cmake_args[@]}"; then
    fatal "CMake configure failed. Check the error above for missing SDL3/OgreNext dependencies or incompatible versions."
fi

if (( DO_STATIC_ANALYSIS )); then
    info "Static analysis: clang-tidy AST pass..."
    analysis_args=(--root "$PROJECT_ROOT" --build-dir "$BUILD_DIR")
    if [[ -n "$JOBS" ]]; then
        analysis_args+=(--jobs "$JOBS")
    fi
    if ! python3 tools/run_static_analysis.py "${analysis_args[@]}"; then
        fatal "clang-tidy static analysis failed or clang-tidy is unavailable. Set CLANG_TIDY=/path/to/clang-tidy if it is installed under a versioned/custom name."
    fi
else
    info "Static analysis: SKIPPED by explicit --no-static-analysis"
fi

if (( ANALYZE_ONLY )); then
    info "Analysis complete; build skipped by --analyze-only."
    exit 0
fi

info "Building..."
build_args=(--build "$BUILD_DIR")
if [[ -n "$JOBS" ]]; then
    build_args+=(--parallel "$JOBS")
else
    build_args+=(--parallel)
fi
if (( VERBOSE )); then
    build_args+=(--verbose)
fi

if ! cmake "${build_args[@]}"; then
    fatal "Compilation failed."
fi

if (( DO_TEST )); then
    info "Running tests..."
    ctest_args=(--test-dir "$BUILD_DIR" --output-on-failure)
    if [[ -n "$JOBS" ]]; then
        ctest_args+=(--parallel "$JOBS")
    fi
    if ! ctest "${ctest_args[@]}"; then
        fatal "One or more tests failed."
    fi
fi

exe="$BUILD_DIR/omnigrid"
[[ -x "$exe" ]] || fatal "Build reported success but executable was not found: $exe"

printf '\n'
info "SUCCESS"
info "Executable   : $exe"
if (( DO_TEST )); then
    info "Tests        : passed"
else
    info "Tests        : skipped"
fi

if (( DO_RUN )); then
    info "Starting Omnigrid..."
    exec "$exe"
fi
