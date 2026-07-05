#!/usr/bin/env bash
# build-esp32s2.sh — build BC250 ESP32-S2 firmware from repository root.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$REPO_ROOT/firmware-esp32s2/build.sh" "$@"
