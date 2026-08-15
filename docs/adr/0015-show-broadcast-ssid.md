# ADR 0015 — Show the broadcast SSID, not the configured one

**Status:** Accepted — implemented
**Context:** user: "it's still using the lowercase ry-ops not the uppercase RY-OPS"

## The observation was correct; the inference it invited was not

The screen did show `ry-ops`. That is the string in `wifi_credentials.h`, and
the verdict line printed `wifimgr::targetSsid()` -- the configured spelling.

The reasonable reading of that is "the case fix from ADR 0007 didn't survive."
It did. The matcher is fine, and the screen simply wasn't reporting on it.

## The matcher is provably correct

`ieq()` in `wifi_manager_rt.cpp` was extracted and unit-tested natively
(`c++ -std=c++11`), 12 cases, **12/12 passing** -- both case directions,
prefix/suffix mismatches, empty strings, null inputs, and a negative control
against the fallback AP's own name. The comparison is not the defect.

## The actual defect: the display had no diagnostic value

Printing the configured spelling renders **byte-identically whether
case-folding works or not**. It is a readback of a compile-time constant. The
one thing a scan verdict exists to report -- what the radio actually received
off the air -- was the one thing it did not show.

This is precisely the ADR 0009 failure repeated. There, the status line was
changed to show the mDNS name, which resolves in both AP and STA mode and
therefore looked the same whether the join had succeeded or failed. Same shape:
**a value that cannot vary with the condition being diagnosed carries no
information about it.**

## Fix

The FOUND verdict now prints the broadcast SSID from the scan result, plus an
explicit marker when it differs from what was configured:

    RY-OPS FOUND ch6 -24dBm ~case

`~case` means: these differ in case, and the match succeeded anyway. That is
ADR 0007's fix reporting itself in operation rather than being invisible.

The NOT RECEIVED branch still prints the configured name -- with no match there
is no broadcast form to show, and the configured spelling is the only honest
value available.

## Rule

A diagnostic display must show a **measured** value, never a configured one.
If the field cannot change when the fault is present, it does not belong on a
diagnostic screen.

Fifth instance of the proxy-question pattern (0007, 0009, 0011, 0013, 0014).

## Consequences

- `+52 B` flash.
- The verdict line now distinguishes "found, exact", "found, case differs", and
  "not received" -- three states that previously rendered as two.
