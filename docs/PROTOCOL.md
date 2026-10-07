# Blestech / Betterlife 347d:0304 — protocol notes

Reverse-engineered from scratch (the vendor ships only a Windows UMDF2 driver).
Everything here was confirmed against the hardware.

## Hardware

- USB `347d:0304` — Blestech "Betterlife Fingerprint". iSerial = firmware `7.0.14.3-2000`.
- Interface 0, vendor class `255/0/0`. Bulk OUT `0x02`, bulk IN `0x81`, full speed.
- MCU: Nations **N32G4FR** (Cortex-M4) bridging a small Betterlife sensor over SPI.
- Image geometry: **96 × 112 px, 8 bpp** (10752 bytes).

## Frame format

Outer frame on the bulk endpoints:

```
0xAA | len32 (LE) | nonce16 | body | crc32   (CRC32 reflected 0xEDB88320)
len = 16 (nonce) + body_len + 4 (crc)
```

Inner body (after decryption, when the secure channel is up):

```
0x4C ('L') | cmd | sub | 0 0 0 | dlen32 (LE) | data | pad-to-16
```

## Secure channel (ShangMi / GM/T)

At open, host and MCU negotiate an encrypted session:

- **SM2** (GM/T 0003 curve, built explicitly from the standard parameters) for ECDH.
- **SM3** as the KDF: `session_key_material = SM3(Sx || Sy || 00000001)`.
- **SM4** for the session. Per-frame key: `framekey = SM4-ECB(session_key, nonce16)`,
  then the body is SM4-ECB'd with `framekey`.
- Handshake: `A0` (init) → `A2` (exchange ephemeral SM2 public keys) → derive
  `session = encSessKey XOR SM3(...)`, verified by a `SM3(Sx || session || Sy)` MAC.

Commands: `A0` init, `A1` version, `A2` key exchange, `B4` image transfer,
`B6` finger-detect, `C0` sleep, `C1` UID, `C2` sensor info. **Never send `D0`
(restore-MCU) or `D1` (update-firmware)** — they can brick the device.

## Image capture — the important part

The device does its own **finger detection**, exactly like the Windows driver:

1. Host sends `B4/0` (data = 4 zero bytes).
2. The device answers an **ack** immediately, then — only once a finger is actually
   present — pushes a **metadata** frame (`B4/0`, dlen ≥ 9). So the host sends `B4/0`
   and does a **long blocking read** for that metadata (re-sending `B4/0` every few
   seconds as a keepalive). No finger → no metadata → the host just waits.
3. When the metadata arrives (finger down), fetch the whole image in **one** request:
   `B4/1` with data `00 2a 00 00` (= 0x2a00 = 10752). The image comes back clean.

Two mistakes cost a lot of debugging and are worth flagging for anyone extending this:

- **Do not poll by capturing images in a loop.** That hammers the weak bus-powered
  firmware and it wedges (only a power cycle recovers it — suspend does not cut VBUS
  on an internal port). Wait for the device's own metadata signal instead; it is
  ~10× fewer operations and detection is instant.
- **Read the image in one `B4/1`, not two chunks.** Splitting it (8140 + 2612, as the
  Windows capture happens to do) makes the device return garbage for the second
  chunk; a single 10752-byte request returns the full, clean frame.

## Matching: why SIGFM, not NBIS

At 96×112 the sensor sees a tiny window of the finger — `mindtct` extracts only
**3–5 minutiae**, so bozorth3 scores 0 on every comparison. The driver therefore
uses **SIGFM** (SIFT-feature matching, OpenCV) which is built for small sensors;
genuine matches score in the hundreds.
