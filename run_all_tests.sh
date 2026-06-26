#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
#  run_all_tests.sh — Lovelang Full Test Runner
#  Cross-platform: macOS · Linux · Windows (Git Bash / MSYS2 / WSL)
#
#  Covers:
#    • Interpreter (all examples)
#    • Native compile: ARM64 (Apple Silicon / Linux aarch64)
#    • Native compile: x86-64 (Intel/AMD)
#    • Cross-compile (Zig cc — skipped when Zig unavailable)
#    • npm CLI unit tests + pack + offline install
#    • Error cases (expected-failure assertions)
#    • Run modes: romantic / toxic / shayari
#
#  Usage:
#    bash run_all_tests.sh              # full suite
#    bash run_all_tests.sh --no-online  # skip live npm download
#    bash run_all_tests.sh --fast       # interpreter + npm only
# ═══════════════════════════════════════════════════════════════════════════
set -u
set -o pipefail

# ── CLI flags ───────────────────────────────────────────────────────────────
SKIP_ONLINE=0
FAST_MODE=0
for arg in "$@"; do
  case "$arg" in
    --no-online) SKIP_ONLINE=1 ;;
    --fast)      FAST_MODE=1   ;;
  esac
done

# ── Paths ───────────────────────────────────────────────────────────────────
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP_DIR="$(mktemp -d)"
BUILD_DIR="$TMP_DIR/build"
mkdir -p "$BUILD_DIR"

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
PACK_TGZ=""

# ── Detect platform ─────────────────────────────────────────────────────────
OS_TYPE="$(uname -s 2>/dev/null || echo "Windows")"
ARCH_TYPE="$(uname -m 2>/dev/null || echo "unknown")"

case "$OS_TYPE" in
  Darwin*)  PLATFORM="macos"   ;;
  Linux*)   PLATFORM="linux"   ;;
  MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
  *)        PLATFORM="unknown" ;;
esac

# Binary name — lovelang.exe on Windows
if [[ "$PLATFORM" == "windows" ]]; then
  LOVELANG_BIN="$ROOT_DIR/lovelang.exe"
else
  LOVELANG_BIN="$ROOT_DIR/lovelang"
fi

# ── Colour output ────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
  RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
  CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
else
  RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; RESET=''
fi

# ── Helpers ──────────────────────────────────────────────────────────────────
log_header() { echo; echo -e "${CYAN}${BOLD}── $1 ──${RESET}"; }
pass()  { PASS_COUNT=$((PASS_COUNT + 1));  echo -e "${GREEN}  PASS${RESET}  $1"; }
fail()  { FAIL_COUNT=$((FAIL_COUNT + 1));  echo -e "${RED}  FAIL${RESET}  $1"; }
skip()  { SKIP_COUNT=$((SKIP_COUNT + 1));  echo -e "${YELLOW}  SKIP${RESET}  $1 (reason: $2)"; }

cleanup() {
  [[ -n "$PACK_TGZ" && -f "$TMP_DIR/$PACK_TGZ" ]] && rm -f "$TMP_DIR/$PACK_TGZ"
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

require_cmd() { command -v "$1" >/dev/null 2>&1; }

run_step() {
  local name="$1"; shift
  echo -e "  ${BOLD}▶${RESET} $name"
  if "$@" 2>/dev/null; then pass "$name"; else fail "$name"; fi
}

run_step_verbose() {
  local name="$1"; shift
  echo -e "  ${BOLD}▶${RESET} $name"
  if "$@"; then pass "$name"; else fail "$name"; fi
}

run_expect_fail() {
  local name="$1"; shift
  echo -e "  ${BOLD}▶${RESET} $name (expected failure)"
  if "$@" 2>/dev/null; then fail "$name (unexpected success)"; else pass "$name"; fi
}

# ── Build functions ──────────────────────────────────────────────────────────
build_interpreter() {
  (cd "$ROOT_DIR" && make >/dev/null 2>&1)
}

# ── Interpreter run helpers ──────────────────────────────────────────────────
interp() {
  local file="$1"; shift
  "$LOVELANG_BIN" "$ROOT_DIR/$file" "$@" >/dev/null 2>&1
}

interp_fail() {
  local file="$1"
  "$LOVELANG_BIN" "$ROOT_DIR/$file" >/dev/null 2>&1
}

run_mode() {
  local mode="$1"
  "$LOVELANG_BIN" "$ROOT_DIR/examples/basics/01-hello-world.love" --mode "$mode" >/dev/null 2>&1
}

# ── Native compile helpers ───────────────────────────────────────────────────
native_compile_and_run() {
  local src="$1"
  local out="$BUILD_DIR/$(basename "$src" .love)"
  "$LOVELANG_BIN" "$ROOT_DIR/$src" --native --out "$out" >/dev/null 2>&1 && \
    "$out" >/dev/null 2>&1
}

native_compile_only() {
  local src="$1"
  local out="$BUILD_DIR/$(basename "$src" .love)"
  "$LOVELANG_BIN" "$ROOT_DIR/$src" --native --out "$out" >/dev/null 2>&1
}

# Cross-compile for a specific target using Zig
zig_cross_compile() {
  local src="$1"
  local target="$2"   # e.g. x86_64-linux-gnu
  local out="$BUILD_DIR/cross_$(basename "$src" .love)_${target//\//_}"
  "$LOVELANG_BIN" "$ROOT_DIR/$src" --native --out "$out" \
    --cc "zig cc -target $target" >/dev/null 2>&1
}

# ── npm helpers ──────────────────────────────────────────────────────────────
npm_unit_tests() {
  (cd "$ROOT_DIR/npm/lovelang-cli" && npm test >/dev/null 2>&1)
}

npm_pack() {
  PACK_TGZ="$(cd "$ROOT_DIR/npm/lovelang-cli" && npm pack 2>/dev/null)"
  mv "$ROOT_DIR/npm/lovelang-cli/$PACK_TGZ" "$TMP_DIR/$PACK_TGZ" 2>/dev/null
}

npm_offline_install() {
  local dir="$TMP_DIR/offline"
  mkdir -p "$dir"
  (
    cd "$dir"
    npm init -y >/dev/null 2>&1
    LOVELANG_SKIP_DOWNLOAD=1 npm install "$TMP_DIR/$PACK_TGZ" >/dev/null 2>&1
    LOVELANG_BIN_PATH="$LOVELANG_BIN" \
      ./node_modules/.bin/lovelang \
      "$ROOT_DIR/examples/basics/01-hello-world.love" >/dev/null 2>&1
  )
}

npm_online_install() {
  local dir="$TMP_DIR/online"
  mkdir -p "$dir"
  (
    cd "$dir"
    npm init -y >/dev/null 2>&1
    npm install lovelang-cli >/dev/null 2>&1
    ./node_modules/.bin/lovelang \
      "$ROOT_DIR/examples/basics/01-hello-world.love" >/dev/null 2>&1
    node -e "
      const fs = require('fs');
      const p = 'node_modules/lovelang-cli/vendor/install.json';
      if (!fs.existsSync(p)) process.exit(0);
      const j = JSON.parse(fs.readFileSync(p, 'utf8'));
      if ((j.owner && j.owner !== 'PATEL-KRISH-0') || (j.repo && j.repo !== 'lovelang')) {
        console.error('Unexpected manifest', j);
        process.exit(1);
      }
    " 2>/dev/null
  )
}

# ═══════════════════════════════════════════════════════════════════════════
echo
echo -e "${BOLD}Lovelang Test Runner v1.0.0${RESET}"
echo "Platform : $PLATFORM ($ARCH_TYPE)"
echo "Root     : $ROOT_DIR"
echo "Binary   : $LOVELANG_BIN"
echo "Tmp      : $TMP_DIR"
[[ $FAST_MODE -eq 1 ]]    && echo -e "${YELLOW}Mode: fast (native + online tests skipped)${RESET}"
[[ $SKIP_ONLINE -eq 1 ]]  && echo -e "${YELLOW}Mode: --no-online (live npm download skipped)${RESET}"

# ── 0. Pre-flight ────────────────────────────────────────────────────────────
log_header "Pre-flight"
run_step "command: make"  require_cmd make
run_step "command: npm"   require_cmd npm
run_step "command: node"  require_cmd node

# ── 1. Build ─────────────────────────────────────────────────────────────────
log_header "Build"
run_step "build interpreter (make)" build_interpreter

if [[ ! -x "$LOVELANG_BIN" ]]; then
  echo -e "${RED}Fatal: binary not found at $LOVELANG_BIN — aborting${RESET}"
  exit 1
fi

# ── 2. Interpreter — basics ──────────────────────────────────────────────────
log_header "Interpreter — basics"
run_step "01-hello-world"              interp "examples/basics/01-hello-world.love"
run_step "02-variables-and-constants"  interp "examples/basics/02-variables-and-constants.love"
run_step "03-types-and-values"         interp "examples/basics/03-types-and-values.love"
run_step "04-comments"                 interp "examples/basics/04-comments.love"

# ── 3. Interpreter — control flow ───────────────────────────────────────────
log_header "Interpreter — control flow"
run_step "05-conditionals"        interp "examples/control-flow/05-conditionals.love"
run_step "06-one-liner-if"        interp "examples/control-flow/06-one-liner-if.love"
run_step "07-loops"               interp "examples/control-flow/07-loops.love"
run_step "08-break-and-continue"  interp "examples/control-flow/08-break-and-continue.love"

# ── 4. Interpreter — functions ───────────────────────────────────────────────
log_header "Interpreter — functions"
run_step "09-functions-basics"          interp "examples/functions/09-functions-basics.love"
run_step "10-default-and-named-args"    interp "examples/functions/10-default-and-named-args.love"
run_step "11-recursion"                 interp "examples/functions/11-recursion.love"

# ── 5. Interpreter — collections ─────────────────────────────────────────────
log_header "Interpreter — collections"
run_step "12-lists"           interp "examples/collections/12-lists.love"
run_step "13-maps"            interp "examples/collections/13-maps.love"
run_step "14-string-toolkit"  interp "examples/collections/14-string-toolkit.love"

# ── 6. Interpreter — I/O ─────────────────────────────────────────────────────
log_header "Interpreter — I/O"
run_step "15-input-sleep-random-time"  interp "examples/io/15-input-sleep-random-time.love"
run_step "16-filesystem"               interp "examples/io/16-filesystem.love"

# ── 7. Interpreter — modules ─────────────────────────────────────────────────
log_header "Interpreter — modules"
run_step "17-import-export"  interp "examples/modules/17-import-export.love"

# ── 8. Interpreter — native examples (interpreter mode) ──────────────────────
log_header "Interpreter — native examples (interpreted)"
run_step "18-floats"     interp "examples/native/18-floats.love"
run_step "19-benchmark"  interp "examples/native/19-benchmark.love"

# ── 9. Interpreter — advanced ────────────────────────────────────────────────
log_header "Interpreter — advanced"
run_step "20-math-builtins"   interp "examples/advanced/20-math-builtins.love"
run_step "21-todo-app"        interp "examples/advanced/21-todo-app.love"
run_step "22-contacts-diary"  interp "examples/advanced/22-contacts-diary.love"

# ── 10. Interpreter — showcase ───────────────────────────────────────────────
log_header "Interpreter — showcase"
run_step "26-all-features"  interp "examples/showcase/26-all-features.love"
run_step "27-phase-tests"   interp "examples/showcase/27-phase-tests.love"

# ── 11. Run modes ────────────────────────────────────────────────────────────
log_header "Run modes"
run_step "mode: romantic"  run_mode "romantic"
run_step "mode: toxic"     run_mode "toxic"
run_step "mode: shayari"   run_mode "shayari"

# ── 12. Error cases (expected failures) ──────────────────────────────────────
log_header "Error cases (expected failures)"
run_expect_fail "23-error-empty-pop"       interp_fail "examples/errors/23-error-empty-pop.love"
run_expect_fail "24-error-missing-import"  interp_fail "examples/errors/24-error-missing-import.love"
run_expect_fail "25-error-invalid-named-arg" interp_fail "examples/errors/25-error-invalid-named-arg.love"

# ── 13. Native compile ───────────────────────────────────────────────────────
if [[ $FAST_MODE -eq 0 ]]; then
  log_header "Native compile — current architecture ($ARCH_TYPE)"

  run_step "native: 01-hello-world"     native_compile_and_run "examples/basics/01-hello-world.love"
  run_step "native: 05-conditionals"    native_compile_and_run "examples/control-flow/05-conditionals.love"
  run_step "native: 07-loops"           native_compile_and_run "examples/control-flow/07-loops.love"
  run_step "native: 09-functions"       native_compile_and_run "examples/functions/09-functions-basics.love"
  run_step "native: 11-recursion"       native_compile_and_run "examples/functions/11-recursion.love"
  run_step "native: 12-lists"           native_compile_and_run "examples/collections/12-lists.love"
  run_step "native: 13-maps"            native_compile_and_run "examples/collections/13-maps.love"
  run_step "native: 18-floats"          native_compile_and_run "examples/native/18-floats.love"
  run_step "native: 19-benchmark"       native_compile_and_run "examples/native/19-benchmark.love"
  run_step "native: 20-math-builtins"   native_compile_and_run "examples/advanced/20-math-builtins.love"
  run_step "native: 21-todo-app"        native_compile_and_run "examples/advanced/21-todo-app.love"
  run_step "native: 26-all-features"    native_compile_and_run "examples/showcase/26-all-features.love"

  # ── 14. Cross-compile with Zig ───────────────────────────────────────────
  log_header "Cross-compile with Zig (skipped if zig not found)"

  if require_cmd zig; then
    ZIG_VER="$(zig version 2>/dev/null)"
    echo "  Zig version: $ZIG_VER"

    case "$ARCH_TYPE" in
      arm64|aarch64)
        # On ARM64: cross-compile to x86_64
        run_step "zig cross: arm64→x86_64-linux"  \
          zig_cross_compile "examples/basics/01-hello-world.love" "x86_64-linux-gnu"
        run_step "zig cross: arm64→x86_64-windows" \
          zig_cross_compile "examples/basics/01-hello-world.love" "x86_64-windows-gnu"
        ;;
      x86_64|amd64)
        # On x86_64: cross-compile to ARM64
        run_step "zig cross: x86_64→aarch64-linux"  \
          zig_cross_compile "examples/basics/01-hello-world.love" "aarch64-linux-gnu"
        run_step "zig cross: x86_64→aarch64-macos"  \
          zig_cross_compile "examples/basics/01-hello-world.love" "aarch64-macos"
        ;;
    esac

    # Always try a compile-only for both common targets
    run_step "zig compile: x86_64-linux-gnu" \
      zig_cross_compile "examples/showcase/26-all-features.love" "x86_64-linux-gnu"
    run_step "zig compile: aarch64-linux-gnu" \
      zig_cross_compile "examples/showcase/26-all-features.love" "aarch64-linux-gnu"
  else
    skip "Zig cross-compile tests" "zig not found in PATH"
  fi
fi

# ── 15. npm CLI unit tests ───────────────────────────────────────────────────
log_header "npm CLI unit tests"
run_step "npm test (11 unit tests)"  npm_unit_tests
run_step "npm pack"                  npm_pack

# ── 16. Offline install ──────────────────────────────────────────────────────
log_header "npm offline install"
run_step "offline install + run (LOVELANG_BIN_PATH override)"  npm_offline_install

# ── 17. Online install ───────────────────────────────────────────────────────
if [[ $SKIP_ONLINE -eq 0 && $FAST_MODE -eq 0 ]]; then
  log_header "npm online install (live network)"
  run_step_verbose "online install + run (downloads from GitHub releases)"  npm_online_install
else
  skip "npm online install" "--no-online or --fast flag set"
fi

# ── Summary ──────────────────────────────────────────────────────────────────
echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BOLD}Test Summary${RESET}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "  ${GREEN}Passed${RESET} : $PASS_COUNT"
echo -e "  ${RED}Failed${RESET} : $FAIL_COUNT"
echo -e "  ${YELLOW}Skipped${RESET}: $SKIP_COUNT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ $FAIL_COUNT -gt 0 ]]; then
  echo -e "${RED}${BOLD}FAILED — $FAIL_COUNT test(s) did not pass.${RESET}"
  exit 1
fi

echo -e "${GREEN}${BOLD}All tests passed! ❤️${RESET}"