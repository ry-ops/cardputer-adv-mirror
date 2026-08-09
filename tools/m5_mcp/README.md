# m5-dev — MCP server for M5Stack / ESP32 firmware work

Runs **on the host**, outside the agent sandbox. That is the whole point: the
sandbox can see `/dev/cu.usbmodem101` but gets `Operation not permitted` when
it tries to open it, and cannot execute `pio` at all. This server can do both,
so the agent can build, flash, and *read the device* without copy-pasted
terminal output.

Pure stdlib except `pyserial`, which only the serial tools import.

## Install

```bash
pip3 install pyserial            # required for serial_read / serial_send
pip3 install platformio          # if not already installed
```

Add to your MCP client config (contents of `mcp-config.json`):

```json
{
  "mcpServers": {
    "m5-dev": {
      "command": "python3",
      "args": [
        "/Users/ryandahlberg/Projects/cardputer-adv-mirror/tools/m5_mcp/server.py"
      ],
      "env": {
        "M5_PROJECT_DIR": "/Users/ryandahlberg/Projects/cardputer-adv-mirror",
        "M5_BAUD": "115200"
      }
    }
  }
}
```

`M5_PROJECT_DIR` sets the default project so tools can be called with no
arguments. `M5_PIO` can pin the pio binary if autodetection fails.

## Tools

| Tool | Purpose |
|---|---|
| `doctor` | One-shot health check: pio path, project, ports, builds, staleness |
| `list_ports` | USB serial ports, flagging the likely ESP32 |
| `check_stale` | Is `firmware.bin` older than the newest source file? |
| `build` | `pio run`, returning a condensed summary not the full log |
| `flash` | Build + upload; refuses on busy port, verifies the write happened |
| `serial_read` | Timeboxed capture, optional `reset` to catch boot output |
| `serial_send` | Write to the device and capture the reply |
| `backup_flash` | Dump flash to a file before overwriting stock firmware |
| `chip_info` | Chip type, flash size, MAC via esptool |
| `identify_firmware` | Strings out of a `.bin` to identify unknown firmware |

## Design notes

Each tool exists because a specific failure cost real time in this project:

- **`check_stale`** — `main.cpp` was edited at 07:35 while `firmware.bin` dated
  from 07:32. Flashing would have silently uploaded the older code.
- **`flash` upload verification** — `pio run` (build) succeeded and was mistaken
  for a completed upload. The device still held stock firmware for two debugging
  cycles. `flash` now greps for `Hash of data verified` / `Hard resetting` and
  warns when exit code 0 arrives without them.
- **`flash` busy-port refusal** — an open `pio device monitor` holds the port and
  makes uploads fail with a confusing error.
- **`serial_read`** — `pio device monitor` occupies a terminal and cannot be read
  programmatically. A bounded capture returns text the agent can actually see,
  and reports `silent: true` explicitly rather than returning an empty string.
- **`identify_firmware`** — stock firmware printed `Failed to mount SDCARD`,
  strings that appear nowhere in this project. Comparing strings against a `.bin`
  proves what is really on the device.
- **`backup_flash`** — flashing is irreversible for whatever shipped on the
  device.

## Safety

`flash` and `backup_flash` touch hardware. `flash` overwrites the device
permanently — take a backup first if the stock image matters. Both refuse to run
while another process holds the serial port.

Output from `build`/`flash` is condensed to the lines that matter and all output
is clipped, so a long PlatformIO log cannot flood the agent's context.
