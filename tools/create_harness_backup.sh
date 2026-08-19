#!/usr/bin/env bash
set -Eeuo pipefail

export LC_ALL=C

die() {
    printf 'BACKUP_ERROR: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

for required_command in \
    git tar gzip sha256sum sort find mktemp realpath cmp awk basename date \
    python3 chmod dirname grep mkdir mv rm; do
    require_command "$required_command"
done

repo_root="$(git rev-parse --show-toplevel 2>/dev/null)" ||
    die 'run this script from inside the OmniGrid Git worktree'
repo_root="$(realpath "$repo_root")"
cd "$repo_root"

for required_file in \
    AGENTS.md \
    opencode.json \
    .opencode/commands/loop.md \
    .opencode/agents/deepseek-loop.md \
    .opencode/agents/deepseek-builder.md \
    .opencode/agents/deepseek-review-code.md \
    .opencode/agents/deepseek-review-architecture.md \
    docs/ROADMAP.md \
    tools/milestone_state.py \
    tools/validate_opencode_harness.py; do
    [[ -f "$required_file" ]] || die "required harness file missing: $required_file"
done

collect_files() {
    {
        git ls-files -co --exclude-standard -z |
            while IFS= read -r -d '' path; do
                [[ "$path" != /* ]] || die "absolute path rejected: $path"
                [[ "$path" != '..' && "$path" != ../* && "$path" != */../* ]] ||
                    die "parent traversal rejected: $path"
                [[ "$path" != *$'\n'* && "$path" != *$'\r'* ]] ||
                    die "newline-bearing path rejected: $path"
                [[ -e "$path" || -L "$path" ]] || continue

                case "$path" in
                    .git|.git/*|.omnigrid-backup|.omnigrid-backup/*|\
                    build|build/*|build-*|build-*/*|cmake-build-*|cmake-build-*/*|\
                    .opencode/node_modules|.opencode/node_modules/*|\
                    .opencode/.cache|.opencode/.cache/*|\
                    graphify-out|graphify-out/*|\
                    .tools|.tools/*|.cache|.cache/*|cache|cache/*|\
                    worlddata|worlddata/*|saves|saves/*|\
                    *.log|*.tar|*.tar.gz|*.tgz|\
                    [0-9]*PixelShader_ps.glsl|[0-9]*VertexShader_vs.glsl)
                        continue
                        ;;
                esac
                printf '%s\0' "$path"
            done

        if [[ -f graphify-out/GRAPH_REPORT.md ]]; then
            printf '%s\0' graphify-out/GRAPH_REPORT.md
        fi
    } | sort -zu
}

validate_file_list() {
    local list_file="$1"
    local path parent part
    local -a parts

    while IFS= read -r -d '' path; do
        [[ -e "$repo_root/$path" || -L "$repo_root/$path" ]] ||
            die "selected path disappeared: $path"
        [[ ! -L "$repo_root/$path" ]] || die "symlinked file rejected: $path"

        parent="$repo_root"
        IFS='/' read -r -a parts <<<"$path"
        if (( ${#parts[@]} > 1 )); then
            for part in "${parts[@]:0:${#parts[@]}-1}"; do
                parent="$parent/$part"
                [[ ! -L "$parent" ]] ||
                    die "symlinked parent rejected: ${parent#"$repo_root/"}"
            done
        fi
    done <"$list_file"
}

fingerprint_from_list() {
    local list_file="$1"
    tar \
        --create \
        --file=- \
        --format=gnu \
        --sort=name \
        --mtime='@0' \
        --owner=0 \
        --group=0 \
        --numeric-owner \
        --mode='u+rwX,go+rX,go-w' \
        --directory="$repo_root" \
        --null \
        --files-from="$list_file" |
        sha256sum | awk '{print $1}'
}

work_dir=''
candidate=''
cleanup() {
    if [[ -n "$candidate" && -f "$candidate" ]]; then
        rm -f -- "$candidate"
    fi
    if [[ -n "$work_dir" && -d "$work_dir" &&
          "$(basename "$work_dir")" == omnigrid-backup.* ]]; then
        rm -rf -- "$work_dir"
    fi
}
trap cleanup EXIT

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/omnigrid-backup.XXXXXX")"
source_list="$work_dir/source-files.nul"
collect_files >"$source_list"
[[ -s "$source_list" ]] || die 'selected source file list is empty'
validate_file_list "$source_list"

snapshot_sha256="$(fingerprint_from_list "$source_list")"
file_count=0
while IFS= read -r -d '' counted_path; do
    file_count=$((file_count + 1))
done <"$source_list"

if [[ "${1:-}" == '--fingerprint-only' && $# -eq 1 ]]; then
    printf 'SNAPSHOT_SHA256=%s\n' "$snapshot_sha256"
    printf 'FILE_COUNT=%s\n' "$file_count"
    exit 0
fi

python3 tools/validate_opencode_harness.py >/dev/null ||
    die 'OpenCode harness self-check failed; refusing backup'

[[ $# -eq 4 && "$1" == '--milestone' && "$3" == '--loop-status' ]] ||
    die 'usage: create_harness_backup.sh --fingerprint-only | --milestone Mxx --loop-status PASS|LOOP_ABORTED|BLOCKED'
milestone="$2"
loop_status="$4"
[[ "$milestone" =~ ^M[0-9]{2}$ ]] || die "invalid milestone: $milestone"
case "$loop_status" in
    PASS|LOOP_ABORTED|BLOCKED) ;;
    *) die "invalid terminal loop status: $loop_status" ;;
esac

if [[ "$loop_status" == PASS ]]; then
    milestone_state="$(python3 tools/milestone_state.py --state "$milestone")"
    grep -Fx "STATE=ACCEPTED" <<<"$milestone_state" >/dev/null ||
        die "$milestone must be ACCEPTED before a PASS backup"
fi

git diff --cached --quiet -- || die 'staged changes exist; refusing backup'
[[ -z "$(git diff --name-only --diff-filter=U)" ]] ||
    die 'unmerged paths exist; refusing backup'
for marker in MERGE_HEAD REBASE_HEAD CHERRY_PICK_HEAD REVERT_HEAD; do
    marker_path="$(git rev-parse --git-path "$marker")"
    [[ ! -e "$marker_path" ]] || die "active Git operation: $marker"
done

backup_parent="$(dirname "$repo_root")"
backup_dir="$backup_parent/backup"
[[ ! -L "$backup_dir" ]] || die "backup directory is a symlink: $backup_dir"
mkdir -p -- "$backup_dir"
backup_dir="$(realpath "$backup_dir")"
[[ "$backup_dir" == "$backup_parent/backup" ]] ||
    die "unexpected backup destination: $backup_dir"

backup_date="$(date +%F)"
snapshot_short="${snapshot_sha256:0:12}"
target="$backup_dir/omingraft_Primus_impetus${backup_date}-${milestone}-${loop_status}-${snapshot_short}.tar.gz"
[[ ! -L "$target" ]] || die "backup target is a symlink: $target"
candidate="$(mktemp "$backup_dir/.omingraft-${milestone}.tmp.XXXXXX")"

stage="$work_dir/tree"
mkdir -p "$stage/.omnigrid-backup"
tar --create --file=- --directory="$repo_root" --null --files-from="$source_list" |
    tar --extract --file=- --directory="$stage"

head_ref="$(git symbolic-ref --quiet HEAD 2>/dev/null || true)"
[[ -n "$head_ref" ]] || die 'detached HEAD cannot be backed up'
bundle_refs=("$head_ref")
if git show-ref --verify --quiet refs/remotes/origin/main; then
    bundle_refs+=(refs/remotes/origin/main)
fi
bundle_path="$stage/.omnigrid-backup/repository.bundle"
git -c pack.threads=1 bundle create "$bundle_path" "${bundle_refs[@]}" >/dev/null
git bundle verify "$bundle_path" >/dev/null 2>&1

git status --porcelain=v1 --untracked-files=all > \
    "$stage/.omnigrid-backup/GIT_STATUS.txt"
git diff --binary --full-index --no-ext-diff HEAD -- > \
    "$stage/.omnigrid-backup/WORKTREE.patch"

source_head="$(git rev-parse HEAD)"
origin_main='MISSING'
if git show-ref --verify --quiet refs/remotes/origin/main; then
    origin_main="$(git rev-parse refs/remotes/origin/main)"
fi

manifest="$stage/.omnigrid-backup/MANIFEST.txt"
printf '%s\n' \
    'OMNIGRID_HARNESS_BACKUP_V2' \
    "milestone=$milestone" \
    "loop_status=$loop_status" \
    "archive_date=$backup_date" \
    "source_head=$source_head" \
    "origin_main=$origin_main" \
    "source_file_count=$file_count" \
    "source_snapshot_sha256=$snapshot_sha256" \
    'archive_mtime_epoch=0' \
    'raw_git_directory_included=no' \
    'git_history=repository.bundle' \
    'tracked_changes=WORKTREE.patch' \
    >"$manifest"

source_text_list="$stage/.omnigrid-backup/SOURCE_FILES.txt"
while IFS= read -r -d '' path; do
    printf '%s\n' "$path"
done <"$source_list" >"$source_text_list"

archive_list="$work_dir/archive-files.nul"
find "$stage" -mindepth 1 -type f -printf '%P\0' | sort -zu >"$archive_list"
tar \
    --create \
    --file=- \
    --format=gnu \
    --sort=name \
    --mtime='@0' \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    --mode='u+rwX,go+rX,go-w' \
    --transform='s|^|OmniGrid-Primus-Impetus/|' \
    --directory="$stage" \
    --null \
    --files-from="$archive_list" |
    gzip -n -9 >"$candidate"

gzip -t "$candidate"
while IFS= read -r archived_path; do
    [[ "$archived_path" == OmniGrid-Primus-Impetus/* ]] ||
        die "archive path escaped expected root: $archived_path"
    case "$archived_path" in
        /*|*/../*|*/.git/*|*/build/*|*/build-*/*|*/cmake-build-*/*|\
        */.opencode/node_modules/*|*/graphify-out/.graphify*|\
        *PixelShader_ps.glsl|*VertexShader_vs.glsl)
            die "forbidden archive member: $archived_path"
            ;;
    esac
done < <(tar -tzf "$candidate")

verified_bundle="$work_dir/verified.bundle"
tar -xOzf "$candidate" \
    OmniGrid-Primus-Impetus/.omnigrid-backup/repository.bundle >"$verified_bundle"
git bundle verify "$verified_bundle" >/dev/null 2>&1

source_list_after="$work_dir/source-files-after.nul"
collect_files >"$source_list_after"
cmp -s "$source_list" "$source_list_after" ||
    die 'selected file set changed during backup'
snapshot_after="$(fingerprint_from_list "$source_list_after")"
[[ "$snapshot_after" == "$snapshot_sha256" ]] ||
    die 'selected file contents changed during backup'

if [[ -e "$target" ]]; then
    [[ -f "$target" ]] || die "backup target is not a regular file: $target"
    cmp -s "$candidate" "$target" || die "different backup already exists: $target"
    rm -f -- "$candidate"
    candidate=''
else
    mv -- "$candidate" "$target"
    candidate=''
fi

chmod 0644 "$target"
backup_sha256="$(sha256sum "$target" | awk '{print $1}')"
printf 'MILESTONE=%s\n' "$milestone"
printf 'LOOP_STATUS=%s\n' "$loop_status"
printf 'SNAPSHOT_SHA256=%s\n' "$snapshot_sha256"
printf 'BACKUP_PATH=%s\n' "$target"
printf 'BACKUP_SHA256=%s\n' "$backup_sha256"
