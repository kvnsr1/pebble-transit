#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PATH="$PROJECT_DIR/.tools/node/bin:$PROJECT_DIR/.tools/bin:$PATH"
export DYLD_LIBRARY_PATH="$PROJECT_DIR/.tools/libpng/lib"

cd "$PROJECT_DIR"
exec pebble install --emulator emery --logs build/pebble-transit.pbw
