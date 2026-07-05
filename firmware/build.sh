#!/usr/bin/env bash
# build.sh — build BC250 firmware with CMake + Pico SDK.
# Usage:
#   ./build.sh
#   ./build.sh --upload
#   ./build.sh --upload --uf2-target /media/$USER/RPI-RP2
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./build.sh [options]

Options:
  -b, --build-dir DIR   Build directory (default: build)
  -g, --generator NAME  CMake generator (default: Ninja if available, else Unix Makefiles)
  -j, --jobs N          Parallel build jobs (default: nproc)
  -c, --clean           Remove build directory before configuring
  -u, --upload          Copy UF2 to mounted RPI-RP2 drive after build
      --uf2-target DIR  Explicit UF2 target mount path (default: auto-detect)
  --wait-for-pico       Wait up to 30s for Pico to appear in BOOTSEL mode before upload
  --fetch-sdk           Download and unpack Pico SDK to firmware/pico-sdk
  -h, --help            Show this help

Upload workflow:
  1. Connect Pico via USB
  2. Hold BOOTSEL button and press RESET (or plug in while holding BOOTSEL)
  3. Run with -u or --upload to flash the UF2
  4. Use --wait-for-pico to automatically wait for the mount to appear
EOF
}

BUILD_DIR="build"
GENERATOR=""
JOBS="$(nproc)"
CLEAN=0
UPLOAD=0
UF2_TARGET=""
FETCH_SDK=0
WAIT_FOR_PICO=0

find_uf2_target() {
  local candidate
  local candidates=(
    "/media/$USER/RPI-RP2"
    "/run/media/$USER/RPI-RP2"
  )

  for candidate in "${candidates[@]}"; do
    if [[ -d "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  # Handle distro/user mount variations, for example /media/<name>/RPI-RP2.
  for candidate in /media/*/RPI-RP2 /run/media/*/RPI-RP2; do
    if [[ -d "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -b|--build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    -g|--generator)
      GENERATOR="$2"
      shift 2
      ;;
    -j|--jobs)
      JOBS="$2"
      shift 2
      ;;
    -c|--clean)
      CLEAN=1
      shift
      ;;
    -u|--upload)
      UPLOAD=1
      shift
      ;;
    --uf2-target)
      UF2_TARGET="$2"
      shift 2
      ;;
    --fetch-sdk)
      FETCH_SDK=1
      shift
      ;;
    --wait-for-pico)
      WAIT_FOR_PICO=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico-sdk}"
export PICO_SDK_PATH

if [[ "$FETCH_SDK" -eq 1 ]]; then
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  LOCAL_SDK_PATH="$SCRIPT_DIR/pico-sdk"

  if [[ -d "$LOCAL_SDK_PATH" ]]; then
    echo "Pico SDK already exists at: $LOCAL_SDK_PATH"
  else
    echo "Downloading Pico SDK with submodules..."
    
    # Use git clone if available, fall back to download + manual extraction
    if command -v git &>/dev/null; then
      if git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git "$LOCAL_SDK_PATH"; then
        echo "Pico SDK (with submodules) cloned to: $LOCAL_SDK_PATH"
      else
        echo "Error: Failed to clone Pico SDK." >&2
        rm -rf "$LOCAL_SDK_PATH"
        exit 1
      fi
    else
      echo "Git not found; downloading ZIP without submodules..."
      mkdir -p "$LOCAL_SDK_PATH"
      
      # Fetch the latest release from GitHub.
      DOWNLOAD_URL="https://github.com/raspberrypi/pico-sdk/archive/refs/heads/master.zip"
      TEMP_ZIP="/tmp/pico-sdk-master.zip"
      TINYUSB_ZIP="/tmp/tinyusb-master.zip"
      
      if ! curl -fsSL -o "$TEMP_ZIP" "$DOWNLOAD_URL"; then
        echo "Error: Failed to download Pico SDK." >&2
        rm -f "$TEMP_ZIP"
        exit 1
      fi

      echo "Extracting Pico SDK..."
      if ! unzip -q "$TEMP_ZIP" -d /tmp/; then
        echo "Error: Failed to extract Pico SDK." >&2
        rm -f "$TEMP_ZIP"
        exit 1
      fi

      # Move the extracted contents into place.
      mv /tmp/pico-sdk-master/* "$LOCAL_SDK_PATH/"
      rmdir /tmp/pico-sdk-master 2>/dev/null || true
      rm -f "$TEMP_ZIP"

      # Manually fetch TinyUSB submodule
      echo "Downloading TinyUSB library..."
      TINYUSB_URL="https://github.com/hathach/tinyusb/archive/refs/heads/master.zip"
      mkdir -p "$LOCAL_SDK_PATH/lib/tinyusb"
      
      if curl -fsSL -o "$TINYUSB_ZIP" "$TINYUSB_URL"; then
        if unzip -q "$TINYUSB_ZIP" -d /tmp/; then
          mv /tmp/tinyusb-master/* "$LOCAL_SDK_PATH/lib/tinyusb/"
          rmdir /tmp/tinyusb-master 2>/dev/null || true
          rm -f "$TINYUSB_ZIP"
        fi
      fi

      echo "Pico SDK fetched to: $LOCAL_SDK_PATH"
    fi
  fi

  PICO_SDK_PATH="$LOCAL_SDK_PATH"
  export PICO_SDK_PATH
fi

if [[ ! -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake" ]]; then
  echo "Error: Pico SDK not found at:" >&2
  echo "  $PICO_SDK_PATH" >&2
  echo "Set PICO_SDK_PATH to your SDK path, for example:" >&2
  echo "  export PICO_SDK_PATH=/path/to/pico-sdk" >&2
  exit 1
fi

if [[ -z "$GENERATOR" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
  else
    GENERATOR="Unix Makefiles"
  fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ "$CLEAN" -eq 1 ]]; then
  rm -rf "$BUILD_DIR"
fi

cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" -DPICO_SDK_PATH="$PICO_SDK_PATH"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

UF2_FILE="$BUILD_DIR/usb_wake_switch.uf2"

if [[ "$UPLOAD" -eq 1 ]]; then
  if [[ ! -f "$UF2_FILE" ]]; then
    echo "Error: UF2 file not found: $UF2_FILE" >&2
    exit 1
  fi

  if [[ -z "$UF2_TARGET" ]]; then
    if ! UF2_TARGET="$(find_uf2_target)"; then
      if [[ "$WAIT_FOR_PICO" -eq 1 ]]; then
        echo "Waiting for Pico in BOOTSEL mode (up to 30 seconds)..."
        echo "If not already in BOOTSEL mode, hold BOOTSEL and press RESET now."
        
        wait_count=0
        while [[ $wait_count -lt 60 ]]; do  # 60 * 0.5s = 30s
          sleep 0.5
          if UF2_TARGET="$(find_uf2_target)"; then
            echo "Pico detected at $UF2_TARGET"
            break
          fi
          wait_count=$((wait_count + 1))
          if (( wait_count % 2 == 0 )); then
            printf '.'
          fi
        done
        echo
        
        if [[ -z "$UF2_TARGET" ]]; then
          echo "Error: Timeout waiting for Pico. No RPI-RP2 mount found." >&2
          echo "Troubleshooting:" >&2
          echo "  - Ensure Pico is in BOOTSEL mode (hold button while plugging in)" >&2
          echo "  - Check with: lsblk | grep -i rpi or mount | grep -i rpi" >&2
          echo "  - Use --uf2-target to specify mount path manually" >&2
          exit 1
        fi
      else
        echo "Error: Could not find mounted RPI-RP2 drive." >&2
        echo "" >&2
        echo "Put the RP2040 in BOOTSEL mode:" >&2
        echo "  1. Hold BOOTSEL button" >&2
        echo "  2. Press RESET (or plug in USB while holding BOOTSEL)" >&2
        echo "  3. The device will mount as RPI-RP2" >&2
        echo "" >&2
        echo "Then try one of:" >&2
        echo "  ./build.sh --upload --wait-for-pico   # Wait for mount to appear" >&2
        echo "  ./build.sh --upload --uf2-target DIR  # Specify mount path manually" >&2
        exit 1
      fi
    fi
  fi

  if [[ ! -d "$UF2_TARGET" ]]; then
    echo "Error: UF2 target path does not exist: $UF2_TARGET" >&2
    exit 1
  fi

  cp "$UF2_FILE" "$UF2_TARGET/"
  sync
  echo "Uploaded $UF2_FILE to $UF2_TARGET"
fi

echo
echo "Build complete. Output files:"
echo "  $UF2_FILE"
echo "  $BUILD_DIR/usb_wake_switch.bin"
echo "  $BUILD_DIR/usb_wake_switch.hex"
