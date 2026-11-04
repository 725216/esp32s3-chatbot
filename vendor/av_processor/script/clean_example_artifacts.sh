#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXAMPLES_DIR="$REPO_ROOT/examples"

TARGETS=(
    "build"
    "managed_components"
    "sdkconfig.old"
    "sdkconfig.bmgr_board.old"
)

if [ ! -d "$EXAMPLES_DIR" ]; then
    echo "Error: examples directory not found: $EXAMPLES_DIR" >&2
    exit 1
fi

echo "Cleaning example artifacts under: $EXAMPLES_DIR"

for dir in "$EXAMPLES_DIR"/*; do
    [ -d "$dir" ] || continue

    if [ ! -d "$dir/main" ] || [ ! -f "$dir/CMakeLists.txt" ]; then
        echo "Skip non-example directory: $(basename "$dir")"
        continue
    fi

    echo "Processing example: $(basename "$dir")"

    for target in "${TARGETS[@]}"; do
        path="$dir/$target"
        if [ -e "$path" ]; then
            echo "  remove: $path"
            rm -rf "$path"
        fi
    done
done

echo "Done."
