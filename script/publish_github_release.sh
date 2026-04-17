#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-v0.2.0}"
RELEASE_DIR="$PROJECT_ROOT/dist/$VERSION"
NOTES_FILE="$PROJECT_ROOT/releases/$VERSION.md"

if ! command -v gh >/dev/null 2>&1; then
    echo "gh is required to publish a GitHub release" >&2
    exit 1
fi

gh auth status >/dev/null

if [ ! -d "$RELEASE_DIR" ]; then
    echo "missing release directory: $RELEASE_DIR" >&2
    echo "run: bash ./script/package_stage2_release.sh $VERSION" >&2
    exit 1
fi

gh release view "$VERSION" >/dev/null 2>&1 && EXISTS=1 || EXISTS=0

if [ "$EXISTS" = "1" ]; then
    gh release upload "$VERSION" "$RELEASE_DIR"/* --clobber
    gh release edit "$VERSION" --notes-file "$NOTES_FILE" --title "$VERSION"
else
    gh release create "$VERSION" "$RELEASE_DIR"/* --notes-file "$NOTES_FILE" --title "$VERSION"
fi

echo "published GitHub release $VERSION"
