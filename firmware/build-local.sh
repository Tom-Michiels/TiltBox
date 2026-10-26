#!/usr/bin/env bash
# Build outside the source tree (and outside Documents/iCloud on macOS).
set -euo pipefail
firmware_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
target="${1:-voice-a}"
case "$target" in voice-a) ;; *) echo 'Usage: bash firmware/build-local.sh [voice-a]' >&2; exit 2 ;; esac
if ! command -v idf.py >/dev/null 2>&1; then
    echo 'Activate ESP-IDF 5.4.2 first: source /path/to/esp-idf/export.sh' >&2
    exit 1
fi
if [[ "$(uname -s)" == Darwin ]]; then
    default_build_root="$HOME/Library/Caches/TiltBox-build"
else
    default_build_root="${XDG_CACHE_HOME:-$HOME/.cache}/TiltBox-build"
fi
build_root="${TILTBOX_BUILD_ROOT:-$default_build_root}"
mkdir -p "$build_root/$target"
if [[ ! -f "$build_root/$target/sdkconfig" ]]; then
    cp "$firmware_root/$target/sdkconfig.voice" "$build_root/$target/sdkconfig"
fi
cd "$firmware_root/$target"
idf.py -B "$build_root/$target/build" -DSDKCONFIG="$build_root/$target/sdkconfig" build
