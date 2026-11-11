#!/usr/bin/env bash
# Try to rebase the screencast-capture branch onto an upstream tag in a
# temporary worktree. Your working branch is never modified.
#
#   fast/scripts/rebase-upstream.sh [TAG] [--no-fetch]
#
# TAG defaults to the newest upstream release tag (vX.Y.Z). On success a NEW
# local branch  screencast-capture-on-<TAG>  is created and the commands to
# adopt it are printed. On conflicts the conflicting files are listed and
# nothing is changed.
set -euo pipefail

BRANCH="${BRANCH:-screencast-capture}"
TAG=""
FETCH=1
for a in "$@"; do
    case "$a" in
        --no-fetch) FETCH=0 ;;
        -*) echo "unknown option $a" >&2; exit 2 ;;
        *) TAG="$a" ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
git rev-parse --verify -q "refs/heads/$BRANCH" >/dev/null || { echo "no branch $BRANCH" >&2; exit 2; }

if [ "$FETCH" = 1 ]; then
    git fetch upstream --tags --quiet
fi
if [ -z "$TAG" ]; then
    TAG="$(git tag --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=-version:refname | head -1)"
fi
git rev-parse --verify -q "refs/tags/$TAG^{commit}" >/dev/null || { echo "no such tag $TAG" >&2; exit 2; }

OLDBASE="$(git merge-base "$BRANCH" upstream/master)"
echo "branch      : $BRANCH ($(git rev-parse --short "$BRANCH"))"
echo "old base    : $(git rev-parse --short "$OLDBASE")"
echo "rebase onto : $TAG ($(git rev-parse --short "$TAG^{commit}"))"
echo "commits     : $(git rev-list --count "$OLDBASE..$BRANCH")"

SB="$(mktemp -d /tmp/w11c-sandbox.XXXXXX)"
WT="$SB/wt"
cleanup() {
    git -C "$ROOT" worktree remove --force "$WT" >/dev/null 2>&1 || true
    git -C "$ROOT" worktree prune >/dev/null 2>&1 || true
    if [[ "$SB" == /tmp/w11c-sandbox.* ]] && [ -d "$SB" ]; then
        if findmnt -M "$SB" >/dev/null 2>&1; then
            echo "refusing to rm -rf mount point $SB" >&2
        else
            rm -rf "$SB"
        fi
    fi
}
trap cleanup EXIT

git worktree add --detach "$WT" "$BRANCH" >/dev/null
cd "$WT"
if git rebase --onto "$TAG^{commit}" "$OLDBASE" HEAD >"$SB/rebase.log" 2>&1; then
    NEW="$(git rev-parse HEAD)"
    OUT="${BRANCH}-on-${TAG}"
    git -C "$ROOT" branch -f "$OUT" "$NEW"
    echo
    echo "OK: clean rebase. New branch: $OUT ($(git rev-parse --short "$NEW"))"
    echo "Check it builds and the tests pass:"
    echo "  git checkout $OUT && fast/scripts/build.sh && fast/scripts/test.sh"
    echo "Adopt it (only when satisfied):"
    echo "  git checkout $BRANCH && git reset --hard $OUT"
else
    echo
    echo "CONFLICTS rebasing onto $TAG. Conflicting files:"
    git diff --name-only --diff-filter=U | sed 's/^/  /'
    echo "(rebase aborted; $BRANCH untouched)"
    git rebase --abort >/dev/null 2>&1 || true
    exit 1
fi
