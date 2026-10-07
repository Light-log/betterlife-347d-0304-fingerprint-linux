# Proof of functionality

Real output from the working driver on Fedora 44 (KDE Plasma 6, kernel 7.2.8).
No fingerprint images are published here on purpose — a fingerprint ridge image
is biometric data. The evidence below is the terminal/daemon output.

## The reader is detected by libfprint

```
$ lsusb | grep 347d
Bus 003 Device 005: ID 347d:0304 Blestech Betterlife Fingerprint

$ fprint-list-supported-devices | grep -i 347d
347d:0304 | Blestech Betterlife 347d:0304
```

## Enrollment (via fprintd, same path GNOME/KDE Settings use)

```
$ fprintd-enroll
Using device /net/reactivated/Fprint/Device/0
Enrolling right-index-finger finger.
Enroll result: enroll-stage-passed
Enroll result: enroll-stage-passed
Enroll result: enroll-stage-passed
Enroll result: enroll-stage-passed
Enroll result: enroll-stage-passed
Enroll result: enroll-completed
```

## Verification — MATCH

```
$ fprintd-verify
Using device /net/reactivated/Fprint/Device/0
Listing enrolled fingers:
 - #0: right-index-finger
Verify started!
Verifying: right-index-finger
Verify result: verify-match (done)
```

## Match quality (SIGFM score, from the fprintd debug log)

```
fprintd[...]: Minutiae scan completed ...
fprintd[...]: sigfm score 645/20
fprintd[...]: report_verify_status: result verify-match
```

A genuine match scores **645** against a threshold of **20** — a very large,
reliable margin. (For comparison, the NBIS/bozorth3 minutiae matcher scored
**0/30** on every attempt: this 96×112 sensor only yields 3–5 minutiae, far too
few for minutiae matching, which is exactly why SIGFM is used instead.)

## System login / unlock

`pam_fprintd` is wired by `authselect` (`authselect enable-feature with-fingerprint`),
so the enrolled finger authenticates `sudo`, the lock screen and the display-manager
login — and fingerprints can be enrolled/removed graphically from
**System Settings → Users** (KDE) or **Settings → Users** (GNOME).
