#!/usr/bin/env bash
# run_love.sh — Run a Lovelang source file using the local binary or system install.
#
# Usage:
#   ./run_love.sh [file.love] [lovelang options...]
#
# Examples:
#   ./run_love.sh examples/01-romantic-hello.love
#   ./run_love.sh myfile.love --mode shayari
#   ./run_love.sh myfile.love --native --out ./build/myapp
#   ./run_love.sh myfile.love --tokens
#   ./run_love.sh myfile.love --debug-love

set -euo pipefail

# ── locate binary ────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

find_binary() {
  # 1. Prefer local build next to this script
  for candidate in \
    "$SCRIPT_DIR/lovelang" \
    "$SCRIPT_DIR/lovelang.exe"; do
    if [[ -x "$candidate" ]]; then
      echo "$candidate"
      return 0
    fi
  done

  # 2. Fall back to system PATH (installed via npm or manual)
  if command -v lovelang &>/dev/null; then
    echo "lovelang"
    return 0
  fi

  return 1
}

if ! BIN="$(find_binary)"; then
  echo "❌  lovelang binary not found." >&2
  echo "    Build it with:  make" >&2
  echo "    Or install via: npm install -g lovelang-cli" >&2
  exit 1
fi

# ── resolve file argument ────────────────────────────────────────────────────
FILE="${1:-examples/01-romantic-hello.love}"
if [[ $# -ge 1 ]]; then
  shift
fi

# ── sanity-check the file exists before invoking binary ─────────────────────
if [[ ! -f "$FILE" ]]; then
  # Only error if the arg doesn't look like a flag (handles: ./run_love.sh --help)
  if [[ "$FILE" != --* ]]; then
    echo "❌  File not found: $FILE" >&2
    exit 1
  fi
  # Pass through flags directly (e.g. ./run_love.sh --help)
  exec "$BIN" "$FILE" "$@"
fi

# ── run ──────────────────────────────────────────────────────────────────────
exec "$BIN" "$FILE" "$@"
