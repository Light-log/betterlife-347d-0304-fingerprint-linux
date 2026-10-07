/*
 * Betterlife 347d:0304 - secure-channel protocol helpers (framing + SM2/SM3/SM4)
 *
 * Copyright (C) 2026 huellero project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Frame layout (little-endian lengths):
 *   0xAA | len32 | nonce16 | body | crc32(reflected 0xEDB88320)
 * where len = 16 + body_len + 4, and
 *   body = 0x4C | cmd | sub | 0 0 0 | dlen32 | data | pad-to-16.
 * When a session key is set, body is SM4-ECB encrypted under a per-frame key
 *   framekey = SM4-ECB-encrypt(sesskey, nonce16).
 * Handshake: SM2 ECDH, KDF = SM3(Sx||Sy||00000001), sess = encSess ^ KDF,
 *   MAC = SM3(Sx||sess||Sy).
 */

#include "betterlife347d-proto.h"
#include "sm3.h"
#include "sm4.h"

#include <string.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

/* SM2 needs the OpenSSL 3 low-level EC API to recover the full shared point
 * (both X and Y); EVP_PKEY_derive only exposes X. These calls are flagged
 * deprecated-in-favour-of-EVP but remain supported. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/ec.h>
#include <openssl/bn.h>

/* SM2 recommended curve parameters, GM/T 0003.5-2012 (big-endian hex).
 * Fedora's OpenSSL does not register SM2 as a named curve, so the group is
 * built explicitly from these. */
#define SM2_P  "FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFF"
#define SM2_A  "FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000FFFFFFFFFFFFFFFC"
#define SM2_B  "28E9FA9E9D9F5E344D5A9E4BCF6509A7F39789F515AB8F92DDBCBD414D940E93"
#define SM2_GX "32C4AE2C1F1981195F9904466A39C9948FE30BBFF2660BE1715A4589334C74C7"
#define SM2_GY "BC3736A2F4F6779C59BDCEE36B692153D0A9877CC62A474002DF32E52139F0A0"
#define SM2_N  "FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFF7203DF6B21C6052B53BBF40939D54123"

static EC_GROUP *
sm2_group_new (BN_CTX *ctx)
{
  BIGNUM *p = NULL, *a = NULL, *b = NULL, *gx = NULL, *gy = NULL, *n = NULL;
  EC_GROUP *g = NULL;
  EC_POINT *gen = NULL;

  if (!BN_hex2bn (&p, SM2_P) || !BN_hex2bn (&a, SM2_A) || !BN_hex2bn (&b, SM2_B) ||
      !BN_hex2bn (&gx, SM2_GX) || !BN_hex2bn (&gy, SM2_GY) || !BN_hex2bn (&n, SM2_N))
    goto out;

  g = EC_GROUP_new_curve_GFp (p, a, b, ctx);
  if (!g)
    goto out;

  gen = EC_POINT_new (g);
  if (!gen ||
      EC_POINT_set_affine_coordinates (g, gen, gx, gy, ctx) != 1 ||
      EC_GROUP_set_generator (g, gen, n, BN_value_one ()) != 1)
    {
      EC_GROUP_free (g);
      g = NULL;
    }

out:
  BN_free (p); BN_free (a); BN_free (b);
  BN_free (gx); BN_free (gy); BN_free (n);
  EC_POINT_free (gen);
  return g;
}

static uint32_t
crc32f (const uint8_t *p, gsize n)
{
  uint32_t c = 0xffffffffu;

  for (gsize i = 0; i < n; i++)
    {
      c ^= p[i];
      for (int k = 0; k < 8; k++)
        c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
  return ~c;
}

/* SM4-ECB over nblocks 16-byte blocks (input is block-aligned, no padding).
 * Fedora's OpenSSL is built with OPENSSL_NO_SM4, so SM4 comes from the bundled
 * GM/T-verified implementation; OpenSSL still provides SM2 below. */
static void
sm4_ecb (const uint8_t key[16], const uint8_t *in, uint8_t *out, int nblocks, int enc)
{
  uint32_t rk[32];

  sm4_key_schedule (key, rk, enc);
  for (int i = 0; i < nblocks; i++)
    sm4_crypt_block (rk, in + i * 16, out + i * 16);
}

gssize
btl_frame_build (uint8_t *out, gsize cap, uint8_t cmd, uint8_t sub,
                 const uint8_t *data, gsize dlen,
                 const uint8_t *sesskey, const uint8_t *nonce16)
{
  int pad = (16 - ((dlen + 10) & 15)) & 15;
  gsize blen = 10 + dlen + pad;             /* always a multiple of 16 */
  gsize crcoff = 21 + blen;
  gsize total = crcoff + 4;
  uint8_t *body = out + 21;
  uint32_t len, crc;

  if (total > cap)
    return -1;

  out[0] = 0xAA;
  if (sesskey)
    {
      if (nonce16)
        memcpy (out + 5, nonce16, 16);
      else if (RAND_bytes (out + 5, 16) != 1)
        return -1;
    }
  else
    {
      memset (out + 5, 0, 16);
    }

  memset (body, 0, blen);
  body[0] = 0x4C;
  body[1] = cmd;
  body[2] = sub;
  body[6] = dlen;
  body[7] = dlen >> 8;
  body[8] = dlen >> 16;
  body[9] = dlen >> 24;
  if (data && dlen)
    memcpy (body + 10, data, dlen);

  if (sesskey)
    {
      uint8_t fk[16];
      sm4_ecb (sesskey, out + 5, fk, 1, 1);
      sm4_ecb (fk, body, body, blen / 16, 1);
    }

  len = 16 + blen + 4;
  out[1] = len;
  out[2] = len >> 8;
  out[3] = len >> 16;
  out[4] = len >> 24;

  crc = crc32f (out, crcoff);
  out[crcoff] = crc;
  out[crcoff + 1] = crc >> 8;
  out[crcoff + 2] = crc >> 16;
  out[crcoff + 3] = crc >> 24;
  return total;
}

const uint8_t *
btl_frame_parse (uint8_t *resp, gsize rl, const uint8_t *sesskey,
                 uint8_t *cmd, uint8_t *sub, uint32_t *dlen)
{
  uint32_t len;
  gsize total, blen;
  uint8_t *body = resp + 21;

  if (rl < BTL_PAYLOAD_OFFSET || resp[0] != 0xAA)
    return NULL;

  len = resp[1] | resp[2] << 8 | resp[3] << 16 | (uint32_t) resp[4] << 24;
  total = (gsize) len + 5;
  if (len < 20 || total > rl)              /* len must cover nonce+crc; frame complete */
    return NULL;

  blen = len - 20;                         /* body length (nonce16 + crc4 removed) */
  if (blen < 16 || (blen & 15))
    return NULL;

  if (sesskey)
    {
      uint8_t fk[16];
      sm4_ecb (sesskey, resp + 5, fk, 1, 1);
      sm4_ecb (fk, body, body, blen / 16, 0);
    }

  if (cmd)
    *cmd = resp[22];
  if (sub)
    *sub = resp[23];
  if (dlen)
    *dlen = resp[27] | resp[28] << 8 | resp[29] << 16 | (uint32_t) resp[30] << 24;

  return resp + BTL_PAYLOAD_OFFSET;
}

struct _BtlHandshake
{
  EC_GROUP *group;
  BIGNUM   *d;
  BN_CTX   *ctx;
};

void
btl_handshake_free (BtlHandshake *hs)
{
  if (!hs)
    return;
  if (hs->d)
    BN_clear_free (hs->d);
  if (hs->group)
    EC_GROUP_free (hs->group);
  if (hs->ctx)
    BN_CTX_free (hs->ctx);
  g_free (hs);
}

BtlHandshake *
btl_handshake_new (void)
{
  BtlHandshake *hs = g_new0 (BtlHandshake, 1);
  const BIGNUM *n;

  hs->ctx = BN_CTX_new ();
  hs->group = hs->ctx ? sm2_group_new (hs->ctx) : NULL;
  hs->d = BN_new ();
  if (!hs->ctx || !hs->group || !hs->d)
    goto fail;

  n = EC_GROUP_get0_order (hs->group);
  do
    {
      if (!BN_rand_range (hs->d, n))
        goto fail;
    }
  while (BN_is_zero (hs->d));
  return hs;

fail:
  btl_handshake_free (hs);
  return NULL;
}

gboolean
btl_handshake_pubkey (BtlHandshake *hs, uint8_t out65[65])
{
  EC_POINT *q = EC_POINT_new (hs->group);
  BIGNUM *x = BN_new (), *y = BN_new ();
  gboolean ok = q && x && y &&
                EC_POINT_mul (hs->group, q, hs->d, NULL, NULL, hs->ctx) == 1 &&
                EC_POINT_get_affine_coordinates (hs->group, q, x, y, hs->ctx) == 1;

  if (ok)
    {
      out65[0] = 0x04;
      BN_bn2binpad (x, out65 + 1, 32);
      BN_bn2binpad (y, out65 + 33, 32);
    }
  EC_POINT_free (q);
  BN_free (x);
  BN_free (y);
  return ok;
}

gboolean
btl_handshake_derive (BtlHandshake *hs, const uint8_t *payload, gsize len,
                      uint8_t sesskey[BTL_SESSKEY_LEN])
{
  gsize off = (len > 0 && payload[0] == 0x04) ? 1 : 0;
  const uint8_t *mx_b, *my_b, *mac, *enc;
  BIGNUM *mx = NULL, *my = NULL, *sx = BN_new (), *sy = BN_new ();
  EC_POINT *qm = EC_POINT_new (hs->group), *s = EC_POINT_new (hs->group);
  uint8_t sxb[32], syb[32], kd[32], sk[16], z[68], m[80], mo[32];
  gboolean ok;

  if (len < off + 64 + 32 + 16)
    { ok = FALSE; goto out; }

  mx_b = payload + off;
  my_b = payload + off + 32;
  mac = payload + off + 64;
  enc = payload + off + 96;

  mx = BN_bin2bn (mx_b, 32, NULL);
  my = BN_bin2bn (my_b, 32, NULL);
  ok = mx && my && sx && sy && qm && s &&
       EC_POINT_set_affine_coordinates (hs->group, qm, mx, my, hs->ctx) == 1 &&
       EC_POINT_is_on_curve (hs->group, qm, hs->ctx) == 1 &&
       EC_POINT_mul (hs->group, s, NULL, qm, hs->d, hs->ctx) == 1 &&
       EC_POINT_get_affine_coordinates (hs->group, s, sx, sy, hs->ctx) == 1;
  if (!ok)
    goto out;

  BN_bn2binpad (sx, sxb, 32);
  BN_bn2binpad (sy, syb, 32);

  memcpy (z, sxb, 32);
  memcpy (z + 32, syb, 32);
  z[64] = 0; z[65] = 0; z[66] = 0; z[67] = 1;
  sm3 (z, 68, kd);

  for (int i = 0; i < 16; i++)
    sk[i] = enc[i] ^ kd[i];

  memcpy (m, sxb, 32);
  memcpy (m + 32, sk, 16);
  memcpy (m + 48, syb, 32);
  sm3 (m, 80, mo);
  ok = CRYPTO_memcmp (mo, mac, 32) == 0;
  if (ok)
    memcpy (sesskey, sk, BTL_SESSKEY_LEN);

out:
  BN_free (mx);
  BN_free (my);
  BN_free (sx);
  BN_free (sy);
  EC_POINT_free (qm);
  EC_POINT_free (s);
  return ok;
}

#pragma GCC diagnostic pop

#ifdef BTL_PROTO_SELFTEST
#include <stdio.h>
#include <assert.h>
int
main (void)
{
  /* SM3("abc") GM/T 0004-2012 known-answer */
  static const uint8_t sm3_abc[32] = {
    0x66,0xc7,0xf0,0xf4,0x62,0xee,0xed,0xd9,0xd1,0xf2,0xd4,0x6b,0xdc,0x10,0xe4,0xe2,
    0x41,0x67,0xc4,0x87,0x5c,0xf2,0xf7,0xa2,0x29,0x7d,0xa0,0x2b,0x8f,0x4b,0xa8,0xe0,
  };
  uint8_t h[32];
  sm3 ((const uint8_t *) "abc", 3, h);
  assert (memcmp (h, sm3_abc, 32) == 0);

  /* SM4 GM/T 0002-2012 known-answer (ECB, one block) */
  static const uint8_t k[16] = {
    0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
  };
  static const uint8_t ct[16] = {
    0x68,0x1e,0xdf,0x34,0xd2,0x06,0x96,0x5e,0x86,0xb3,0xe9,0x4f,0x53,0x6e,0x42,0x46,
  };
  uint8_t o[16], d[16];
  sm4_ecb (k, k, o, 1, 1);
  assert (memcmp (o, ct, 16) == 0);
  sm4_ecb (k, o, d, 1, 0);
  assert (memcmp (d, k, 16) == 0);

  /* Frame round-trip: build encrypted, parse back to the same payload. */
  uint8_t nonce[16]; for (int i = 0; i < 16; i++) nonce[i] = i * 7 + 1;
  uint8_t data[20]; for (int i = 0; i < 20; i++) data[i] = 0xA0 + i;
  uint8_t frame[256];
  gssize n = btl_frame_build (frame, sizeof frame, 0xB4, 1, data, sizeof data, k, nonce);
  assert (n > 0);

  /* CRC over the pristine (still-encrypted) frame, before parse mutates it. */
  assert (crc32f (frame, n - 4) == (uint32_t) (frame[n - 4] | frame[n - 3] << 8 |
                                               frame[n - 2] << 16 | (uint32_t) frame[n - 1] << 24));

  uint8_t cmd, sub; uint32_t dl;
  const uint8_t *pl = btl_frame_parse (frame, n, k, &cmd, &sub, &dl);
  assert (pl && cmd == 0xB4 && sub == 1 && dl == sizeof data);
  assert (memcmp (pl, data, sizeof data) == 0);

  /* SM2: ephemeral pubkey is a valid 0x04-prefixed on-curve point. */
  BtlHandshake *hs = btl_handshake_new ();
  assert (hs);
  uint8_t pub[65];
  assert (btl_handshake_pubkey (hs, pub) && pub[0] == 0x04);
  btl_handshake_free (hs);

  printf ("betterlife347d-proto selftest: OK\n");
  return 0;
}
#endif
