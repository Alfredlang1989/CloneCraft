#!/usr/bin/env python3
"""Verify vendored EnTT and milestone-gated host RocksDB development files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTT_ROOT = ROOT / "third_party" / "entt"
ENTT_MANIFEST = ENTT_ROOT / "vendor.json"
EXPECTED_ENTT_VERSION = "3.16.0"
EXPECTED_ENTT_COMMIT = "b4e58bdd364ad72246c123a0c28538eab3252672"
EXPECTED_ENTT_FILE_COUNT = 88
EXPECTED_ENTT_INVENTORY_SHA256 = \
    "15f5ca4a07e0d34c6d5f173930349b4be11319f50b4ac8dae1a9f2181ae87717"
MIN_ROCKSDB_VERSION = (8, 9, 0)
MILESTONE_ID = re.compile(r"^M\d{2}$")


class DependencyError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_entt() -> tuple[bool, str]:
    try:
        manifest = json.loads(ENTT_MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return False, f"manifest unavailable: {exc}"

    if manifest.get("version") != EXPECTED_ENTT_VERSION:
        return False, "manifest version does not match the pinned harness version"
    if manifest.get("commit") != EXPECTED_ENTT_COMMIT:
        return False, "manifest commit does not match the pinned upstream commit"
    if manifest.get("license") != "MIT":
        return False, "manifest license is not MIT"
    if manifest.get("distribution") != "src/entt":
        return False, "manifest does not select the pinned modular distribution"
    if manifest.get("file_count") != EXPECTED_ENTT_FILE_COUNT:
        return False, "manifest file count does not match the pin"
    if manifest.get("checksums") != "vendor.sha256":
        return False, "manifest checksum inventory path does not match the pin"
    if manifest.get("checksums_sha256") != EXPECTED_ENTT_INVENTORY_SHA256:
        return False, "manifest checksum-inventory digest does not match the pin"

    inventory = ENTT_ROOT / "vendor.sha256"
    if not inventory.is_file() or sha256(inventory) != EXPECTED_ENTT_INVENTORY_SHA256:
        return False, "vendor.sha256 is missing or modified"
    try:
        lines = inventory.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return False, f"cannot read vendor.sha256: {exc}"
    if len(lines) != EXPECTED_ENTT_FILE_COUNT:
        return False, "vendor.sha256 file count does not match the pin"

    expected_files: dict[str, str] = {}
    for line in lines:
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", line)
        if match is None:
            return False, "vendor.sha256 contains an invalid line"
        relative = match.group(2)
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or relative in expected_files:
            return False, "vendor.sha256 contains an unsafe/duplicate path"
        expected_files[relative] = match.group(1)

    actual_files = {str(path.relative_to(ENTT_ROOT))
                    for path in ENTT_ROOT.rglob("*") if path.is_file() and
                    path.name not in {"vendor.json", "vendor.sha256",
                                      "README.omnigrid.md"}}
    if actual_files != set(expected_files):
        return False, "vendored file set differs from vendor.sha256"

    for relative, expected in expected_files.items():
        path = ENTT_ROOT / relative
        if not path.is_file():
            return False, f"missing vendored file: {path.relative_to(ROOT)}"
        if sha256(path) != expected:
            return False, f"checksum mismatch: {path.relative_to(ROOT)}"
    return True, "verified"


def unique_paths(values: list[Path]) -> list[Path]:
    result: list[Path] = []
    seen: set[str] = set()
    for value in values:
        key = str(value)
        if key not in seen:
            seen.add(key)
            result.append(value)
    return result


def environment_paths(name: str) -> list[Path]:
    return [Path(item) for item in os.environ.get(name, "").split(os.pathsep)
            if item]


def pkg_config_data() -> tuple[str | None, list[Path], list[Path]]:
    executable = shutil.which("pkg-config")
    if executable is None:
        return None, [], []

    exists = subprocess.run(
        [executable, "--exists", "rocksdb >= 8.9.0"],
        capture_output=True, text=True, check=False,
    )
    if exists.returncode != 0:
        return None, [], []

    def query(flag: str) -> str:
        result = subprocess.run(
            [executable, flag, "rocksdb"],
            capture_output=True, text=True, check=False,
        )
        return result.stdout.strip() if result.returncode == 0 else ""

    version = query("--modversion") or None
    include_dirs = [Path(token[2:]) for token in shlex.split(query("--cflags-only-I"))
                    if token.startswith("-I") and len(token) > 2]
    library_dirs = [Path(token[2:]) for token in shlex.split(query("--libs-only-L"))
                    if token.startswith("-L") and len(token) > 2]
    return version, include_dirs, library_dirs


def parse_version(value: str | None) -> tuple[int, int, int] | None:
    if value is None:
        return None
    match = re.match(r"^\s*(\d+)\.(\d+)(?:\.(\d+))?", value)
    if match is None:
        return None
    return (int(match.group(1)), int(match.group(2)), int(match.group(3) or 0))


def version_from_header(header: Path) -> tuple[int, int, int] | None:
    try:
        text = header.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    values: dict[str, int] = {}
    for part in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"(?m)^#define\s+ROCKSDB_{part}\s+(\d+)\s*$", text)
        if match is None:
            return None
        values[part] = int(match.group(1))
    return values["MAJOR"], values["MINOR"], values["PATCH"]


def find_rocksdb() -> tuple[bool, str, str, str]:
    pkg_version, pkg_includes, pkg_libraries = pkg_config_data()
    multiarch = sysconfig.get_config_var("MULTIARCH")
    prefix = Path(os.environ["ROCKSDB_ROOT"]) if os.environ.get("ROCKSDB_ROOT") else None

    include_roots = pkg_includes + environment_paths("CPATH")
    include_roots += environment_paths("CPLUS_INCLUDE_PATH")
    if prefix is not None:
        include_roots.append(prefix / "include")
    include_roots += [Path("/usr/include"), Path("/usr/local/include")]

    library_roots = pkg_libraries + environment_paths("LIBRARY_PATH")
    library_roots += environment_paths("LD_LIBRARY_PATH")
    if prefix is not None:
        library_roots.extend((prefix / "lib", prefix / "lib64"))
    library_roots += [Path("/usr/lib"), Path("/usr/local/lib"),
                      Path("/usr/lib64"), Path("/usr/local/lib64")]
    if multiarch:
        library_roots.extend((Path("/usr/lib") / multiarch,
                              Path("/usr/local/lib") / multiarch))

    header_root: Path | None = None
    for candidate in unique_paths(include_roots):
        if (candidate / "rocksdb" / "db.h").is_file() and \
           (candidate / "rocksdb" / "version.h").is_file():
            header_root = candidate
            break

    library: Path | None = None
    names = ("librocksdb.so", "librocksdb.a", "librocksdb.dylib", "rocksdb.lib")
    for candidate in unique_paths(library_roots):
        for name in names:
            path = candidate / name
            if path.is_file():
                library = path
                break
        if library is not None:
            break

    version_tuple = parse_version(pkg_version)
    if version_tuple is None and header_root is not None:
        version_tuple = version_from_header(header_root / "rocksdb" / "version.h")
    version = ".".join(str(part) for part in version_tuple) \
        if version_tuple is not None else "unknown"

    if header_root is None:
        return False, version, "missing", "rocksdb/db.h and version.h not found"
    if library is None:
        return False, version, str(header_root), \
            "linkable librocksdb.so/librocksdb.a not found"
    if version_tuple is None:
        return False, version, f"{header_root};{library}", "version is unreadable"
    if version_tuple < MIN_ROCKSDB_VERSION:
        return False, version, f"{header_root};{library}", "version is older than 8.9.0"
    return True, version, f"{header_root};{library}", "verified"


def milestone_selection(identifier: str | None) -> tuple[str, bool]:
    from milestone_state import ROADMAP, current_milestone, parse_roadmap

    _, milestones = parse_roadmap(ROADMAP.read_text(encoding="utf-8"))
    ids = [milestone.identifier for milestone in milestones]
    if "M05" not in ids:
        raise DependencyError("ROADMAP has no M05 RocksDB milestone")

    if identifier is None:
        current = current_milestone(milestones)
        if current is None:
            return "COMPLETE", False
        identifier = current.identifier
    elif identifier not in ids:
        raise DependencyError(f"unknown milestone: {identifier}")

    return identifier, ids.index(identifier) >= ids.index("M05")


def main() -> int:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--current", action="store_true")
    action.add_argument("--milestone", metavar="MXX")
    action.add_argument("--vendored-only", action="store_true")
    args = parser.parse_args()

    if args.milestone is not None and MILESTONE_ID.fullmatch(args.milestone) is None:
        parser.error("--milestone must match Mxx")

    try:
        entt_ready, entt_detail = check_entt()
        if args.vendored_only:
            milestone, rocksdb_required = "VENDORED", False
        else:
            milestone, rocksdb_required = milestone_selection(args.milestone)

        rocksdb_ready = False
        rocksdb_version = "not-checked"
        rocksdb_location = "not-checked"
        rocksdb_detail = "not checked for vendored-only validation"
        if not args.vendored_only:
            rocksdb_ready, rocksdb_version, rocksdb_location, rocksdb_detail = \
                find_rocksdb()

        blocked = not entt_ready or (rocksdb_required and not rocksdb_ready)
        if args.vendored_only:
            rocksdb_status = "NOT_CHECKED"
        elif rocksdb_ready:
            rocksdb_status = "READY"
        elif rocksdb_required:
            rocksdb_status = "MISSING_REQUIRED"
        else:
            rocksdb_status = "MISSING_NOT_REQUIRED_YET"

        print(f"DEPENDENCY_MILESTONE={milestone}")
        print("ENTT_POLICY=VENDORED")
        print(f"ENTT_VERSION={EXPECTED_ENTT_VERSION}")
        print(f"ENTT_STATUS={'READY' if entt_ready else 'INVALID'}")
        print(f"ENTT_DETAIL={entt_detail}")
        print("ROCKSDB_POLICY=HOST")
        print(f"ROCKSDB_MIN_VERSION={'.'.join(map(str, MIN_ROCKSDB_VERSION))}")
        print(f"ROCKSDB_REQUIRED={'YES' if rocksdb_required else 'NO'}")
        print(f"ROCKSDB_STATUS={rocksdb_status}")
        print(f"ROCKSDB_VERSION={rocksdb_version}")
        print(f"ROCKSDB_LOCATION={rocksdb_location}")
        print(f"ROCKSDB_DETAIL={rocksdb_detail}")
        print(f"OVERALL_STATUS={'BLOCKED' if blocked else 'READY'}")
        return 1 if blocked else 0
    except (DependencyError, OSError) as exc:
        print(f"DEPENDENCY_ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
