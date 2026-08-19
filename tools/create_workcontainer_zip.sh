#!/usr/bin/env bash
set -Eeuo pipefail

export LC_ALL=C
export TZ=UTC

die() {
    printf 'WORKCONTAINER_ERROR: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

for required_command in \
    git tar zip unzip sha256sum sort find mktemp realpath cmp awk basename \
    dirname python3 chmod mkdir mv rm touch; do
    require_command "$required_command"
done

[[ $# -eq 2 && "$1" == '--output' ]] ||
    die 'usage: create_workcontainer_zip.sh --output /path/to/package.zip'

repo_root="$(git rev-parse --show-toplevel 2>/dev/null)" ||
    die 'run this script from inside the OmniGrid Git worktree'
repo_root="$(realpath "$repo_root")"
cd "$repo_root"

[[ -d .git && ! -L .git ]] ||
    die 'a real .git directory is required for the workcontainer'
[[ -d .opencode && ! -L .opencode ]] ||
    die 'tracked .opencode harness directory is missing'

output="$(realpath -m "$2")"
[[ "$output" == *.zip ]] || die 'output filename must end in .zip'
case "$output" in
    "$repo_root"|"$repo_root"/*)
        die 'output must be outside the repository being packaged'
        ;;
esac
output_parent="$(dirname "$output")"
[[ -d "$output_parent" && ! -L "$output_parent" ]] ||
    die "output directory is missing or symlinked: $output_parent"

branch="$(git branch --show-current)"
[[ "$branch" == main ]] || die "expected main branch, found: $branch"
head_sha="$(git rev-parse HEAD)"
tree_sha="$(git rev-parse 'HEAD^{tree}')"
git show-ref --verify --quiet refs/remotes/origin/main ||
    die 'origin/main is missing'
origin_main="$(git rev-parse refs/remotes/origin/main)"
[[ "$head_sha" == "$origin_main" ]] ||
    die "HEAD ($head_sha) is not synchronized with origin/main ($origin_main)"
[[ -z "$(git status --porcelain=v1 --untracked-files=all)" ]] ||
    die 'worktree is not clean'
[[ -z "$(git diff --name-only --diff-filter=U)" ]] ||
    die 'unmerged paths exist'
for marker in MERGE_HEAD REBASE_HEAD CHERRY_PICK_HEAD REVERT_HEAD; do
    marker_path="$(git rev-parse --git-path "$marker")"
    [[ ! -e "$marker_path" ]] || die "active Git operation: $marker"
done

remote_url="$(git remote get-url origin)"
case "$remote_url" in
    *://*@*) die 'origin URL contains embedded credentials' ;;
esac

python3 tools/validate_opencode_harness.py >/dev/null ||
    die 'OpenCode harness self-check failed'
python3 tools/check_host_dependencies.py --vendored-only >/dev/null ||
    die 'vendored dependency validation failed'
chain_output="$(python3 tools/milestone_state.py --verify-chain)" ||
    die 'milestone acceptance chain is invalid'
fingerprint_output="$(tools/create_harness_backup.sh --fingerprint-only)" ||
    die 'source fingerprint failed'
snapshot_sha256="$(awk -F= '$1 == "SNAPSHOT_SHA256" {print $2}' \
    <<<"$fingerprint_output")"
file_count="$(awk -F= '$1 == "FILE_COUNT" {print $2}' \
    <<<"$fingerprint_output")"
[[ "$snapshot_sha256" =~ ^[0-9a-f]{64}$ ]] ||
    die 'invalid source fingerprint'
[[ "$file_count" =~ ^[0-9]+$ ]] || die 'invalid source file count'

git fsck --full --no-dangling >/dev/null 2>&1 ||
    die 'source Git repository failed fsck'
[[ -z "$(find .git -type l -print -quit)" ]] ||
    die 'symlink found inside .git'

work_dir=''
candidate=''
cleanup() {
    if [[ -n "$candidate" && -f "$candidate" ]]; then
        rm -f -- "$candidate"
    fi
    if [[ -n "$work_dir" && -d "$work_dir" &&
          "$(basename "$work_dir")" == omnigrid-workcontainer.* ]]; then
        rm -rf -- "$work_dir"
    fi
}
trap cleanup EXIT

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/omnigrid-workcontainer.XXXXXX")"
package_name="OmniGrid-main-${head_sha:0:7}-deepseek-harness"
package_root="$work_dir/stage/$package_name"
mkdir -p "$package_root"

source_list="$work_dir/source-files.nul"
git ls-files -co --exclude-standard -z | sort -zu >"$source_list"
[[ -s "$source_list" ]] || die 'selected source file list is empty'
while IFS= read -r -d '' path; do
    [[ "$path" != /* && "$path" != '..' && "$path" != ../* &&
       "$path" != */../* ]] || die "unsafe source path: $path"
    [[ "$path" != *$'\n'* && "$path" != *$'\r'* ]] ||
        die "newline-bearing source path: $path"
    [[ -f "$path" && ! -L "$path" ]] ||
        die "source entry is not a regular file: $path"
done <"$source_list"

tar --create --file=- --null --files-from="$source_list" |
    tar --extract --file=- --directory="$package_root"
tar \
    --create \
    --file=- \
    --exclude='.git/*.lock' \
    --exclude='.git/objects/pack/tmp_*' \
    --exclude='.git/lfs/incomplete' \
    .git |
    tar --extract --file=- --directory="$package_root"

manifest="$package_root/.git/OMNIGRID_WORKCONTAINER_MANIFEST"
{
    printf '%s\n' 'OMNIGRID_WORKCONTAINER_V1'
    printf 'source_head=%s\n' "$head_sha"
    printf 'source_tree=%s\n' "$tree_sha"
    printf 'origin_main=%s\n' "$origin_main"
    printf 'branch=%s\n' "$branch"
    printf 'source_snapshot_sha256=%s\n' "$snapshot_sha256"
    printf 'source_file_count=%s\n' "$file_count"
    printf '%s\n' 'raw_git_directory_included=yes'
    printf '%s\n' 'opencode_directory_included=yes'
    printf '%s\n' 'entt_policy=vendored-v3.16.0'
    printf '%s\n' 'rocksdb_policy=host-version-8.9.0-or-newer'
    printf '%s\n' "$chain_output"
} >"$manifest"

# ZIP timestamps start in 1980. Normalizing the copied tree keeps repeated
# packages byte-identical for the same repository snapshot.
find "$work_dir/stage" -exec touch -h -d '@315532800' {} +

candidate="$(mktemp "$output_parent/.omnigrid-workcontainer.tmp.XXXXXX")"
rm -f -- "$candidate"
(
    cd "$work_dir/stage"
    find "$package_name" -print | sort | zip -X -9 -q "$candidate" -@
)

unzip -tqq "$candidate" || die 'ZIP integrity test failed'
verify_root="$work_dir/verify"
mkdir -p "$verify_root"
unzip -q "$candidate" -d "$verify_root"
verified_repo="$verify_root/$package_name"
[[ -d "$verified_repo/.git" ]] || die 'verified ZIP has no .git directory'
[[ -d "$verified_repo/.opencode" ]] ||
    die 'verified ZIP has no .opencode directory'
[[ "$(git -C "$verified_repo" rev-parse HEAD)" == "$head_sha" ]] ||
    die 'verified ZIP HEAD mismatch'
[[ "$(git -C "$verified_repo" rev-parse 'HEAD^{tree}')" == "$tree_sha" ]] ||
    die 'verified ZIP tree mismatch'
[[ -z "$(git -C "$verified_repo" status --porcelain=v1 --untracked-files=all)" ]] ||
    die 'verified ZIP worktree is not clean'
git -C "$verified_repo" fsck --full --no-dangling >/dev/null 2>&1 ||
    die 'verified ZIP Git repository failed fsck'
verified_fingerprint="$(
    cd "$verified_repo"
    tools/create_harness_backup.sh --fingerprint-only
)"
grep -Fx "SNAPSHOT_SHA256=$snapshot_sha256" <<<"$verified_fingerprint" >/dev/null ||
    die 'verified ZIP source fingerprint mismatch'
(
    cd "$verified_repo"
    python3 tools/validate_opencode_harness.py >/dev/null
    python3 tools/check_host_dependencies.py --vendored-only >/dev/null
) || die 'verified ZIP harness validation failed'

if [[ -e "$output" ]]; then
    [[ -f "$output" && ! -L "$output" ]] ||
        die "existing output is not a regular file: $output"
    cmp -s "$candidate" "$output" ||
        die "different package already exists: $output"
    rm -f -- "$candidate"
    candidate=''
else
    mv -- "$candidate" "$output"
    candidate=''
fi

chmod 0644 "$output"
zip_sha256="$(sha256sum "$output" | awk '{print $1}')"
printf 'WORKCONTAINER_PATH=%s\n' "$output"
printf 'WORKCONTAINER_SHA256=%s\n' "$zip_sha256"
printf 'WORKCONTAINER_HEAD=%s\n' "$head_sha"
printf 'WORKCONTAINER_TREE=%s\n' "$tree_sha"
printf 'WORKCONTAINER_SOURCE_SNAPSHOT=%s\n' "$snapshot_sha256"
printf 'WORKCONTAINER_ROOT=%s\n' "$package_name"
