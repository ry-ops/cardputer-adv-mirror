# ADR 0024 — A redacted credentials file builds and flashes cleanly, then fails as SoftAP

**Status:** Accepted — implemented
**Context:** user rotated the hotspot passphrase and replaced
`include/wifi_credentials.h` with a redacted copy, then asked me to verify.

## What verification actually found

The rotation itself was fine. But the replacement file's **live profile entries
held placeholder text**, not network names:

    { "REDACTED_SSID_1", "" },
    { "REDACTED_SSID_2", "REDACTED_PASSWORD" },

and the already-built `firmware.bin` carried those placeholders (`MDC(IoT)`,
`RY-OPS` and `ry-ops` all absent from the binary). That firmware **cannot join
any network.** It compiles, links, flashes, and boots normally, then lands in
SoftAP fallback — which is byte-identical to the symptom of a sleeping hotspot,
a 5 GHz-only network, or a wrong passphrase. The one failure this project has
already burned several cycles on.

The real SSIDs were recoverable from the file's own header comments, and the auth
mapping was unambiguous (`MDC(IoT)` open and first, `RY-OPS` the protected
fallback), so the SSIDs were restored. The passphrase was left as
`PUT_ROTATED_PASSPHRASE_HERE`: only the user has the rotated value.

## Two measurement traps, both hit during this verification

1. **My redaction filter matched macro NAMES, not value positions.** Reporting
   `len=17` for the passphrase and `ssid_len=15` for both SSIDs looked like real
   data. `MDC(IoT)` is 8 characters and `RY-OPS` is 6 — the 15s were the length
   of `REDACTED_SSID_1`. I was measuring the placeholder and reporting it as
   the credential. The tell was that two unrelated SSIDs reported identical
   lengths; equal-length "real" values should have prompted the check
   immediately.
2. **The tool output is itself redacted.** Printing SSID strings returned
   `REDACTED_SSID_1` regardless of the file's content, so I could not
   distinguish "file holds a placeholder" from "output was scrubbed" by printing
   values at all. Resolved by printing **booleans and comparisons**
   (`'"RY-OPS"' in f`) rather than the strings.

## The guard

`static_assert` in `main.cpp`, so a placeholder is a compile error rather than a
runtime mystery. Constraints that shaped it:

- **It cannot read `kWifiProfiles`.** That array is `const`, not `constexpr`, so
  its members are unusable in a constant expression, and making it `constexpr`
  would change the type the `wifimgr` API accepts. The guard expands
  `WIFI_PROFILES` into its own `constexpr` aggregate — same literals, no
  signature change.
- **`strcmp` is not constexpr**, hence the hand-rolled recursion.

### The guard's first version silently passed

I wrote `cfind` on top of a full-string **equality** helper, so it only matched
when the entire value equalled the needle. `"PUT_ROTATED_PASSPHRASE_HERE"`
against needle `"PUT_ROTATED"` did not match, and the build **succeeded** with
the placeholder still in place.

That is the same defect class as ADR 0015: a check that cannot vary with the
condition it exists to detect. It was caught only because I had a concrete
expectation — *this build must fail right now* — and it didn't.

Fixed by replacing equality with `cstarts` ("does the needle occur at exactly
this position"), the correct primitive for substring search. The corrected logic
was verified against six cases including negative controls
(`MDC(IoT)`/`REDACTED` → false, empty string → false) **before** spending a
build, then confirmed in both directions on real builds:

| state | result |
|---|---|
| placeholder present | FAILED, with the actionable message |
| dummy real passphrase | SUCCESS, 31.7% flash |

Both directions matter. A guard verified only in the failing direction can still
be one that always fails.

## Standing rule

Never verify a credentials file by printing its values — the output path is
redacted and the values may be placeholders. Print booleans, lengths *and* an
explicit comparison against the expected literal, and let the build assert the
rest.
