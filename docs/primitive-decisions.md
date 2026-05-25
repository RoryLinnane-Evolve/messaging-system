# Cryptographic Primitive Decisions

This document justifies every cryptographic algorithm and parameter choice at the level required by the CS4455 marking criteria (criterion 5c).

---

## 1. AEAD: ChaCha20-Poly1305 IETF (RFC 8439)

**Algorithm:** ChaCha20 stream cipher + Poly1305 MAC, combined as an Authenticated Encryption with Associated Data (AEAD) scheme.

**Parameters:**
- Key: 256 bits
- Nonce: 96 bits (12 bytes), from CSPRNG, fresh per message
- Tag: 128 bits (16 bytes)

**Why AEAD and not Encrypt-and-MAC or MAC-then-Encrypt:**
AEAD constructions provide a single, well-defined security guarantee: IND-CCA2 (indistinguishability under adaptive chosen-ciphertext attack), meaning an attacker who can query a decryption oracle gains no information about plaintexts. Encrypt-and-MAC (e.g. AES-CBC + HMAC) does not inherently provide IND-CCA2 — the MAC may leak information about the plaintext (e.g., CBC-MAC over plaintext). MAC-then-Encrypt is vulnerable to padding oracle attacks when combined with block ciphers. The AEAD construction avoids both by fusing encryption and authentication into a single primitive.

**Why ChaCha20-Poly1305 over AES-256-GCM:**
Both are standardised IND-CCA2 AEAD schemes and either would satisfy the assignment. ChaCha20-Poly1305 was chosen because:
- It does not require hardware AES acceleration (AES-NI) to run in constant time. On hardware without AES-NI, software AES implementations are vulnerable to cache-timing attacks. ChaCha20 is inherently bitsliced and constant-time in software on all platforms (Bernstein, "ChaCha, a variant of Salsa20", 2008).
- libsodium's `crypto_aead_chacha20poly1305_ietf_*` implements the IETF variant (RFC 8439) with a 96-bit nonce, which is the standard used in TLS 1.3 and QUIC.

**Why not XSalsa20-Poly1305 (the original `crypto_box`):**
`crypto_box_easy` uses XSalsa20-Poly1305, which has a 192-bit nonce. While the larger nonce reduces nonce-collision probability with random generation, XSalsa20-Poly1305 is not standardised by the IETF and would require more justification in the context of this assessment. The explicit ChaCha20-Poly1305 (IETF) path also allows us to use explicit HKDF key derivation (criterion 3b), which `crypto_box` abstracts away.

**Nonce handling:**
Each nonce is generated fresh from `randombytes_buf` (OS CSPRNG). Given a 96-bit nonce and IID random generation, the birthday bound collision probability across N messages is approximately N²/2⁹⁷. For a single conversation to reach a 2⁻³² collision probability would require approximately 2³² ≈ 4 billion messages — well beyond any realistic usage. Nonce reuse would be catastrophic (an attacker could XOR two ciphertexts to cancel the keystream), so fresh CSPRNG nonces per message are mandatory.

**Reference:** RFC 8439 (ChaCha20 and Poly1305 for IETF Protocols), §2.

---

## 2. Key Agreement: X25519 (RFC 7748)

**Algorithm:** Elliptic-curve Diffie-Hellman over Curve25519.

**Parameters:**
- Field: GF(2²⁵⁵ − 19)
- Security level: ~128 bits (comparable to 3072-bit RSA)

**Why ephemeral keypair:**
An ephemeral X25519 keypair is generated per message and discarded after the DH computation. This provides **forward secrecy**: if the long-term secret key is compromised in the future, past session keys (derived from ephemeral keys) cannot be recovered.

**Why X25519 over NIST P-256 or RSA:**
- P-256 uses a standardised but controversial random prime. No exploitable weakness is known, but the curve generation process was not transparent (Bernstein & Lange, "SafeCurves", 2014).
- Curve25519 was designed with verifiably rigid criteria: the prime, cofactor, and base point were all chosen deterministically from stated security requirements with no unexplained constants.
- RSA key agreement (textbook RSA or OAEP) is explicitly forbidden by the assignment and is generally unsuitable for forward-secret key exchange.
- libsodium's `crypto_scalarmult` checks for the all-zero (low-order) point and returns -1, preventing a class of small-subgroup attacks.

**Reference:** RFC 7748 §5 (Elliptic Curves for Security — Curve25519).

---

## 3. Key Derivation: HKDF-SHA256 (RFC 5869)

**Algorithm:** HMAC-based Extract-and-Expand Key Derivation Function.

**Why explicit HKDF and not raw DH output:**
Raw X25519 output has statistical structure — it is a point on an elliptic curve, not uniform random. Using it directly as a symmetric key allows an attacker to exploit this structure. HKDF removes bias by passing the DH output through a pseudorandom function (HMAC-SHA256) in the Extract phase, producing a uniformly distributed Pseudorandom Key (PRK).

**Extract phase:**
```
PRK = HMAC-SHA256(salt = epk, IKM = dh_out)
```
The ephemeral public key `epk` serves as the HKDF salt. RFC 5869 §3.1 states that a non-secret random salt is acceptable and recommended. Using `epk` as salt binds the derived key to this specific ephemeral exchange.

**Expand phase:**
```
enc_key = HMAC-SHA256(PRK, "SecureMsg-v1-message-enc" || 0x01)
```
The `info` string provides domain separation (RFC 5869 §3.2). If the same PRK were used to derive multiple keys (e.g. separate encryption and authentication keys), different `info` strings ensure the derived keys are independent. The counter byte `0x01` is the first byte of the standard HKDF-Expand construction (T(1)). Output length is 32 bytes = 1 HMAC-SHA256 block, so no second round is needed.

**Why SHA-256 and not SHA-1 or MD5:**
SHA-1 is forbidden by the assignment and is considered cryptographically broken in collision-resistance contexts (SHAttered, 2017). MD5 is forbidden and broken. SHA-256 provides 128-bit second-preimage resistance and is standardised in RFC 6234.

**Implementation:** HKDF is implemented directly over `crypto_auth_hmacsha256` (libsodium's HMAC-SHA256). This is not a "hand-rolled primitive" — HMAC-SHA256 is the vetted primitive; HKDF is a published standard construction (RFC 5869) that uses it.

**Reference:** RFC 5869 (HMAC-based Extract-and-Expand Key Derivation Function), §2.

---

## 4. Sender Authentication: Ed25519 (RFC 8032)

**Algorithm:** Edwards-curve Digital Signature Algorithm over Curve25519 (Twisted Edwards form).

**Parameters:**
- Curve: Ed25519 (birational equivalent of Curve25519)
- Hash: SHA-512 (internally)
- Signature: 64 bytes
- Public key: 32 bytes

**Why Ed25519 signatures and not including the sender's long-term key in the DH:**
An alternative would be HPKE Mode\_Auth (RFC 9180), which would compute `X25519(sk_enc_sender, pk_enc_recipient)` in addition to the ephemeral DH and include both in the key schedule, providing sender authentication without a separate signature. However:
- HPKE Mode\_Auth is not natively available in libsodium (no direct function).
- An implementation using separate Ed25519 signatures is simpler to audit and explain.
- Signatures provide non-repudiation (a third party can verify the sender), whereas HPKE Mode\_Auth provides only receiver-verifiable authentication.

**What is signed:**
```
sig_material = ciphertext || nonce || ephemeralPublicKey
```
Signing the full ciphertext prevents any modification of the encrypted payload. Signing the nonce prevents nonce substitution. Signing the ephemeral public key prevents ephemeral key swapping (an attacker substituting `epk` would cause the recipient to derive a different decryption key).

**Why not sign plaintext:**
Signing ciphertext (Encrypt-then-Sign) is the standard composition. Signing plaintext before encryption would leak length information and potentially allow an attacker to check guesses against the signature before breaking the encryption.

**Reference:** RFC 8032 (Edwards-Curve Digital Signature Algorithm), §5.1.

---

## 5. Password Hashing: Argon2id (RFC 9106)

**Algorithm:** Argon2id — hybrid of Argon2i (side-channel resistant) and Argon2d (GPU-resistant).

**Parameters (server-side):**
- Time cost (t): 3 iterations
- Memory cost (m): 65536 KiB = 64 MiB
- Parallelism (p): 4 lanes
- Output length: 32 bytes
- Salt: 16 bytes, `RandomNumberGenerator.GetBytes` (OS CSPRNG)
- Pepper: loaded from `ARGON_PEPPER` environment variable

**Why Argon2id and not bcrypt or PBKDF2:**
- bcrypt has a 72-byte password limit and uses a fixed 4 KiB memory footprint — trivially parallelisable on modern GPU farms.
- PBKDF2 is purely CPU-bound with no memory-hardness — massively parallelisable.
- Argon2id's large memory footprint (64 MiB per verification) prevents cost-effective GPU or ASIC attacks: a GPU with 6 GB VRAM can run at most ~93 parallel Argon2id instances at m=64 MiB, compared to millions of PBKDF2 instances.

**Why t=3, m=65536, p=4:**
OWASP Password Storage Cheat Sheet (2024) recommends minimum t=2, m=64 MiB, p=1 for Argon2id. Our parameters (t=3, p=4) exceed the minimum. RFC 9106 §4 recommends t=3 for interactive use cases. Parallelism p=4 matches the typical CPU core count on server hardware, ensuring the full memory bandwidth is used without over-provisioning.

**Why a pepper:**
The pepper is a server-side secret not stored in the database. If the database is dumped without the server's environment (a common attack scenario), the attacker cannot verify password guesses without also knowing the pepper. The pepper is loaded from `ARGON_PEPPER` at startup; the server crashes immediately if it is not set.

**Timing safety:**
`CryptographicOperations.FixedTimeEquals` is used for hash comparison. A dummy hash is computed even for non-existent usernames to prevent timing-based username enumeration.

**Reference:** RFC 9106 (Argon2 Memory-Hard Function for Password Hashing and Proof-of-Work), §4.

---

## 6. Local Key Wrapping: Argon2id + XSalsa20-Poly1305

**Key derivation:**
The local key file uses the same Argon2id via libsodium's `crypto_pwhash` with `OPSLIMIT_INTERACTIVE` and `MEMLIMIT_INTERACTIVE`, which correspond approximately to t=2, m=64 MiB, p=1. These parameters are intentionally lower than the server-side parameters for two reasons:

1. The user unlocks their key on every application start — excessive delay degrades usability.
2. The threat model for local key protection is an offline attacker with a copy of the key file; the memory-hardness still prevents trivial GPU brute-force of the passphrase.

**Encryption of key material:**
`crypto_secretbox_easy` (XSalsa20-Poly1305) is used to encrypt the 96-byte concatenated secret key block (32-byte X25519 sk + 64-byte Ed25519 sk). The 24-byte nonce is generated from `randombytes_buf` and stored alongside the ciphertext. XSalsa20-Poly1305 is used here (not ChaCha20-Poly1305) because it is the native format of `crypto_secretbox_easy` and the larger nonce (192 bits vs 96 bits) provides a comfortable safety margin for random nonce generation within a single key file.

After decryption, plaintext key material is immediately zeroed with `sodium_memzero` to reduce the time the secret is held in memory.

---

## 7. Randomness

All randomness is sourced from `randombytes_buf` (libsodium), which calls `getrandom(2)` on Linux (kernel 3.17+). `getrandom(2)` reads from the kernel's CSPRNG (ChaCha20-based since Linux 5.17), which is seeded from hardware entropy sources (CPU RDRAND, interrupt timing, etc.) and blocks until sufficient entropy is available. The server uses `System.Security.Cryptography.RandomNumberGenerator.GetBytes`, which calls the OS CSPRNG (`/dev/urandom` or equivalent).

No userspace PRNG, `rand()`, `srand()`, `Math.random()`, or time-seeded generators are used anywhere.
