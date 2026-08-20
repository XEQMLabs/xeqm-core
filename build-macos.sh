#!/usr/bin/env bash
# Builds xeqm-d (and optionally xeqm-wallet / xeqm-rpc) from source on macOS.
# Works on both Intel (x86_64) and Apple Silicon (arm64).
#
# Usage:
#   ./build-macos.sh           # builds xeqm-d only
#   ./build-macos.sh all       # builds xeqm-d, xeqm-wallet, xeqm-rpc
#
# The binary is copied to the repo root when done.
# Three macOS-specific issues are handled automatically:
#   1. Short temp build path  — prevents "file name too long" on Intel
#   2. LocalLibzmq.cmake patch — fixes cmake 4.x child-cmake incompatibility
#   3. SQLiteCpp header patch  — adds missing <cstdint> for AppleClang

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
BUILD_TARGET="${1:-daemon}"
[[ "$BUILD_TARGET" == "all" ]] && BUILD_TARGET="daemon simplewallet wallet_rpc_server"

# ── sanity checks ────────────────────────────────────────────────────────────

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "ERROR: This script is for macOS only." >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required. Install it first:"
  echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"'
  exit 1
fi

# cmake must be 3.14+; cmake 4.x is fine (we patch child-cmake compat below)
if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found — will be installed via Homebrew."
fi

# ── dependencies ─────────────────────────────────────────────────────────────

echo "==> Installing/updating dependencies via Homebrew..."
brew install cmake ninja pkg-config \
  zeromq openssl@3 libsodium hidapi \
  sqlite3 curl gmp zstd boost \
  2>/dev/null || true

# ── source patches ───────────────────────────────────────────────────────────

# Patch 1: LocalLibzmq.cmake — cmake 4.x dropped support for old cmake_minimum_required
# in child cmake invocations (ExternalProject_Add). Without this the build fails with
# "cmake_minimum_required VERSION 2.x...3.x requires setting cmake_policy_version_minimum".
LZMQ="${REPO_ROOT}/external/oxen-mq/cmake/local-libzmq/LocalLibzmq.cmake"
if [[ -f "$LZMQ" ]] && ! grep -q 'CMAKE_POLICY_VERSION_MINIMUM' "$LZMQ"; then
  echo "==> Patching LocalLibzmq.cmake for cmake 4.x compatibility..."
  sed -i '' \
    's/CMAKE_ARGS \${libzmq_compiler_args}/CMAKE_ARGS ${libzmq_compiler_args} -DCMAKE_POLICY_VERSION_MINIMUM=3.5/' \
    "$LZMQ"
fi

# Patch 2: SQLiteCpp headers — AppleClang requires explicit <cstdint> include
python3 - <<'PY'
from pathlib import Path
patches = [
    (Path("external/SQLiteCpp/include/SQLiteCpp/Statement.h"), "#include <memory>"),
    (Path("external/SQLiteCpp/include/SQLiteCpp/Column.h"),    "#include <SQLiteCpp/Statement.h>"),
]
for path, marker in patches:
    if not path.exists():
        continue
    text = path.read_text()
    if "#include <cstdint>" not in text and marker in text:
        path.write_text(text.replace(marker, marker + "\n#include <cstdint>", 1))
        print(f"==> Patched {path}")
PY

# ── configure ────────────────────────────────────────────────────────────────

OSSL="$(brew --prefix openssl@3)"
BOOST="$(brew --prefix boost)"
ZMQ="$(brew --prefix zeromq)"
HIDAPI="$(brew --prefix hidapi)"
NCPU="$(sysctl -n hw.logicalcpu)"

# Use a short temp path — long absolute paths in object file names exceed macOS
# make's path limit on Intel and produce "file name too long" link errors.
BUILD_DIR="/tmp/xb$$"
mkdir -p "$BUILD_DIR"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "==> Configuring (build dir: $BUILD_DIR)..."
cd "$BUILD_DIR"

# Force native /usr/bin/ar — if GNU binutils is installed, cmake may pick up
# 'gar' which produces SYSV-format archives that Apple ld64 rejects with
# "archive member invalid control bits".
cmake "$REPO_ROOT" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DARCH=default \
  -DCMAKE_AR=/usr/bin/ar \
  -DCMAKE_RANLIB=/usr/bin/ranlib \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_TESTS=OFF \
  -DBUILD_STATIC_DEPS=OFF \
  -DSTATIC=OFF \
  -DTREZOR=OFF \
  -DBoost_NO_BOOST_CMAKE=ON \
  -DBOOST_ROOT="$BOOST" \
  -DBoost_USE_STATIC_LIBS=OFF \
  -DCMAKE_PREFIX_PATH="${BOOST};${ZMQ};${OSSL};${HIDAPI}" \
  -DOPENSSL_ROOT_DIR="$OSSL" \
  -DWITH_SYSTEM_ZMQ=ON \
  -DBUILD_ZMQ=OFF \
  -DBUILD_LIBZMQ=OFF \
  -DZEROMQ_INCLUDE_DIR="${ZMQ}/include" \
  -DZEROMQ_LIBRARY="${ZMQ}/lib/libzmq.dylib" \
  -DHIDAPI_INCLUDE_DIR="${HIDAPI}/include" \
  -DHIDAPI_LIBRARY="${HIDAPI}/lib/libhidapi.dylib"

# ── build ────────────────────────────────────────────────────────────────────

echo "==> Building with $NCPU cores..."
# shellcheck disable=SC2086
cmake --build . --target $BUILD_TARGET --parallel "$NCPU"

# ── collect binaries ─────────────────────────────────────────────────────────

echo ""
echo "==> Copying binaries to repo root..."
for bin in xeqm-d xeqm-wallet xeqm-rpc; do
  [[ -f "${BUILD_DIR}/bin/${bin}" ]] || continue
  cp "${BUILD_DIR}/bin/${bin}" "${REPO_ROOT}/${bin}"
  chmod +x "${REPO_ROOT}/${bin}"
  echo "    ${REPO_ROOT}/${bin}"
done

echo ""
echo "==> Done."
"${REPO_ROOT}/xeqm-d" --version
