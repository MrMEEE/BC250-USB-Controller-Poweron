#!/usr/bin/env bash
# build.sh — build BC250 firmware from repository root (CMake + Pico SDK).
# Usage:
#   ./build.sh
#   ./build.sh --upload
#   ./build.sh --upload --uf2-target /media/$USER/RPI-RP2
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$REPO_ROOT/firmware/build.sh" "$@"
