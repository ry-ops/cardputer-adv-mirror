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

# --- sandbox writability shim -----------------------------------------------
# PlatformIO validates --project-dir with a click Path(writable=True) check,
# which calls os.access(dir, W_OK). Inside the agent sandbox that syscall
# returns False for the MOUNT POINT itself while returning True for its
# children -- same uid (501), same owner, same 0755 mode, and a real
# open(...,'w') in the directory SUCCEEDS. So the probe is wrong, not the
# permissions, and pio aborts with:
#   Error: Invalid value for '-d' / '--project-dir': Path '...' is not writable.
#
# Passing the project dir as a CHILD-relative path sidesteps the bad probe on
# the mount point. Only do this when os.access disagrees with an actual write,
# so a genuinely read-only checkout still fails loudly instead of silently.
if [ -z "${M5_NO_ACCESS_SHIM:-}" ] && python3 - <<'PY'
import os, sys
d = os.getcwd()
probe_says_ro = not os.access(d, os.W_OK)
try:
    p = os.path.join(d, ".pio_write_probe")
    open(p, "w").close(); os.remove(p)
    really_writable = True
except OSError:
    really_writable = False
sys.exit(0 if (probe_says_ro and really_writable) else 1)
PY
then
    # No path spelling fixes this: os.access returns False for the mount point
    # under EVERY form (trailing slash, /., realpath, and via a symlink), so
    # there is no directory argument that satisfies the probe. Patch the probe.
    #
    # Scope is deliberately narrow: only click's W_OK branch, only for a
    # directory that a real write test proves writable. Any other path -- and a
    # genuinely read-only one -- still fails exactly as it would unpatched.
    export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
    exec python3 -c '
import os, sys, click.types

_real = os.access
def _access(path, mode, **kw):
    if mode & os.W_OK and os.path.isdir(path) and not _real(path, mode, **kw):
        try:                                     # believe a real write, not the probe
            p = os.path.join(path, ".pio_access_probe")
            open(p, "w").close(); os.remove(p)
            return True
        except OSError:
            return False
    return _real(path, mode, **kw)

click.types.os.access = _access                  # only the click Path() check
sys.argv = ["platformio"] + sys.argv[1:]
from platformio.__main__ import main
sys.exit(main())
' "$@"
fi

exec python3 -m platformio "$@"
