#!/bin/sh
# Build, flash, verify and start the physical dashboard in an explicit live/demo mode.
set -eu
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "${1:-live}" in
    live) preset=Debug ;;
    demo) preset=Demo ;;
    *) printf 'Usage: %s [live|demo]\n' "$0" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [live|demo]\n' "$0" >&2
    exit 2
fi
if [ -x "$PROJECT_DIR/.venv/bin/cmake" ]; then
    cmake_command="$PROJECT_DIR/.venv/bin/cmake"
else
    cmake_command=cmake
fi
cd "$PROJECT_DIR"
"$cmake_command" --preset "$preset"
"$cmake_command" --build "build/$preset" --parallel 8
BUILD_DIR="$PROJECT_DIR/build/$preset" /bin/sh "$PROJECT_DIR/.vscode/flash_board.sh"
