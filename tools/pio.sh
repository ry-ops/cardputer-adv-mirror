#!/usr/bin/env bash
# Build the firmware using the HOST's PlatformIO install from a context where
# `pio` itself is not on PATH (e.g. the agent sandbox, or any shell where the
# pip --user bin dir was never added).
#
# Two paths are needed and neither is on sys.path by default:
#   1. the host site-packages holding platformio 6.1.19
#   2. tool-esptoolpy, which ships `esptool` as a package but is not installed
#      into site-packages — the build fails at bootloader.bin without it
#
# SPDX-License-Identifier: MIT
set -euo pipefail

HOSTSP="${M5_HOST_SITE_PACKAGES:-$HOME/Library/Python/3.9/lib/python/site-packages}"
ESPT="$(ls -d "$HOME"/.platformio/packages/tool-esptoolpy@* 2>/dev/null | sort | tail -1)"
[ -n "${ESPT:-}" ] || ESPT="$HOME/.platformio/packages/tool-esptoolpy"

if [ ! -d "$HOSTSP/platformio" ]; then
  echo "error: platformio not found at $HOSTSP" >&2
  echo "       set M5_HOST_SITE_PACKAGES, or: pip3 install platformio" >&2
  exit 1
fi

export PYTHONPATH="$HOSTSP:$ESPT${PYTHONPATH:+:$PYTHONPATH}"
cd "$(dirname "$0")/.."

# --- upload preflight -------------------------------------------------------
# Every upload failure in this project so far has had ONE cause: another process
# still holding /dev/cu.usbmodem*. esptool reports it as
#   "device reports readiness to read but returned no data
#    (device disconnected or multiple access on port?)"
# which reads like a cable fault and is not one. Worse, the failure can land
# mid-erase and leave the app partition blank.
#
# So: refuse to start an upload while the port is held, and name the culprit.
# A refused upload costs seconds; a half-erased one costs a recovery cycle.
case " $* " in
  *" upload "*)
    for dev in /dev/cu.usbmodem*; do
      [ -e "$dev" ] || continue
      holders="$(lsof -t "$dev" 2>/dev/null || true)"
      if [ -n "$holders" ]; then
        echo "" >&2
        echo "REFUSING TO UPLOAD: $dev is held by another process." >&2
        lsof "$dev" 2>/dev/null | sed 's/^/    /' >&2
        echo "" >&2
        echo "  Free it with:  kill -9 $(echo $holders | tr '\n' ' ')" >&2
        echo "  Then retry. (Unplug/replug also works, and is safe here.)" >&2
        echo "" >&2
        exit 2
      fi
    done
    ;;
esac

exec python3 -m platformio "$@"
