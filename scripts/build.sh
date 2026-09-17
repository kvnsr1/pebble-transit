#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PATH="$PROJECT_DIR/.tools/node/bin:$PROJECT_DIR/.tools/bin:$PATH"

KEY_FILE="$PROJECT_DIR/src/pkjs/private-key.js"
if [ ! -f "$KEY_FILE" ]; then
  umask 077
  {
    printf "'use strict';\n"
    printf "module.exports = %s;\n" "'${TRANSIT_API_KEY:-}'"
  } > "$KEY_FILE"
fi

cd "$PROJECT_DIR"
exec pebble build
