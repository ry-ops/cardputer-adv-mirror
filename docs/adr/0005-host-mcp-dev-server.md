# ADR 0005 — Host-side MCP server for the firmware development loop

**Status:** Accepted — implemented
**Date:** 2026-08-08
**Context:** ADR 0002 implementation; five consecutive debugging failures

## Context

Bringing up the ADR 0002 mirror produced five failures in a row, none of which
were bugs in the firmware:

| # | Failure | Root cause |
|---|---|---|
| 1 | `zsh: not enough directory stack entries` | A pasted command carried a trailing `#` comment containing `~5 min`. zsh leaves `interactive_comments` off, so `#` was not a comment and `~5` parsed as a directory-stack reference. |
| 2 | Serial monitor showed nothing | The sketch had no `Serial` output at all; the single `log_i` fired once at boot and was missed because USB CDC re-enumerates after reset. |
| 3 | Two debug cycles spent on a device that was never flashed | `pio run` (build) succeeded and was mistaken for `pio run -t upload`. The upload never ran — it was on the line killed by failure #1. |
| 4 | `pio: command not found` | pip installed it to `~/Library/Python/3.9/bin`, not on PATH. |
| 5 | `firmware.bin` older than the edited `main.cpp` | No staleness check; flashing would have uploaded stale code. |

The shared structure matters more than the individual bugs. The agent cannot
observe the device: the sandbox can *see* `/dev/cu.usbmodem101` but gets
`Operation not permitted` on open, and cannot execute `pio` at all. So every
cycle was **guess → user runs → user pastes → agent diagnoses**, roughly three
round-trips per bug. Failures #2 and #3 were expensive precisely because they
were only observable *at the device*.

## Decision

Build a host-side MCP server (`tools/m5_mcp/server.py`) exposing the build,
flash, and serial surface as structured tools. It runs outside the sandbox and
therefore reaches both `pio` and the serial port.

JSON-RPC 2.0 over stdio, pure stdlib; `pyserial` imported lazily by the two
serial tools only.

## Consequences

Each tool traces to a failure above:

- `check_stale` / `flash` staleness guard → #5
- `flash` upload verification (greps `Hash of data verified` / `Hard resetting`
  rather than trusting exit code 0) → #3
- `flash` busy-port refusal → the port-held-by-monitor variant of #3
- `serial_read` (bounded capture, explicit `silent: true`) → #2
- `doctor` (pio autodetect across `~/Library/Python/*/bin`) → #4
- No shell text is pasted at all → #1
- `identify_firmware` → identifying stock firmware from strings, which is how
  #3 was ultimately proven
- `backup_flash` → flashing is irreversible for the stock image

**Costs.** A component to maintain, and it must be registered as a connector by
the user — there is no programmatic registration API (`host.agents` exposes only
`attach_connector`/`detach_connector` for connectors that already exist).

**Alternative considered: a Makefile.** Cheaper, and it fixes #1, #4 and #5. It
does *not* fix #2 or #3, because those required the agent to observe the device.
That observation loop is the whole value; the Makefile was rejected as
insufficient, though it remains a reasonable fallback if the server is
unavailable.

**Scope.** Deliberately device-agnostic — nothing here is Cardputer-specific, so
it serves any ESP32/PlatformIO project.

## Safety

`flash` overwrites the device permanently; `backup_flash` exists to make that
recoverable and should be run before the first flash of a new device. Both refuse
to run while another process holds the port. All output is clipped and
build/flash logs are condensed, so a long PlatformIO log cannot flood context.


## Addendum (2026-08-08) — measured sandbox capability split

The agent sandbox was probed directly rather than assumed. After granting
`~/.platformio` and `~/Library/Python`, the boundary is not where it first
appeared:

| Operation | Sandbox | Evidence |
|---|---|---|
| **Build** (`pio run`) | **works** | Builds with `PYTHONPATH=<host site-packages>:<tool-esptoolpy>`; RAM 16.0%, Flash 28.0%, SUCCESS in 22.8 s |
| Flash / upload | **blocked** | `os.open(port, O_RDWR)` → `PermissionError`; esptool reports "port doesn't exist" |
| Serial read | **blocked in practice** | `O_RDONLY` succeeds and returns a live fd, but `tcsetattr` → `PermissionError`, so baud cannot be set and reads yield 0 bytes |
| Serial write | **blocked** | requires `O_RDWR` |

Two findings drove this:

1. `~/.platformio` was *invisible* rather than absent — `os.path.exists` returned
   False while `os.makedirs` raised `FileExistsError`. A host-access grant fixed it.
2. `pip install platformio` fails inside the sandbox in **every** environment
   (shared, dedicated conda, venv): the wheel ships a `.vscode` template directory
   and the sandbox refuses to create dotfiles. Reusing the host's already-installed
   copy via `PYTHONPATH` is the working route, and `tool-esptoolpy` must be added
   to that path separately or the build dies at `bootloader.bin`.

**Consequence for this ADR.** The decision stands, with narrowed scope. `build`
and `check_stale` are now runnable by the agent directly (see `tools/pio.sh`), so
the MCP server's remaining irreplaceable value is exactly the hardware surface:
`flash`, `serial_read`, `serial_send`, `backup_flash`, `chip_info`. That is still
the half that caused the two most expensive failures, so the server remains
worth attaching — but the agent no longer needs it to compile.
