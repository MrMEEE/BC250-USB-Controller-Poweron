#!/usr/bin/env bash
# build.sh — build/upload ESP32-S2 firmware via PlatformIO.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./build.sh [options]

Options:
  -e, --env NAME        PlatformIO environment (default: lolin_s2_mini)
  -p, --port DEV        Upload port (example: /dev/ttyACM1)
  -c, --clean           Clean before build
  -u, --upload          Upload after successful build
  -m, --monitor         Open serial monitor after upload
      --wait-for-port   Wait up to 30s for serial port to appear (with --upload)
  -h, --help            Show this help

Examples:
  ./build.sh
  ./build.sh --clean
  ./build.sh --upload --wait-for-port
  ./build.sh --upload --port /dev/ttyACM1 --monitor
EOF
}

ENV_NAME="lolin_s2_mini"
PORT=""
CLEAN=0
UPLOAD=0
MONITOR=0
WAIT_FOR_PORT=0

find_upload_port() {
  local candidate
  local candidates=()

  # Build a candidate list in stable order.
  for candidate in /dev/ttyACM* /dev/ttyUSB*; do
    if [[ -e "$candidate" ]]; then
      candidates+=("$candidate")
    fi
  done

  # First pass: prefer Espressif / LOLIN ports.
  for candidate in "${candidates[@]}"; do
    if command -v udevadm >/dev/null 2>&1; then
      if udevadm info -q property -n "$candidate" 2>/dev/null | grep -qiE 'ID_VENDOR_ID=303a|ID_VENDOR=.*Espressif|ID_MODEL=.*(ESP32|LOLIN)|ID_MODEL_FROM_DATABASE=.*(Espressif|LOLIN)'; then
        printf '%s\n' "$candidate"
        return 0
      fi
    fi
  done

  # Fallback: first available serial device.
  for candidate in "${candidates[@]}"; do
    printf '%s\n' "$candidate"
    return 0
  done

  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -e|--env)
      ENV_NAME="$2"
      shift 2
      ;;
    -p|--port)
      PORT="$2"
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
    -m|--monitor)
      MONITOR=1
      shift
      ;;
    --wait-for-port)
      WAIT_FOR_PORT=1
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

if ! command -v pio >/dev/null 2>&1; then
  echo "Error: PlatformIO (pio) not found in PATH." >&2
  echo "Install with: pipx install platformio  (or: pip install --user platformio)" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ "$CLEAN" -eq 1 ]]; then
  pio run -e "$ENV_NAME" -t clean
fi

pio run -e "$ENV_NAME"

if [[ "$UPLOAD" -eq 1 ]]; then
  if [[ -z "$PORT" ]]; then
    if ! PORT="$(find_upload_port)"; then
      if [[ "$WAIT_FOR_PORT" -eq 1 ]]; then
        echo "Waiting for ESP32-S2 serial port (up to 30 seconds)..."
        echo "Press RESET on the board now if needed."

        wait_count=0
        while [[ $wait_count -lt 60 ]]; do
          sleep 0.5
          if PORT="$(find_upload_port)"; then
            echo "Detected upload port: $PORT"
            break
          fi
          wait_count=$((wait_count + 1))
          if (( wait_count % 2 == 0 )); then
            printf '.'
          fi
        done
        echo
      fi

      if [[ -z "$PORT" ]]; then
        echo "Error: Could not detect serial upload port." >&2
        echo "Use --port /dev/ttyACM1 (or your device) explicitly." >&2
        exit 1
      fi
    fi
  fi

  if [[ ! -e "$PORT" ]]; then
    echo "Error: Upload port does not exist: $PORT" >&2
    exit 1
  fi

  pio run -e "$ENV_NAME" -t upload --upload-port "$PORT"

  if [[ "$MONITOR" -eq 1 ]]; then
    exec pio device monitor -b 115200 --port "$PORT"
  fi
fi

echo
echo "Build complete for env '$ENV_NAME'."
if [[ "$UPLOAD" -eq 1 ]]; then
  echo "Uploaded via $PORT"
fi
