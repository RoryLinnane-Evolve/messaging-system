# SecureMsg — Cryptographic Design Document

**Module:** CS4455 Cybersecurity, University of Limerick (ISE, 2nd Year)
**Submission:** 3rd June 2026

---

## 1. Threat Model

SecureMsg is designed so that the server acts as a message relay and key directory only. It never sees plaintext.

| Attacker | Properties Held | Mechanism |
|---|---|---|
| Passive network attacker (eavesdropping TLS) | Confidentiality, integrity | TLS 1.3 on all connections; C++ client enforces `CURLOPT_SSL_VERIFYPEER = 1`, `CURLOPT_SSL_VERIFYHOST = 2` |
| Active network attacker (TLS MITM) | Confidentiality, integrity, sender authentication | TLS certificate verification prevents MITM of the transport layer; TOFU key pinning detects substitution of application-layer public keys after first contact |
| Honest-but-curious server | Confidentiality; sender authentication | Server stores only ciphertext, nonce, ephemeral public key, and Ed25519 signature. It has no decryption key. Even with full DB read access it cannot recover plaintext. |
| Fully compromised server | Confidentiality of historical messages (if client keys are uncompromised) | A compromised server **can**: serve attacker-controlled public keys to users who have not yet pinned a contact (pre-TOFU window); drop or delay messages; deny service. It **cannot**: read ciphertext encrypted under keys it does not hold; forge a valid Ed25519 signature without the sender's signing secret key. |

### Properties not held against a fully compromised server

- **Forward secrecy of long-term signing key**: if the server is compromised and also exfiltrates the client's key file with the passphrase (e.g. via malware), past messages can be forged in attribution (not in confidentiality, which is protected by the ephemeral DH key).
- **Pre-TOFU key substitution**: if an attacker compromises the server before two users have ever exchanged messages, they can substitute their own public keys. After TOFU pins are established this window closes.
- **Availability**: a compromised server can drop messages or prevent delivery.
- **No key revocation**: there is no mechanism to revoke a compromised private key and inform contacts. This is a known limitation of TOFU systems without a CA.

---

## 2. Cryptographic Primitives

| Role | Algorithm | Parameters | Library |
|---|---|---|---|
| Message encryption | ChaCha20-Poly1305 (IETF) | 256-bit key, 96-bit nonce, 128-bit tag | libsodium `crypto_aead_chacha20poly1305_ietf_*` |
| Key agreement | X25519 ECDH | Curve25519, 255-bit field | libsodium `crypto_scalarmult` |
| Key derivation | HKDF-SHA256 | RFC 5869; single-block expand (L=32) | libsodium `crypto_auth_hmacsha256` (HMAC-SHA256 primitive) |
| Sender authentication | Ed25519 | 255-bit scalar, SHA-512 internally | libsodium `crypto_sign_detached` / `crypto_sign_verify_detached` |
| Password hashing (server) | Argon2id | t=3, m=65536 KiB, p=4, output=32 bytes | `Konscious.Security.Cryptography` |
| Local key wrapping | Argon2id + XSalsa20-Poly1305 | OPSLIMIT\_INTERACTIVE, MEMLIMIT\_INTERACTIVE | libsodium `crypto_pwhash` + `crypto_secretbox_easy` |
| Randomness | OS CSPRNG | — | libsodium `randombytes_buf` → `getrandom(2)` on Linux |

---

## 3. Key Generation and Registration

Each client generates two independent keypairs locally before registration:

```
X25519 keypair:   (pk_enc,  sk_enc)  — Curve25519, used for key agreement
Ed25519 keypair:  (pk_sign, sk_sign) — used for message signing
```

Both keypairs are generated via libsodium (`crypto_box_keypair`, `crypto_sign_keypair`), which uses `randombytes_buf` internally (OS CSPRNG).

On registration, both **public** keys are sent to the server over TLS and stored in the `Users` table. The server acts as a public key directory — it does not use these keys for any computation.

Both **secret** keys are encrypted at rest on the client (see §6).

```
Registration flow:
  Client                              Server (HTTPS only)
    |                                    |
    |  POST /api/auth/sign-up            |
    |  { username, argon2_password_hash, |
    |    pk_enc_b64, pk_sign_b64 }       |
    |----------------------------------->|
    |                                    |  Store user record
    |  201 Created                       |
    |<-----------------------------------|
```

---

## 4. Message Send (Encryption + Signing)

### 4.1 Key Agreement

For each message, the sender generates a fresh **ephemeral** X25519 keypair `(epk, esk)`. This keypair is used once and discarded after encryption, providing forward secrecy: compromise of the sender's long-term keys does not expose past message content.

```
dh_out = X25519(esk, pk_enc_recipient)   // 32 bytes
```

libsodium's `crypto_scalarmult` performs the X25519 Diffie-Hellman and returns non-zero if the output is the low-order point (all-zeros), in which case encryption aborts.

### 4.2 Key Derivation (HKDF-SHA256, RFC 5869)

The raw DH output is not used directly as a key — it must be extracted and expanded through HKDF to remove bias and provide domain separation.

**Extract phase** (RFC 5869 §2.2):
```
PRK = HMAC-SHA256(salt = epk, IKM = dh_out)
```
The ephemeral public key `epk` is used as the HKDF salt. This binds the derived key to the specific ephemeral keypair, preventing cross-session key reuse.

**Expand phase** (RFC 5869 §2.3, single block):
```
enc_key = HMAC-SHA256(PRK, info = "SecureMsg-v1-message-enc" || 0x01)
```
The `info` string `"SecureMsg-v1-message-enc"` provides **domain separation**: if additional keys were derived from the same PRK for other purposes, they would use distinct `info` strings and produce independent key material. The counter byte `0x01` follows the RFC 5869 construction for the first (and here, only) output block.

`enc_key` is 32 bytes, matching ChaCha20-Poly1305's key requirement.

### 4.3 Encryption (ChaCha20-Poly1305 IETF)

```
nonce   = randombytes_buf(12)    // 96-bit, fresh CSPRNG per message
ciphertext || tag = ChaCha20-Poly1305-IETF-Encrypt(enc_key, nonce, plaintext)
```

`crypto_aead_chacha20poly1305_ietf_encrypt` produces a 16-byte Poly1305 authentication tag appended to the ciphertext. Decryption with the wrong key or any modification to the ciphertext will cause tag verification to fail and return an error — the server cannot alter ciphertext undetectably.

All key material is zeroed after use with `sodium_memzero`.

### 4.4 Sender Authentication (Ed25519)

After encryption, the sender produces an Ed25519 signature over the concatenation:

```
sig_material = ciphertext || nonce || epk
signature    = Ed25519-Sign(sk_sign_sender, sig_material)
```

This binds:
- The exact ciphertext (tampering invalidates the signature)
- The nonce (prevents nonce substitution attacks)
- The ephemeral public key (prevents epk swapping)

...to the sender's long-term **signing** identity. A recipient who has pinned `pk_sign_sender` via TOFU can verify that this exact ciphertext was produced by exactly that signing key holder.

### 4.5 Wire Format

The message sent to the server is:

```json
{
  "conversationId": "...",
  "ciphertext":        "<base64url: ChaCha20-Poly1305 output>",
  "nonce":             "<base64url: 12 bytes>",
  "ephemeralPublicKey":"<base64url: 32 bytes X25519 epk>",
  "signature":         "<base64url: 64 bytes Ed25519 sig>"
}
```

The server stores all fields opaquely. It cannot read `ciphertext` and cannot produce a valid `signature` without `sk_sign_sender`.

---

## 5. Message Receive (Verification + Decryption)

```
Receive flow on client:
  1. Fetch messages from server (includes senderSigningKey in response)
  2. For each message:
     a. Verify Ed25519 signature against senderSigningKey
        — abort if verification fails
     b. Compute: dh_out = X25519(sk_enc_self, epk)
     c. HKDF-Extract: PRK = HMAC-SHA256(salt=epk, IKM=dh_out)
     d. HKDF-Expand:  enc_key = HMAC-SHA256(PRK, "SecureMsg-v1-message-enc"||0x01)
     e. ChaCha20-Poly1305-Decrypt(enc_key, nonce, ciphertext)
        — abort if authentication tag fails
     f. Display plaintext
```

Signature verification is performed **before** decryption. This ensures that:
- No decryption work is done on forged ciphertexts
- A failed verification immediately signals a potential attack

The `senderSigningKey` in the server response is the sender's Ed25519 public key, fetched from the `Users` table. TOFU ensures that once a key is pinned for a username, any server-side substitution is detected.

---

## 6. Local Key Storage (Encrypted at Rest)

Both secret keys `(sk_enc, sk_sign)` are encrypted under a key derived from the user's local passphrase before being written to disk.

### Derivation

```
salt    = randombytes_buf(32)      // fresh per key-file creation
wrap_key = Argon2id(passphrase, salt,
                    OPSLIMIT_INTERACTIVE,   // ≈ 2 iterations
                    MEMLIMIT_INTERACTIVE,   // ≈ 64 MiB
                    output_len = 32)
```

`OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE` in libsodium correspond approximately to t=2, m=64 MiB, p=1. These parameters are deliberately different from the server-side Argon2id parameters (t=3, m=64 MiB, p=4) to ensure independence of the two KDF usages (server authentication vs local key protection). Memory-hardness resists GPU/ASIC brute-force of the passphrase.

### Encryption

```
nonce   = randombytes_buf(24)
plainSKs = sk_enc (32 bytes) || sk_sign (64 bytes)     // 96 bytes total
encSKs   = XSalsa20-Poly1305-Encrypt(wrap_key, nonce, plainSKs)  // 112 bytes
```

`crypto_secretbox_easy` (XSalsa20-Poly1305) is used. It provides authenticated encryption: if the passphrase is wrong, decryption fails with an authentication error rather than producing corrupt key material.

### On-Disk Layout (232 bytes)

```
Offset  Length  Content
     0      32  Argon2id salt
    32      24  secretbox nonce
    56     112  encrypted secret keys (sk_enc || sk_sign) + 16-byte MAC
   168      32  X25519 encryption public key (plaintext — not secret)
   200      32  Ed25519 signing public key (plaintext — not secret)
```

Public keys are stored unencrypted: they are transmitted to the server during registration anyway, so encrypting them provides no confidentiality benefit.

---

## 7. TOFU Trust Model

See `docs/trust-model.md` for the full trust model justification.

In brief: on first contact with a user, both their `pk_enc` and `pk_sign` are pinned to `~/.securemsg_tofu.json`. On all subsequent interactions, the fetched keys are compared against the pinned values. Any mismatch aborts the operation with a warning. The TOFU window (before first contact) is an acknowledged limitation.

---

## 8. Server-Side Password Verification

Passwords are never stored in plaintext. On registration:

```
salt = RandomNumberGenerator.GetBytes(16)     // 128-bit, OS CSPRNG
hash = Argon2id(UTF8(password),
                salt,
                pepper = ARGON_PEPPER env var,
                t = 3, m = 65536 KiB, p = 4,
                output_len = 32)
```

The `pepper` is a server-side secret loaded from the environment, not the database. It provides an additional layer: even if the database is dumped, an attacker without the pepper cannot verify guesses.

On login, the hash is recomputed and compared with `CryptographicOperations.FixedTimeEquals` — a constant-time comparison that prevents timing-based username enumeration. A dummy hash is computed even when the username does not exist, for the same reason.

Parameters (t=3, m=65536, p=4) meet and exceed OWASP minimum recommendations (t=2, m=64 MiB, p=1) for Argon2id. See `docs/primitive-decisions.md` for justification.

---

## 9. Known Limitations

1. **Pre-TOFU key substitution**: a compromised server can serve attacker keys to users who have never exchanged messages. TOFU does not help before first contact.
2. **No key revocation**: there is no mechanism to revoke a signing or encryption key and notify contacts. A compromised key file (passphrase + file) gives an attacker permanent impersonation capability until the contact manually re-establishes trust.
3. **No multi-device support**: the private key file exists on one device. There is no key synchronisation protocol.
4. **No forward secrecy for signing keys**: compromise of `sk_sign` allows retroactive forgery of message attribution (but not message content, which is protected by the ephemeral DH key).
5. **Server availability**: the server can drop messages or deny service. No delivery receipts are implemented.
6. **Single recipient per message**: the current scheme encrypts each message for one recipient's public key. Group messaging would require per-recipient encryption or a group key protocol.
