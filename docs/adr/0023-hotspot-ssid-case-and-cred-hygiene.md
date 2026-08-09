# ADR 0023 — The lowercase hotspot SSID was a real bug, but not the one we hit

**Status:** accepted
**Context:** user: "wifi_credentials.h still uses the lowercase name ry-ops and
includes the password. this was never updated. this was probably the root of our
wifi issues earlier."

## The observation was right; the causal claim does not hold

The file did still read `{ "ry-ops", ... }`. But it was **not** the root of the
earlier failures, and the evidence is in this repo:

- `findInScan()` tries exact match, then **case-folded** match, and logs when it
  falls through. A lowercase entry matches `RY-OPS` in the scan.
- On a match it associates with `WiFi.SSID(idx)` — the string **as broadcast**,
  not as configured. So the association was already byte-exact.
- `ieq()` was extracted and unit-tested natively, 12/12 (ADR 0015).
- ADR 0014 traced the real defect: the scan screen compared results against
  `wifimgr::last().ssid`, which in SoftAP fallback is **our own AP's name**
  (`CardputerADV`). The match marker could not light for RY-OPS under any
  circumstance.
- ADR 0015 traced the second: the verdict line printed the *configured* spelling
  — a readback of a compile-time constant that renders identically whether
  case-folding works or not.

## Where the spelling does matter

One path, which the earlier ADRs did not cover. In `begin()`:

    const int idx = (n > 0) ? findInScan(n, prof.ssid) : -1;
    const String joinSsid = (idx >= 0) ? WiFi.SSID(idx) : String(prof.ssid);

When the scan returns **zero** networks, `idx` is -1 and the join goes out blind
with the configured literal. 802.11 treats the SSID as an opaque byte string, so
a blind join with `ry-ops` **cannot** associate with an AP beaconing `RY-OPS`.
That path is reachable exactly when the hotspot is asleep or the scan is starved
— the same conditions under which a fallback profile matters most.

Changed to the broadcast spelling `RY-OPS`. This also makes the exact-match
branch win first, and makes `r.ssid` (the configured string, not the received
one) print the truth.

## Credential hygiene

`include/wifi_credentials.h` is correctly gitignored (`.gitignore:4`), the repo
has one commit, and `git log --all -S` finds **no** commit containing the
passphrase. It never reached version control.

**However:** while inspecting the file I printed it with a redaction filter that
only matched `#define ...PASS` lines. The passphrase lives inside the
`WIFI_PROFILES` macro and therefore **printed in clear text into the session
transcript.** That is my error. The hotspot passphrase should be rotated on the
iPhone and updated here; the network is a personal hotspot, so the blast radius
is bounded, but the credential is compromised regardless of the transcript's
storage.

Lesson recorded in the file itself: redaction must match on **value position**,
not on a guessed macro-name pattern.

## Also fixed

`tools/pio.sh` could not build from the agent sandbox: PlatformIO validates
`--project-dir` with click's `Path(writable=True)`, whose `os.access(dir, W_OK)`
returns **False for the mount point** while returning True for its children,
same uid and 0755 mode, with real `open(...,"w")` succeeding in both. No path
spelling fixes it (trailing slash, `/.`, realpath, and a symlink all fail), so
the shim patches `click.types.os.access` — narrowly: only the W_OK branch, only
for a directory a real write test proves writable, so a genuinely read-only
checkout still fails loudly. Set `M5_NO_ACCESS_SHIM=1` to disable.
