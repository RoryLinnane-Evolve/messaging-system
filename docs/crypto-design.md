# TeamWFH Cryptographic Design Document

**EPIC Project 2026**

Óran Fleming and Rory Linnane

---

## 1. Threat Model

SecureMsg is designed so the server acts as a message relay and key directory only. It never sees plaintext.

| Attacker | Properties Held | Mechanism |
|---|---|---|
| Passive network attacker | Confidentiality, integrity | TLS 1.3 on all connections |
| Active network attacker (TLS MITM) | Confidentiality, integrity, sender authentication | TLS certificate verification prevents transport-layer MITM; TOFU key pinning detects application-layer key substitution after first contact |
| Honest-but-curious server | Confidentiality; sender authentication | Server stores only ciphertext, nonce, ephemeral public key, and Ed25519 signature — it holds no decryption key and cannot recover plaintext |
| Fully compromised server | Confidentiality of historical messages (if client keys are uncompromised) | A compromised server **can**: serve attacker-controlled keys to users who have not yet pinned a contact; drop or delay messages; deny service. It **cannot**: read existing ciphertext; forge a valid Ed25519 signature without the sender's signing secret key. |

### Properties not held against a fully compromised server

- **Pre-TOFU key substitution**: before two users have ever exchanged messages, a compromised server can substitute attacker-controlled public keys. After TOFU pins are established this window closes.
- **Forward secrecy of signing key**: compromise of `sk_sign` allows retroactive forgery of message attribution (not content, which is protected by the ephemeral DH key).
- **Availability**: a compromised server can drop or delay messages.
- **No key revocation**: there is no mechanism to revoke a compromised private key and inform contacts.

---

## 2. Cryptographic Primitives

| Role | Algorithm | Parameters | Library |
|---|---|---|---|
| Message encryption | ChaCha20-Poly1305 IETF (RFC 8439) | 256-bit key, 96-bit nonce, 128-bit tag | libsodium `crypto_aead_chacha20poly1305_ietf_*` |
| Key agreement | X25519 ECDH (RFC 7748) | Curve25519, 255-bit field | libsodium `crypto_scalarmult` |
| Key derivation | HKDF-SHA256 (RFC 5869) | Single-block expand, L=32 | libsodium `crypto_auth_hmacsha256` |
| Sender authentication | Ed25519 (RFC 8032) | 255-bit scalar, SHA-512 internally | libsodium `crypto_sign_detached` |
| Password hashing (server) | Argon2id (RFC 9106) | t=3, m=65536 KiB, p=4, output=32 B | `Konscious.Security.Cryptography` |
| Local key wrapping | Argon2id + XSalsa20-Poly1305 | OPSLIMIT\_INTERACTIVE, MEMLIMIT\_INTERACTIVE | libsodium `crypto_pwhash` + `crypto_secretbox_easy` |
| Integrity digest | Keccak-256 | — | Nethereum `Sha3Keccack` |
| Randomness | OS CSPRNG | — | libsodium `randombytes_buf` / `RandomNumberGenerator` |

---

## 3. Construction Walkthrough

### 3.1 Key Generation & Registration

Each client generates two independent keypairs locally before registration. Both secret keys are encrypted under a passphrase-derived key before storage (see Section 3.5).

```
  Client                                       Server
    │                                             │
    │  Generate X25519 keypair (pk_enc, sk_enc)   │
    │  Generate Ed25519 keypair (pk_sign, sk_sign) │
    │  Encrypt (sk_enc ‖ sk_sign) → key_blob      │
    │                                             │
    ├──── POST /api/auth/sign-up ────────────────►│
    │     { username, password,                   │
    │       pk_enc, pk_sign, key_blob }            │
    │                                             ├── salt = CSPRNG(16)
    │                                             ├── hash = Argon2id(password, salt, pepper)
    │                                             └── Store user record
    │◄─────────────── 201 Created ────────────────┤
    │                                             │
```

The server stores both public keys as a directory accessible to other users. The encrypted key blob allows the web client to recover secret keys on any device using only the passphrase — the plaintext secret keys are never sent to or stored by the server.

---

### 3.2 Key Publication & TOFU Pinning

Before any message is sent or received, the sender fetches the recipient's public keys and applies Trust On First Use (TOFU) pinning.

```
  Client                         Server              TOFU Store
    │                               │                     │
    ├──── GET /api/user/{username} ─►│                     │
    │◄─── { pk_enc, pk_sign } ───────┤                     │
    │                               │                     │
    │  ┌─ First contact ────────────────────────────────┐  │
    │  │  Pin pk_enc and pk_sign ───────────────────────┼─►│
    │  │  Proceed                                       │  │
    │  └────────────────────────────────────────────────┘  │
    │                               │                     │
    │  ┌─ Returning contact ────────────────────────────┐  │
    │  │  Fetch pinned keys ────────────────────────────┼─►│
    │  │◄─ pinned pk_enc, pk_sign ──────────────────────┼──┤
    │  │                                                │  │
    │  │  Keys match? ──► Proceed                       │  │
    │  │  Mismatch?   ──► ABORT, display TOFU warning   │  │
    │  └────────────────────────────────────────────────┘  │
    │                               │                     │
```

Both the encryption and signing keys are pinned and checked independently. Any server-side substitution after first contact is detected immediately and the operation is aborted.

---

### 3.3 Message Send (Encryption & Signing)

For each message a fresh ephemeral X25519 keypair is generated and discarded after use, providing forward secrecy.

```
  Client                                                Server
    │                                                     │
    │  Generate ephemeral keypair (epk, esk)              │
    │  dh_out     = X25519(esk, pk_enc_recipient)         │
    │  PRK        = HMAC-SHA256(salt=epk, IKM=dh_out)    │
    │  enc_key    = HMAC-SHA256(PRK, info ‖ 0x01)         │
    │  nonce      = randombytes(12)                       │
    │  ciphertext = ChaCha20-Poly1305-Encrypt(            │
    │                 enc_key, nonce, plaintext)          │
    │  sig_material = ciphertext ‖ nonce ‖ epk            │
    │  signature  = Ed25519-Sign(sk_sign, sig_material)   │
    │                                                     │
    ├──── POST /api/message ─────────────────────────────►│
    │     { ciphertext, nonce, epk, signature }           │
    │                                                     ├── Store opaquely
    │◄─────────────── 200 OK ─────────────────────────────┤
    │                                                     │
```

The signature binds the ciphertext, nonce, and ephemeral public key to the sender's long-term signing identity. Tampering with any of these fields invalidates the signature.

---

### 3.4 Message Receive (Verification & Decryption)

Signature verification is performed **before** any DH computation or decryption. This follows the Horton Principle and prevents oracle attacks that might exploit partial decryption failures.

```
  Client                            Server             TOFU Store
    │                                 │                     │
    ├──── GET /api/message/conv/{id} ─►│                     │
    │◄─── [ messages ] ───────────────┤                     │
    │                                 │                     │
    │  ┌─ For each message ────────────────────────────────┐ │
    │  │  Fetch pinned pk_sign ─────────────────────────── ┼─►│
    │  │◄─ pk_sign ──────────────────────────────────────── ┼──┤
    │  │                                                    │ │
    │  │  sig_material = ciphertext ‖ nonce ‖ epk           │ │
    │  │  Ed25519-Verify(pk_sign, sig_material, signature)  │ │
    │  │                                                    │ │
    │  │  INVALID ──► ABORT + security warning              │ │
    │  │                                                    │ │
    │  │  VALID:                                            │ │
    │  │    dh_out   = X25519(sk_enc_self, epk)             │ │
    │  │    PRK      = HMAC-SHA256(salt=epk, IKM=dh_out)   │ │
    │  │    enc_key  = HMAC-SHA256(PRK, info ‖ 0x01)        │ │
    │  │    plaintext = ChaCha20-Poly1305-Decrypt(          │ │
    │  │                  enc_key, nonce, ciphertext)       │ │
    │  │    Display plaintext                               │ │
    │  └────────────────────────────────────────────────────┘ │
    │                                 │                     │
```

---

### 3.5 Storage at Rest (Local Key Wrapping)

Both secret keys are encrypted under a passphrase-derived wrapping key before storage. This protects key material if the stored blob is exfiltrated without the passphrase.

```
  ┌─────────────┐
  │  Passphrase │──┐
  └─────────────┘  │   ┌─────────────────────────────┐   ┌──────────────────┐
                   ├──►│ Argon2id                    ├──►│  wrap_key (32 B) │
  ┌─────────────┐  │   │ OPSLIMIT_INTERACTIVE        │   └────────┬─────────┘
  │ Salt (32 B) │──┘   │ MEMLIMIT_INTERACTIVE        │            │
  └─────────────┘      └─────────────────────────────┘            │
                                                                   ▼
  ┌──────────────────────┐                             ┌─────────────────────┐
  │ sk_enc ‖ sk_sign     │────────────────────────────►│  XSalsa20-Poly1305  │
  │ (96 bytes)           │                             │  crypto_secretbox   ├──► Encrypted blob
  └──────────────────────┘                             │  _easy              │    (112 B + 16 B MAC)
  ┌──────────────────────┐                             └─────────────────────┘
  │ Nonce (24 B, CSPRNG) │────────────────────────────►
  └──────────────────────┘
```

**Stored layout:**

| Offset | Length | Content |
|---|---|---|
| 0 | 32 B | Argon2id salt |
| 32 | 24 B | XSalsa20-Poly1305 nonce |
| 56 | 112 B | Encrypted secret keys (sk\_enc ‖ sk\_sign) + 16-byte MAC |
| 168 | 32 B | X25519 public key (plaintext) |
| 200 | 32 B | Ed25519 signing public key (plaintext) |

Public keys are stored unencrypted as they are already transmitted to and held by the server. The C++ client stores this layout on disk as a binary file; the web client serialises the same structure as JSON and stores it server-side in the `EncryptedKeyBlob` field. In both cases the plaintext secret keys never leave the client.

`crypto_secretbox_open_easy` fails with an authentication error if the passphrase is wrong, preventing silent production of corrupt key material.

---

## 4. Primitive Justifications

### 4.1 ChaCha20-Poly1305 IETF (RFC 8439)

**Why AEAD:** AEAD constructions provide a single, well-defined IND-CCA2 security guarantee — an attacker who can query a decryption oracle gains no information about plaintexts. Encrypt-and-MAC does not inherently provide IND-CCA2, as the MAC may leak plaintext information (e.g. CBC-MAC over plaintext). MAC-then-Encrypt is vulnerable to padding oracle attacks with block ciphers. AEAD avoids both by fusing encryption and authentication into one primitive.

**Why ChaCha20-Poly1305 over AES-256-GCM:** Both are standardised IND-CCA2 AEAD schemes. ChaCha20-Poly1305 was chosen because it does not require hardware AES acceleration (AES-NI) to run in constant time. Software AES implementations without AES-NI are vulnerable to cache-timing attacks. ChaCha20 is inherently bitsliced and constant-time on all platforms (Bernstein, "ChaCha, a variant of Salsa20", 2008).

**Why not XSalsa20-Poly1305:** `crypto_box_easy` uses XSalsa20-Poly1305, which abstracts away the key derivation step. Using explicit ChaCha20-Poly1305 allows explicit HKDF key derivation to be demonstrated and justified, which XSalsa20 would hide.

**Nonce handling:** Each nonce is generated fresh from `randombytes_buf` per message. The birthday-bound collision probability reaches 2⁻³² only after approximately 2³² messages (N²/2⁹⁷ for 96-bit nonces) — well beyond realistic usage. Nonce reuse would allow ciphertext XOR to cancel the keystream entirely, so CSPRNG generation is mandatory. *RFC 8439, Section 2.*

---

### 4.2 X25519 Key Agreement (RFC 7748)

A per-message ephemeral keypair is generated and discarded after the DH computation, providing **forward secrecy**: future compromise of long-term keys does not expose past session keys.

**Why Curve25519 over NIST P-256:** P-256 uses a standardised but opaque random prime — the curve generation process was not fully transparent (Bernstein & Lange, SafeCurves, 2014). Curve25519 was designed with verifiably rigid criteria: the prime, cofactor, and base point were all chosen deterministically from publicly stated security requirements with no unexplained constants. RSA key exchange is explicitly forbidden by the assignment and does not support forward secrecy.

libsodium's `crypto_scalarmult` checks for the all-zero (low-order) output point and returns an error, preventing small-subgroup attacks. *RFC 7748, Section 5.*

---

### 4.3 HKDF-SHA256 (RFC 5869)

Raw X25519 output has elliptic-curve structure and is not uniformly random; using it directly as a symmetric key would allow an attacker to exploit this bias. HKDF removes it via a two-phase construction:

**Extract:** `PRK = HMAC-SHA256(salt=epk, IKM=dh_out)` — using the ephemeral public key as the HKDF salt binds the derived key to this specific ephemeral exchange. RFC 5869, Section 3.1 states that a non-secret random salt is acceptable and recommended.

**Expand:** `enc_key = HMAC-SHA256(PRK, "SecureMsg-v1-message-enc" ‖ 0x01)` — the `info` string provides domain separation so that any additional keys derived from the same PRK would be independent (RFC 5869, Section 3.2). The counter byte `0x01` follows the standard HKDF-Expand T(1) construction. Output is 32 bytes = one HMAC-SHA256 block; no second round is needed.

SHA-256 provides 128-bit second-preimage resistance. SHA-1 and MD5 are both forbidden by the assignment and cryptographically broken. HKDF is implemented directly over `crypto_auth_hmacsha256` — HMAC-SHA256 is the vetted primitive; HKDF is a published standard construction that uses it, not a hand-rolled design. *RFC 5869, Section 2.*

---

### 4.4 Ed25519 Sender Authentication — Deviation from HPKE Mode_Auth (RFC 8032, RFC 9180)

The assignment specifies HPKE Mode_Auth (RFC 9180, Section 5.1.2), which authenticates the sender by computing a second DH term `DH(sk_sender_enc, pk_recipient)` and mixing it into the key schedule. This implementation deviates for three reasons:

1. **No libsodium Mode_Auth API exists.** Composing the full RFC 9180 key schedule (`ks_binder`, `key_schedule_context`, `VerifyPSKInputs`) from primitives would constitute a hand-rolled protocol, which is forbidden by the assignment.
2. **Ed25519 provides a strictly stronger property.** HPKE Mode_Auth provides *receiver-only* authentication — only the recipient can verify sender identity. Ed25519 signatures provide **non-repudiation**: any party holding `pk_sign` can verify the signature.
3. **Independent key pinning.** A dedicated Ed25519 signing keypair allows both keys to be pinned independently in the TOFU store. Compromise of the encryption keypair does not automatically compromise the signing keypair.

| Property | HPKE Mode_Auth | Ed25519 (this implementation) |
|---|---|---|
| Sender authentication | Receiver-only (implicit, via key schedule) | Universal (anyone with signing public key can verify) |
| Non-repudiation | No | Yes |
| libsodium support | No direct API | `crypto_sign_detached` / `crypto_sign_verify_detached` |
| Forward secrecy of encryption | Yes (ephemeral DH) | Yes (unchanged) |
| Independent key pinning | Not applicable | Yes |
| Hand-rolled protocol required | Yes | No |

The signed material `ciphertext ‖ nonce ‖ epk` prevents ciphertext tampering, nonce substitution, and ephemeral key swapping. Signing the ciphertext (Encrypt-then-Sign) is correct — signing plaintext before encryption would leak length information and allow signature-checking without breaking the encryption. Verification occurs before decryption (Horton Principle). RFC 9180, Section 10.1 explicitly states that "an application may choose to authenticate the sender using a separate mechanism, such as a digital signature." *RFC 8032, Section 5.1; RFC 9180, Sections 5.1.2 and 10.1.*

---

### 4.5 Argon2id Password Hashing (RFC 9106)

**Why not bcrypt or PBKDF2:** bcrypt has a 72-byte password limit and a fixed 4 KiB memory footprint — trivially GPU-parallelisable. PBKDF2 is purely CPU-bound with no memory hardness — an attacker can run millions of parallel instances. Argon2id's 64 MiB memory cost limits a 6 GB GPU to approximately 93 parallel instances, making large-scale cracking economically impractical.

**Parameters:** t=3, m=65536 KiB, p=4. These exceed OWASP minimums (t=2, m=64 MiB, p=1) and match RFC 9106, Section 4's recommendation of t=3 for interactive use. p=4 matches typical server CPU core count, ensuring full memory bandwidth utilisation.

**Pepper:** A server-side secret loaded from the `ARGON_PEPPER` environment variable, not stored in the database. If the database is dumped without the server environment, an attacker cannot verify password guesses without also knowing the pepper.

**Timing safety:** `CryptographicOperations.FixedTimeEquals` is used for hash comparison. A dummy hash is computed even for non-existent usernames to prevent timing-based username enumeration. *RFC 9106, Section 4.*

---

### 4.6 Local Key Wrapping: Argon2id + XSalsa20-Poly1305

`OPSLIMIT_INTERACTIVE` / `MEMLIMIT_INTERACTIVE` (≈ t=2, m=64 MiB, p=1) are used instead of the server-side parameters. These are intentionally lower to keep unlock latency acceptable for interactive use while retaining memory-hardness against offline GPU brute-force of the passphrase. The threat model here is an offline attacker with a copy of the key file.

XSalsa20-Poly1305 (`crypto_secretbox_easy`) is used rather than ChaCha20-Poly1305 because it is the native libsodium secretbox format and its 192-bit nonce provides a larger safety margin for random generation within a single key file. Authentication failure on a wrong passphrase is guaranteed by the Poly1305 MAC — decryption does not silently produce corrupt key material.

---

### 4.7 Randomness

All randomness is sourced from `randombytes_buf` (libsodium), which calls `getrandom(2)` on Linux (kernel 3.17+). `getrandom(2)` reads from the kernel's CSPRNG (ChaCha20-based since Linux 5.17), seeded from hardware entropy sources and blocking until sufficient entropy is available. The server uses `System.Security.Cryptography.RandomNumberGenerator.GetBytes`, which calls the OS CSPRNG. No userspace PRNG, `Math.random()`, or time-seeded generator is used anywhere.

---

## 5. Blockchain Integrity

Every 10th message in a conversation triggers an integrity digest. The server computes a Keccak-256 hash of the ciphertext batch and records it on Ethereum Sepolia via a smart contract, producing an immutable, externally verifiable integrity record.

```
  Server                                         Ethereum Sepolia
    │                                                   │
    │  [After every 10th message]                       │
    │  Fetch messages N-9 … N (ordered by timestamp)    │
    │  combined = Concat(ciphertext₁ … ciphertext₁₀)   │
    │  hash     = keccak256(UTF-8(combined))            │
    │                                                   │
    ├──── recordDigest(bytes32 hash) ──────────────────►│
    │◄─── DigestRecorded(id, hash, timestamp) + txHash ─┤
    │                                                   │
    │  Store ConversationDigest                         │
    │  { hash, txHash, firstMessageId, lastMessageId }  │


  Client                       Server              Ethereum Sepolia
    │                             │                       │
    ├──── GET .../digests ────────►│                       │
    │◄─── [{ txHash, … }] ────────┤                       │
    │                             │                       │
    ├──── getTransactionReceipt(txHash) ─────────────────►│
    │◄─── DigestRecorded event: onChainHash ──────────────┤
    │                             │                       │
    │  recomputed = keccak256(UTF-8(Concat(ciphertexts))) │
    │  PASS if onChainHash == recomputed                  │
    │  FAIL otherwise                                     │
    │                             │                       │
```

**What this protects:** if the server modifies any stored ciphertext after the digest is recorded, the recomputed Keccak-256 will not match the on-chain hash. The Ethereum blockchain is append-only; recorded transactions cannot be altered after confirmation.

**What this does not protect:** the server controls digest timing and batch composition. A compromised server could omit messages from a batch before recording. The integrity guarantee applies only to the ciphertexts included in the batch at digest time.

---

## 6. TOFU Trust Model

### What is TOFU?

Trust On First Use (TOFU) is a key authentication model where a public key is unconditionally trusted the first time it is seen, then pinned locally. All subsequent contacts must present the same key. This is analogous to how SSH handles host keys.

### Why TOFU?

| Model | Requirement | Drawback for this deployment |
|---|---|---|
| PKI | Certificate Authority | Requires CA, certificate issuance, and revocation infrastructure (CRL/OCSP). Out of scope for a closed university deployment. |
| Web of Trust | Users sign each other's keys | Requires out-of-band signing ceremonies. Impractical for a general-purpose messenger. |
| TOFU | First contact establishes trust | Simple, widely used (SSH, Signal's initial key exchange), provides strong post-first-contact security. |

### What is Pinned

For each contact, two public keys are pinned independently:

| Key | Used for |
|---|---|
| X25519 encryption public key (`pk_enc`) | Key agreement — recipient's public key in ECDH |
| Ed25519 signing public key (`pk_sign`) | Verifying message signatures |

Pinning both keys independently means a compromise of one does not automatically compromise the other, and a server-side substitution of either is detected separately.

### When Pinning Occurs

Pinning is applied at two points:

1. **Before entering a chat**: the recipient's keys are fetched from the server and pinned (or verified against the existing pin). If either key differs from the pinned value, the operation is refused and a warning is displayed.
2. **Before forwarding a message**: the target recipient's keys are pinned or verified before the forwarded content is re-encrypted for them.

### What Happens on a Mismatch

If either key differs from the pinned value:

1. A clear warning is displayed to the user.
2. The operation is aborted — no message is encrypted or sent.
3. The user must resolve the mismatch out-of-band (e.g. confirm the correct key fingerprint with the contact via a separate channel) before communications can resume.

### Security Properties

**TOFU protects against:** a server that substitutes a contact's public key after first contact. Once a key is pinned, any change is detected immediately.

**TOFU does not protect against:** key substitution before first contact. If the server is compromised before two users have ever exchanged messages, it can serve attacker-controlled keys and TOFU will pin the attacker's keys. This is the fundamental limitation of TOFU.

Users can mitigate the pre-TOFU window by verifying key fingerprints out-of-band — consistent with the approach used by Signal ("Safety Numbers") and WhatsApp ("Security Code").

### Key Revocation

There is no automated key revocation mechanism. If a key file is compromised, the user should register a new account with new keys; contacts must manually delete the old TOFU entry. In a production system this would be addressed by a PKI with revocation (CRL/OCSP) or a server-published revocation list with a transparency log. These are out of scope for this project.

---

## 7. Server-Side Password Verification

Passwords are never stored in plaintext. On registration:

```
salt          = RandomNumberGenerator.GetBytes(16)    // 128-bit, OS CSPRNG
password_hash = Argon2id(UTF-8(password), salt, pepper, t=3, m=65536, p=4, len=32)
```

The pepper is a server-side secret loaded from the `ARGON_PEPPER` environment variable. On login, the hash is recomputed and compared with `CryptographicOperations.FixedTimeEquals`. A dummy hash is computed even when the username does not exist, preventing timing-based username enumeration.

---

## 8. Known Limitations

1. **Pre-TOFU key substitution**: a compromised server can serve attacker keys before two users have first exchanged messages. TOFU provides no protection before first contact.
2. **No key revocation**: there is no mechanism to revoke a compromised signing or encryption key. A compromised key file gives an attacker permanent impersonation capability until contacts manually re-establish trust.
3. **No multi-device support**: the encrypted key blob is per-account and single-instance. There is no key synchronisation protocol.
4. **No forward secrecy for signing keys**: compromise of `sk_sign` allows retroactive forgery of message attribution but not message content, which is protected by the ephemeral DH key.
5. **Server availability**: the server can drop messages or deny service. No delivery receipts are implemented.
6. **Single recipient per message**: the scheme encrypts for one recipient's public key. Group messaging would require per-recipient encryption or a group key protocol.
7. **Blockchain integrity is advisory**: a fully compromised server could omit messages from a digest batch before recording. The on-chain record proves integrity only of the ciphertexts that were included.
8. **JavaScript key material in memory**: unlike the C++ client, the web client cannot call `sodium_memzero` — secret key material persists in the JavaScript heap until garbage collected.
