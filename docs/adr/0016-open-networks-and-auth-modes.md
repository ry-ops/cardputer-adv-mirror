# ADR 0016 — Open networks, auth modes, and the WPA2 association floor

**Status:** Accepted — implemented
**Context:** switching the target network to MDC(IoT), which has no passphrase

## Three real defects, found before the first flash

### 1. The framework refuses to associate below WPA2

`WiFiSTAClass::_minSecurity` defaults to `WIFI_AUTH_WPA2_PSK`
(`WiFiSTA.cpp:118`), and `wifi_sta_config()` copies it into
`sta.threshold.authmode`. A WPA-PSK-only or WEP network is rejected **before
any air traffic**, and it surfaces as a generic association timeout with no
indication that a policy, not the radio, refused it.

Corporate IoT SSIDs are exactly where legacy auth still lives. Fixed with
`WiFi.setMinSecurity(WIFI_AUTH_WEP)` before the join.

### 2. Empty passphrase is not the same as no passphrase

Read `wifi_sta_config()`:

    if(password != NULL && password[0] != 0){
        wifi_config->sta.threshold.authmode = min_security;

The password branch keys on non-NULL AND non-empty, so `""` happens to work
here -- but the intent is unclear at the call site and depends on that exact
guard. The call site now converts an empty configured password to `nullptr`
explicitly, so open association is selected on purpose rather than by accident.

### 3. "check passphrase" is nonsense on an open network

The failure detail was hardcoded to `"visible but refused - check passphrase"`.
On MDC(IoT) there is no passphrase, so that message would have sent us hunting
for a credential that does not exist -- the same class of misleading diagnostic
as ADR 0014 and 0015.

Failure detail is now derived from the AP's advertised auth mode:

| Advertised | Configured | Detail |
|---|---|---|
| ENTERPRISE | anything | `802.1X enterprise - needs EAP identity, not a passphrase` |
| non-OPEN | empty | `AP wants a passphrase but none is configured` |
| OPEN | non-empty | `AP is open but a passphrase is configured - clear it` |
| OPEN | empty | `open network refused association (MAC filter or AP limit?)` |

## Auth mode is now visible everywhere

`ScanEntry` carries `auth`, and the scan screen, verdict line, and serial dump
all print it. This matters because **enterprise and open look identical to a
passphrase-based join attempt until you can see the auth mode** -- both just
time out.

If MDC(IoT) reads `ENT`, no amount of retrying will work and we need a
different approach entirely. That is worth knowing in one glance rather than
three flash cycles.

## Association is not connectivity

An open corporate SSID is commonly gated by a **captive portal**: association
succeeds, DHCP succeeds, and every outbound request is intercepted until a
browser completes a sign-on the ESP32 cannot perform. `Outcome::Connected` with
a real IP would look like unqualified success.

`Result::openNet` is set on an open join, and the Network screen shows
`[OPEN]` beside the SSID plus an explicit portal warning. Same principle as ADR
0015: do not let a display imply more than was actually measured.

## Build note: never put `//` inside a continued macro

Adding a `//` comment inside `WIFI_PROFILES` broke the build:

    include/wifi_credentials.h:35:5: error: expected unqualified-id before '{'

Line splicing (translation phase 2) runs **before** comment removal (phase 3),
so a `//` on a backslash-continued line swallows the following line into the
comment and the macro silently loses profiles. The prose moved above the
`#define`, with a warning recorded there.

## Verification

Confirmed against the built binary rather than the source:

    strings firmware.bin | grep -E "^MDC\(IoT\)$|^ry-ops$"
    -> MDC(IoT), ry-ops, CardputerADV

Both profiles and all new auth labels and detail strings are present.

## Consequences

- `+820 B` flash.
- `ry-ops` retained as the second profile; profiles are tried in order, so this
  costs nothing when MDC(IoT) is present.
- `ieq()` unit test (ADR 0015) still applies -- SSID matching is unchanged.
