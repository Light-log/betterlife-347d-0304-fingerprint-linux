# Linux driver for the Blestech "Betterlife Fingerprint" reader (USB `347d:0304`)

A working **libfprint** driver for the **Blestech / Betterlife** USB fingerprint
reader with USB ID **`347d:0304`** — the one that shows up in `lsusb` as
`Blestech Betterlife Fingerprint` and that has **no driver on Linux**, so the
fingerprint sensor in your laptop just does nothing.

With this driver it works end to end: **enroll, verify, unlock, and login** —
from the terminal *and* from the graphical **Settings → Users** panel (GNOME/KDE),
exactly like Windows.

> Enroll completes in 5 taps and a genuine finger verifies with a huge margin
> (SIGFM score **645** vs a threshold of **20**). See
> [`docs/proof/EVIDENCE.md`](docs/proof/EVIDENCE.md).

---

## Does this affect me?

Run:

```bash
lsusb | grep -i 347d
```

If you see `347d:0304 ... Blestech Betterlife Fingerprint`, **yes** — this is for you.

This reader is soldered into a lot of **generic / rebadged laptops** (Tongfang /
Clevo "barebone" chassis sold under many brand names — the DMI is often blank or
an `A140`-class string). If your laptop's fingerprint reader doesn't work on Linux
and the USB ID is `347d:0304`, this driver should make it work.

**Tested on:** Fedora 44, KDE Plasma 6, kernel 7.2, libfprint 1.94.100, reader
firmware `7.0.14.3-2000`. It should work on any distro with libfprint + fprintd
(Ubuntu, Arch, …) — see *Build from source*.

➡️ **If it works (or doesn't) on your laptop, please
[open an issue](../../issues) with your laptop model and `lsusb` line** so we can
build a compatibility list. 🙏

---

## How it works (short version)

| Piece | What it does |
|---|---|
| Secure channel | The device speaks an encrypted protocol (**SM2 / SM3 / SM4**, Chinese ShangMi). The driver implements the full handshake. |
| Finger detection | The **device detects the finger itself** and signals the host (like the Windows driver). The driver waits for that signal instead of polling — so it is fast and never wedges the firmware. |
| Image | Captures the full **96×112** image in one transfer. |
| Matching | Uses **SIGFM** (SIFT features, OpenCV), because this tiny sensor yields too few minutiae for the classic NBIS/bozorth3 matcher. |

Full reverse-engineering notes: [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

---

## Install

The driver lives *inside* libfprint (libfprint has no plugin system), so installing
it means installing a patched libfprint. Two ways:

### Fedora 44 — download the prebuilt RPM (easiest)

A ready-to-install RPM is attached to the
[**latest release**](../../releases/latest):

```bash
sudo dnf install ./libfprint-1.94.100-2.sigfm.fc44.x86_64.rpm
```

Then `fprintd-enroll` / `fprintd-verify`, or enroll from **System Settings → Users**.
(`Epoch: 1` keeps `dnf update` from overwriting it.)

### Build the RPM yourself (Fedora)

```bash
# build deps (one time)
sudo dnf builddep -y libfprint
sudo dnf install -y opencv-devel rpm-build rpmdevtools

# build the patched libfprint RPM
rpmdev-setuptree
dnf download --source libfprint
rpm -i libfprint-1.94.100-*.src.rpm
cp patches/betterlife-sigfm.patch ~/rpmbuild/SOURCES/
# use the spec in packaging/libfprint-sigfm.spec (adds opencv-devel, Patch0, Epoch:1)
cp packaging/libfprint-sigfm.spec ~/rpmbuild/SPECS/libfprint.spec
rpmbuild -bb --nocheck ~/rpmbuild/SPECS/libfprint.spec
sudo dnf install -y ~/rpmbuild/RPMS/*/libfprint-1*.rpm
```

`Epoch: 1` in the spec keeps your patched build from being overwritten by routine
`dnf update`s.

### B) Build from source (any distro) — quick to test

```bash
sudo dnf install -y opencv-devel meson gcc gcc-c++   # or your distro's equivalents
git clone https://gitlab.freedesktop.org/libfprint/libfprint
cd libfprint && git checkout v1.94.100
git apply ../patches/betterlife-sigfm.patch
meson setup build -Ddrivers=all
ninja -C build
```

Then either `sudo meson install -C build`, or use the reversible helper
`packaging/install-local.sh` (backs up your distro's libfprint and drops ours in;
`packaging/uninstall-local.sh` reverts it).

### Enroll & use

```bash
fprintd-enroll          # tap 5 times
fprintd-verify          # tap once → verify-match
sudo authselect enable-feature with-fingerprint   # enable fingerprint login/sudo
```

…or just open **System Settings → Users** and add a fingerprint there.

---

## Repository layout

```
driver/      the libfprint driver (betterlife347d.c) + SM2/SM3/SM4 helpers
sigfm/       the SIGFM matcher (from libfprint MR !418, see Credits)
patches/     betterlife-sigfm.patch — the full change set against libfprint v1.94.100
packaging/   install/uninstall scripts and the RPM spec
docs/        PROTOCOL.md (reverse-engineering notes) and proof/EVIDENCE.md
```

---

## Status & upstreaming

Fully working for daily use. The clean long-term home for this is **upstream
libfprint**; the matcher depends on the SIGFM merge request
([!418](https://gitlab.freedesktop.org/libfprint/libfprint/-/merge_requests/418)),
which is not merged yet, so for now it ships as this patch.

## Credits

- Driver, protocol reverse-engineering (SM2/SM3/SM4 secure channel, capture
  sequence, device-side finger-detect model): this project.
- **SIGFM** matcher: Matthieu Charette, Natasha England-Elbro, Timur Mangliev
  (libfprint MR !418). LGPL-2.1-or-later.
- Built on [**libfprint**](https://gitlab.freedesktop.org/libfprint/libfprint).

## License

LGPL-2.1-or-later, matching libfprint. See [`LICENSE`](LICENSE).

Not affiliated with Blestech/Betterlife or any laptop vendor. "Blestech" and
"Betterlife" are used only to identify the hardware.
