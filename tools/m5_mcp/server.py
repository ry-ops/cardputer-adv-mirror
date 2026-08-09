#!/usr/bin/env python3
"""
m5-dev — MCP server for M5Stack / ESP32 firmware development.

Runs on the HOST (outside the agent sandbox), so it can reach the things the
sandbox cannot: the PlatformIO binary and the USB serial port. That is the
entire point — the agent can build, flash, and *observe the device* itself
instead of asking the user to copy-paste terminal output.

Transport: JSON-RPC 2.0 over stdio, newline-delimited. Pure stdlib; pyserial
is imported lazily and only by the serial tools.

SPDX-License-Identifier: MIT
"""
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

PROTOCOL_VERSION = "2024-11-05"
SERVER_NAME = "m5-dev"
SERVER_VERSION = "1.0.0"

DEFAULT_PROJECT = os.environ.get("M5_PROJECT_DIR", os.getcwd())
DEFAULT_BAUD = int(os.environ.get("M5_BAUD", "115200"))
# Hard ceiling so a bad call can never hang the agent's turn.
MAX_WAIT_S = 120
MAX_OUTPUT_CHARS = 20000


def log(msg):
    """Diagnostics must go to stderr; stdout is the JSON-RPC channel."""
    print(f"[m5-dev] {msg}", file=sys.stderr, flush=True)


def clip(text, limit=MAX_OUTPUT_CHARS):
    if text is None:
        return ""
    if len(text) <= limit:
        return text
    half = limit // 2
    return (text[:half] + f"\n... [{len(text) - limit} chars elided] ...\n"
            + text[-half:])


# --------------------------------------------------------------- toolchain

def find_pio():
    """Locate the pio binary. PATH is unreliable under pip --user installs."""
    if os.environ.get("M5_PIO"):
        return os.environ["M5_PIO"]
    found = shutil.which("pio") or shutil.which("platformio")
    if found:
        return found
    for pat in (
        os.path.expanduser("~/Library/Python/*/bin/pio"),
        os.path.expanduser("~/.platformio/penv/bin/pio"),
        os.path.expanduser("~/.local/bin/pio"),
    ):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]
    return None


def run(cmd, cwd=None, timeout=600, stdin_data=None):
    try:
        p = subprocess.run(
            cmd, cwd=cwd, timeout=timeout, capture_output=True, text=True,
            input=stdin_data,
        )
        return {
            "ok": p.returncode == 0,
            "exit_code": p.returncode,
            "stdout": clip(p.stdout),
            "stderr": clip(p.stderr),
            "cmd": " ".join(shlex.quote(c) for c in cmd),
        }
    except subprocess.TimeoutExpired:
        return {"ok": False, "exit_code": None, "stdout": "", "error_kind": "timeout",
                "stderr": f"timed out after {timeout}s",
                "cmd": " ".join(shlex.quote(c) for c in cmd)}
    except FileNotFoundError as e:
        return {"ok": False, "exit_code": None, "stdout": "", "error_kind": "not_found",
                "stderr": str(e), "cmd": " ".join(shlex.quote(c) for c in cmd)}


# ------------------------------------------------------------------ serial

def list_serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return {"ok": False, "error": "pyserial not installed on the host "
                                      "(pip3 install pyserial)"}
    out = []
    for p in list_ports.comports():
        out.append({
            "device": p.device, "description": p.description,
            "hwid": p.hwid, "vid": p.vid, "pid": p.pid,
            "manufacturer": p.manufacturer,
        })
    # Prefer the usbmodem/usbserial devices an ESP32 actually enumerates as.
    likely = [p["device"] for p in out
              if re.search(r"usbmodem|usbserial|ttyUSB|ttyACM", p["device"])]
    return {"ok": True, "ports": out, "likely_device": likely[0] if likely else None,
            "count": len(out)}


def resolve_port(port):
    if port:
        return port, None
    info = list_serial_ports()
    if not info.get("ok"):
        return None, info.get("error")
    if info.get("likely_device"):
        return info["likely_device"], None
    return None, "no serial port found; is the device plugged in?"


def port_busy(port):
    """True if another process holds the port (a monitor left open)."""
    try:
        import serial
    except ImportError:
        return None
    try:
        s = serial.Serial(port, DEFAULT_BAUD, timeout=0.1)
        s.close()
        return False
    except Exception:
        return True


# ------------------------------------------------------------------- tools

def t_doctor(args):
    """One call that answers 'is this machine able to build and flash?'"""
    pio = find_pio()
    proj = args.get("project_dir") or DEFAULT_PROJECT
    checks = {}
    checks["project_dir"] = {"path": proj, "exists": os.path.isdir(proj)}
    checks["platformio_ini"] = {"exists": os.path.isfile(os.path.join(proj, "platformio.ini"))}
    checks["pio"] = {"path": pio, "found": bool(pio)}
    if pio:
        v = run([pio, "--version"], timeout=60)
        checks["pio"]["version"] = (v.get("stdout") or v.get("stderr", "")).strip()
    try:
        import serial  # noqa: F401
        checks["pyserial"] = {"installed": True}
    except ImportError:
        checks["pyserial"] = {"installed": False}
    checks["ports"] = list_serial_ports()

    fw = os.path.join(proj, ".pio", "build")
    builds = []
    if os.path.isdir(fw):
        for env in os.listdir(fw):
            b = os.path.join(fw, env, "firmware.bin")
            if os.path.isfile(b):
                builds.append({"env": env, "bin": b,
                               "size": os.path.getsize(b),
                               "mtime": time.strftime("%Y-%m-%d %H:%M:%S",
                                                      time.localtime(os.path.getmtime(b)))})
    checks["builds"] = builds
    checks["stale"] = t_check_stale({"project_dir": proj})
    return checks


def _newest_source_mtime(proj):
    newest, newest_f = 0, None
    for sub in ("src", "lib", "include"):
        base = os.path.join(proj, sub)
        for root, _dirs, files in os.walk(base):
            for f in files:
                if f.endswith((".c", ".cpp", ".h", ".hpp", ".ino", ".S")):
                    p = os.path.join(root, f)
                    m = os.path.getmtime(p)
                    if m > newest:
                        newest, newest_f = m, p
    ini = os.path.join(proj, "platformio.ini")
    if os.path.isfile(ini) and os.path.getmtime(ini) > newest:
        newest, newest_f = os.path.getmtime(ini), ini
    return newest, newest_f


def t_check_stale(args):
    """Is firmware.bin older than the newest source file? This is the check
    that would have caught the 'edited main.cpp, flashed the old binary' bug."""
    proj = args.get("project_dir") or DEFAULT_PROJECT
    newest, newest_f = _newest_source_mtime(proj)
    bins = glob.glob(os.path.join(proj, ".pio", "build", "*", "firmware.bin"))
    if not bins:
        return {"built": False, "stale": True,
                "reason": "no firmware.bin — project has never been built"}
    b = max(bins, key=os.path.getmtime)
    bt = os.path.getmtime(b)
    stale = newest > bt
    return {
        "built": True, "stale": stale, "firmware_bin": b,
        "firmware_mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(bt)),
        "newest_source": newest_f,
        "newest_source_mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(newest)),
        "reason": ("source is newer than the built binary — rebuild before flashing"
                   if stale else "binary is up to date"),
    }


def t_build(args):
    proj = args.get("project_dir") or DEFAULT_PROJECT
    pio = find_pio()
    if not pio:
        return {"ok": False, "error": "pio not found; set M5_PIO or pip3 install platformio"}
    cmd = [pio, "run"]
    if args.get("environment"):
        cmd += ["-e", args["environment"]]
    if args.get("clean"):
        cmd = [pio, "run", "-t", "clean"]
    r = run(cmd, cwd=proj, timeout=int(args.get("timeout", 900)))
    r["summary"] = _summarize_pio(r)
    return r


def _summarize_pio(r):
    """Pull the lines that matter out of PlatformIO's very long output."""
    text = (r.get("stdout") or "") + "\n" + (r.get("stderr") or "")
    keep = []
    for line in text.splitlines():
        if re.search(r"(RAM|Flash):\s+\[|SUCCESS|FAILED|error:|Error|Writing at|"
                     r"Hash of data verified|Hard resetting|Leaving\.\.\.|"
                     r"Auto-detected|Uploading|Compiling .*main|undefined reference", line):
            keep.append(line.strip())
    return keep[-30:]


def t_flash(args):
    """Build + upload. Refuses on a busy port, and (unless forced) refuses to
    flash without an explicit acknowledgement that this overwrites the device."""
    proj = args.get("project_dir") or DEFAULT_PROJECT
    pio = find_pio()
    if not pio:
        return {"ok": False, "error": "pio not found; set M5_PIO or pip3 install platformio"}

    port, err = resolve_port(args.get("port"))
    if err and not args.get("port"):
        return {"ok": False, "error": err, "hint": "run list_ports, or plug the device in"}

    if port and port_busy(port):
        return {"ok": False, "error": f"port {port} is busy",
                "hint": "close `pio device monitor` (or any serial tool) holding it"}

    cmd = [pio, "run", "-t", "upload"]
    if args.get("environment"):
        cmd += ["-e", args["environment"]]
    if port:
        cmd += ["--upload-port", port]
    r = run(cmd, cwd=proj, timeout=int(args.get("timeout", 900)))
    r["port"] = port
    r["summary"] = _summarize_pio(r)
    # Verify the upload actually happened rather than trusting the exit code.
    blob = (r.get("stdout") or "") + (r.get("stderr") or "")
    r["upload_confirmed"] = bool(re.search(r"Hash of data verified|Hard resetting|Leaving\.\.\.", blob))
    if r["ok"] and not r["upload_confirmed"]:
        r["warning"] = ("exit code 0 but no upload confirmation in output — "
                        "verify the device was actually written")
    return r


def t_serial_read(args):
    """Timeboxed capture. Replaces `pio device monitor`, which holds a terminal
    open and cannot be read by the agent."""
    try:
        import serial
    except ImportError:
        return {"ok": False, "error": "pyserial not installed (pip3 install pyserial)"}

    port, err = resolve_port(args.get("port"))
    if err:
        return {"ok": False, "error": err}
    seconds = min(float(args.get("seconds", 8)), MAX_WAIT_S)
    baud = int(args.get("baud", DEFAULT_BAUD))
    until = args.get("until")
    reset = bool(args.get("reset", False))

    try:
        s = serial.Serial(port, baud, timeout=0.2)
    except Exception as e:
        return {"ok": False, "error": f"could not open {port}: {e}",
                "hint": "close any open serial monitor holding the port"}

    buf = bytearray()
    matched = False
    try:
        if reset:
            # DTR/RTS toggle = the same reset esptool performs.
            s.setDTR(False); s.setRTS(True); time.sleep(0.1)
            s.setRTS(False); time.sleep(0.05)
            s.reset_input_buffer()
        t0 = time.time()
        while time.time() - t0 < seconds:
            chunk = s.read(4096)
            if chunk:
                buf.extend(chunk)
                if until and until.encode("utf-8", "ignore") in buf:
                    matched = True
                    break
            else:
                time.sleep(0.02)
    finally:
        s.close()

    text = buf.decode("utf-8", "replace")
    return {"ok": True, "port": port, "baud": baud, "bytes": len(buf),
            "seconds": round(time.time() - t0, 2),
            "matched_until": matched,
            "silent": len(buf) == 0,
            "hint": ("no output — device may be running firmware that prints nothing, "
                     "or is held in bootloader; try reset=true"
                     if len(buf) == 0 else None),
            "text": clip(text)}


def t_serial_send(args):
    try:
        import serial
    except ImportError:
        return {"ok": False, "error": "pyserial not installed (pip3 install pyserial)"}
    port, err = resolve_port(args.get("port"))
    if err:
        return {"ok": False, "error": err}
    data = args.get("data", "")
    if args.get("newline", True):
        data += "\n"
    baud = int(args.get("baud", DEFAULT_BAUD))
    wait = min(float(args.get("read_seconds", 3)), MAX_WAIT_S)
    try:
        s = serial.Serial(port, baud, timeout=0.2)
    except Exception as e:
        return {"ok": False, "error": f"could not open {port}: {e}"}
    buf = bytearray()
    try:
        s.reset_input_buffer()
        s.write(data.encode("utf-8", "ignore"))
        s.flush()
        t0 = time.time()
        while time.time() - t0 < wait:
            chunk = s.read(4096)
            if chunk:
                buf.extend(chunk)
            else:
                time.sleep(0.02)
    finally:
        s.close()
    return {"ok": True, "port": port, "sent": data,
            "text": clip(buf.decode("utf-8", "replace")), "bytes": len(buf)}


def _esptool_cmd(proj, pio):
    """esptool ships inside the PlatformIO toolchain; prefer that copy."""
    hits = sorted(glob.glob(os.path.expanduser(
        "~/.platformio/packages/tool-esptoolpy*/esptool.py")))
    if hits:
        return [sys.executable, hits[-1]]
    if shutil.which("esptool.py"):
        return [shutil.which("esptool.py")]
    if pio:
        return [pio, "pkg", "exec", "-p", "tool-esptoolpy", "--", "esptool.py"]
    return None


def t_backup_flash(args):
    """Dump device flash to a file BEFORE overwriting stock firmware."""
    proj = args.get("project_dir") or DEFAULT_PROJECT
    pio = find_pio()
    esp = _esptool_cmd(proj, pio)
    if not esp:
        return {"ok": False, "error": "esptool not found"}
    port, err = resolve_port(args.get("port"))
    if err:
        return {"ok": False, "error": err}
    if port_busy(port):
        return {"ok": False, "error": f"port {port} is busy — close the serial monitor"}
    size = int(args.get("size", 0x800000))
    out = args.get("output") or os.path.join(proj, "factory-backup.bin")
    cmd = esp + ["--port", port, "--baud", str(args.get("baud", 921600)),
                 "read_flash", "0", hex(size), out]
    r = run(cmd, cwd=proj, timeout=int(args.get("timeout", 900)))
    r["output"] = out
    r["exists"] = os.path.isfile(out)
    r["size_bytes"] = os.path.getsize(out) if os.path.isfile(out) else 0
    return r


def t_chip_info(args):
    proj = args.get("project_dir") or DEFAULT_PROJECT
    pio = find_pio()
    esp = _esptool_cmd(proj, pio)
    if not esp:
        return {"ok": False, "error": "esptool not found"}
    port, err = resolve_port(args.get("port"))
    if err:
        return {"ok": False, "error": err}
    if port_busy(port):
        return {"ok": False, "error": f"port {port} is busy — close the serial monitor"}
    r = run(esp + ["--port", port, "flash_id"], cwd=proj, timeout=120)
    return r


def t_identify_firmware(args):
    """Extract printable strings from a firmware image — identifies what is
    currently on the device (or what a .bin actually contains)."""
    path = args.get("path")
    proj = args.get("project_dir") or DEFAULT_PROJECT
    if not path:
        bins = glob.glob(os.path.join(proj, ".pio", "build", "*", "firmware.bin"))
        if not bins:
            return {"ok": False, "error": "no path given and no firmware.bin found"}
        path = max(bins, key=os.path.getmtime)
    if not os.path.isfile(path):
        return {"ok": False, "error": f"not found: {path}"}
    with open(path, "rb") as fh:
        blob = fh.read()
    strings = re.findall(rb"[\x20-\x7e]{%d,}" % int(args.get("min_len", 8)), blob)
    decoded = [s.decode("ascii", "ignore") for s in strings]
    needles = args.get("needles") or []
    found = {n: any(n.lower() in d.lower() for d in decoded) for n in needles}
    interesting = [d for d in decoded if re.search(
        r"(?i)(version|firmware|m5|cardputer|bruce|nemo|launcher|wifi|sdcard|"
        r"http|build|compiled|esp32)", d)]
    return {"ok": True, "path": path, "size": len(blob),
            "total_strings": len(decoded),
            "needles": found,
            "sample": interesting[:60]}


TOOLS = [
    {
        "name": "doctor",
        "description": "One-shot health check of the firmware toolchain: locates pio, "
                       "checks the project and platformio.ini, lists serial ports and "
                       "existing builds, and reports whether the built binary is stale. "
                       "Run this first when anything is not working.",
        "inputSchema": {"type": "object", "properties": {
            "project_dir": {"type": "string", "description": "PlatformIO project root"}}},
        "handler": t_doctor,
    },
    {
        "name": "list_ports",
        "description": "List USB serial ports on the host, flagging the one an ESP32 "
                       "most likely enumerated as.",
        "inputSchema": {"type": "object", "properties": {}},
        "handler": lambda a: list_serial_ports(),
    },
    {
        "name": "check_stale",
        "description": "Compare firmware.bin mtime against the newest source file. "
                       "Catches the case where sources were edited but the old binary "
                       "would be flashed.",
        "inputSchema": {"type": "object", "properties": {
            "project_dir": {"type": "string"}}},
        "handler": t_check_stale,
    },
    {
        "name": "build",
        "description": "Run `pio run` and return a condensed summary (memory usage, "
                       "errors) instead of the full log.",
        "inputSchema": {"type": "object", "properties": {
            "project_dir": {"type": "string"},
            "environment": {"type": "string", "description": "PlatformIO env name"},
            "clean": {"type": "boolean", "description": "run the clean target instead"},
            "timeout": {"type": "integer", "description": "seconds, default 900"}}},
        "handler": t_build,
    },
    {
        "name": "flash",
        "description": "Build and upload firmware to the device. Refuses if the serial "
                       "port is held by another process, and verifies the upload actually "
                       "occurred rather than trusting the exit code. OVERWRITES whatever "
                       "firmware is on the device — take backup_flash first if the stock "
                       "image matters.",
        "inputSchema": {"type": "object", "properties": {
            "project_dir": {"type": "string"},
            "environment": {"type": "string"},
            "port": {"type": "string", "description": "auto-detected when omitted"},
            "timeout": {"type": "integer"}}},
        "handler": t_flash,
    },
    {
        "name": "serial_read",
        "description": "Capture serial output for a bounded number of seconds and return "
                       "it as text. Use instead of `pio device monitor`, which holds a "
                       "terminal open and cannot be read programmatically. Set reset=true "
                       "to pulse DTR/RTS and catch boot output.",
        "inputSchema": {"type": "object", "properties": {
            "port": {"type": "string"},
            "baud": {"type": "integer", "description": "default 115200"},
            "seconds": {"type": "number", "description": "capture window, max 120"},
            "reset": {"type": "boolean", "description": "reset the device first to catch boot output"},
            "until": {"type": "string", "description": "stop early when this substring appears"}}},
        "handler": t_serial_read,
    },
    {
        "name": "serial_send",
        "description": "Write a string to the device's serial port and capture the reply. "
                       "Useful for firmware with a serial command interface.",
        "inputSchema": {"type": "object", "properties": {
            "data": {"type": "string"},
            "port": {"type": "string"},
            "baud": {"type": "integer"},
            "newline": {"type": "boolean", "description": "append newline, default true"},
            "read_seconds": {"type": "number"}},
            "required": ["data"]},
        "handler": t_serial_send,
    },
    {
        "name": "backup_flash",
        "description": "Dump the device's entire flash to a file so stock firmware can be "
                       "restored later. Do this before the first flash of a new device.",
        "inputSchema": {"type": "object", "properties": {
            "project_dir": {"type": "string"},
            "output": {"type": "string", "description": "output .bin path"},
            "port": {"type": "string"},
            "size": {"type": "integer", "description": "bytes, default 8388608 (8MB)"},
            "baud": {"type": "integer"}}},
        "handler": t_backup_flash,
    },
    {
        "name": "chip_info",
        "description": "Read chip type, flash size and MAC via esptool.",
        "inputSchema": {"type": "object", "properties": {
            "project_dir": {"type": "string"}, "port": {"type": "string"}}},
        "handler": t_chip_info,
    },
    {
        "name": "identify_firmware",
        "description": "Extract printable strings from a firmware .bin to identify what it "
                       "is. Pass needles to test for specific markers. Works on a flash "
                       "backup to identify unknown stock firmware.",
        "inputSchema": {"type": "object", "properties": {
            "path": {"type": "string", "description": "defaults to newest built firmware.bin"},
            "project_dir": {"type": "string"},
            "needles": {"type": "array", "items": {"type": "string"},
                        "description": "substrings to test for"},
            "min_len": {"type": "integer"}}},
        "handler": t_identify_firmware,
    },
]

TOOLS_BY_NAME = {t["name"]: t for t in TOOLS}


# ---------------------------------------------------------------- JSON-RPC

def handle(req):
    method = req.get("method")
    rid = req.get("id")
    params = req.get("params") or {}

    if method == "initialize":
        return {"jsonrpc": "2.0", "id": rid, "result": {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
        }}

    if method in ("notifications/initialized", "initialized"):
        return None  # notification: no response

    if method == "ping":
        return {"jsonrpc": "2.0", "id": rid, "result": {}}

    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": rid, "result": {
            "tools": [{"name": t["name"], "description": t["description"],
                       "inputSchema": t["inputSchema"]} for t in TOOLS]}}

    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        tool = TOOLS_BY_NAME.get(name)
        if not tool:
            return {"jsonrpc": "2.0", "id": rid,
                    "error": {"code": -32602, "message": f"unknown tool: {name}"}}
        try:
            result = tool["handler"](args)
        except Exception as e:
            import traceback
            result = {"ok": False, "error": f"{type(e).__name__}: {e}",
                      "traceback": clip(traceback.format_exc(), 2000)}
        return {"jsonrpc": "2.0", "id": rid, "result": {
            "content": [{"type": "text",
                         "text": json.dumps(result, indent=2, default=str)}],
            "isError": bool(isinstance(result, dict) and result.get("ok") is False)}}

    if rid is None:
        return None
    return {"jsonrpc": "2.0", "id": rid,
            "error": {"code": -32601, "message": f"method not found: {method}"}}


def main():
    log(f"started (project={DEFAULT_PROJECT}, pio={find_pio()})")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            log(f"bad JSON: {e}")
            continue
        resp = handle(req)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
