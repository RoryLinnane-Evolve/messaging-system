import sodium from 'libsodium-wrappers';

export async function initSodium() {
  await sodium.ready;
}

// URL-safe no-padding base64 — matches C++ client exactly
export function b64Encode(bytes) {
  return sodium.to_base64(bytes, sodium.base64_variants.URLSAFE_NO_PADDING);
}

export function b64Decode(str) {
  return sodium.from_base64(str, sodium.base64_variants.URLSAFE_NO_PADDING);
}

// Generate a fresh X25519 encryption keypair + Ed25519 signing keypair
export function generateKeypairs() {
  const box  = sodium.crypto_box_keypair();
  const sign = sodium.crypto_sign_keypair();
  return {
    publicKey:        box.publicKey,
    secretKey:        box.privateKey,
    signingPublicKey: sign.publicKey,
    signingSecretKey: sign.privateKey,
  };
}

// Derive a 32-byte wrapping key from a passphrase using Argon2id (INTERACTIVE params)
// Must match the C++ client's deriveKey function
function deriveKey(passphrase, salt) {
  return sodium.crypto_pwhash(
    32,
    passphrase,
    salt,
    sodium.crypto_pwhash_OPSLIMIT_INTERACTIVE,
    sodium.crypto_pwhash_MEMLIMIT_INTERACTIVE,
    sodium.crypto_pwhash_ALG_ARGON2ID13
  );
}

// Encrypt keypairs for storage in localStorage
// Layout: salt(32) | nonce(24) | secretbox(X25519_sk || Ed25519_sk) | X25519_pk | Ed25519_pk
export function encryptKeypairs(keys, passphrase) {
  const salt  = sodium.randombytes_buf(sodium.crypto_pwhash_SALTBYTES);
  const nonce = sodium.randombytes_buf(sodium.crypto_secretbox_NONCEBYTES);
  const wrapKey = deriveKey(passphrase, salt);

  const payload = new Uint8Array(keys.secretKey.length + keys.signingSecretKey.length);
  payload.set(keys.secretKey, 0);
  payload.set(keys.signingSecretKey, keys.secretKey.length);

  const encPayload = sodium.crypto_secretbox_easy(payload, nonce, wrapKey);

  return {
    salt:             b64Encode(salt),
    nonce:            b64Encode(nonce),
    encPayload:       b64Encode(encPayload),
    publicKey:        b64Encode(keys.publicKey),
    signingPublicKey: b64Encode(keys.signingPublicKey),
  };
}

// Decrypt keypairs from localStorage. Throws on wrong passphrase.
export function decryptKeypairs(stored, passphrase) {
  const salt       = b64Decode(stored.salt);
  const nonce      = b64Decode(stored.nonce);
  const encPayload = b64Decode(stored.encPayload);

  const wrapKey = deriveKey(passphrase, salt);
  // crypto_secretbox_open_easy throws if MAC fails (wrong passphrase)
  const payload = sodium.crypto_secretbox_open_easy(encPayload, nonce, wrapKey);

  const secretKey        = payload.slice(0, sodium.crypto_box_SECRETKEYBYTES);
  const signingSecretKey = payload.slice(sodium.crypto_box_SECRETKEYBYTES);

  return {
    publicKey:        b64Decode(stored.publicKey),
    secretKey,
    signingPublicKey: b64Decode(stored.signingPublicKey),
    signingSecretKey,
  };
}

// Encrypt a plaintext message for a recipient.
// Construction: ephemeral X25519 DH → HKDF-SHA256 → ChaCha20-Poly1305-IETF.
// Matches main's C++ Client::encryptFor exactly (RFC 5869 + RFC 8439).
// Signs (ciphertext || nonce || ephemeralPK) with the sender's Ed25519 key.
export function encryptMessage(plaintext, recipientPublicKeyB64, signingSecretKey) {
  const recipientPk = b64Decode(recipientPublicKeyB64);

  // 1. Ephemeral X25519 keypair — discarded after this call (forward secrecy)
  const epkPair = sodium.crypto_box_keypair();
  const epk     = epkPair.publicKey;
  const esk     = epkPair.privateKey;

  // 2. X25519 DH
  const dhOut = sodium.crypto_scalarmult(esk, recipientPk);

  // 3. HKDF-Extract: PRK = HMAC-SHA256(salt=epk, IKM=dhOut)
  const prk = sodium.crypto_auth_hmacsha256(dhOut, epk);

  // 4. HKDF-Expand: enc_key = HMAC-SHA256(key=PRK, msg="SecureMsg-v1-message-enc" || 0x01)
  const infoBytes = new TextEncoder().encode('SecureMsg-v1-message-enc');
  const infoBlock = new Uint8Array(infoBytes.length + 1);
  infoBlock.set(infoBytes, 0);
  infoBlock[infoBytes.length] = 0x01;
  const encKey = sodium.crypto_auth_hmacsha256(infoBlock, prk);

  // 5. ChaCha20-Poly1305-IETF encrypt (12-byte nonce per RFC 8439)
  const nonce = sodium.randombytes_buf(sodium.crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
  const ct    = sodium.crypto_aead_chacha20poly1305_ietf_encrypt(
    sodium.from_string(plaintext),
    null,  // no additional data
    null,  // nsec unused
    nonce,
    encKey
  );

  // 6. Ed25519 sign (ciphertext || nonce || epk) — matches C++ sig_material
  const signInput = new Uint8Array(ct.length + nonce.length + epk.length);
  signInput.set(ct,    0);
  signInput.set(nonce, ct.length);
  signInput.set(epk,   ct.length + nonce.length);
  const signature = sodium.crypto_sign_detached(signInput, signingSecretKey);

  return {
    ciphertext:         b64Encode(ct),
    nonce:              b64Encode(nonce),
    ephemeralPublicKey: b64Encode(epk),
    signature:          b64Encode(signature),
  };
}

// Decrypt a message. Verifies Ed25519 signature before any DH or decryption.
// Construction: HKDF-SHA256 key derivation → ChaCha20-Poly1305-IETF decrypt.
// Throws a descriptive error on any failure.
export function decryptMessage(msg, secretKey, signTofu) {
  const ct          = b64Decode(msg.ciphertext);
  const nonce       = b64Decode(msg.nonce);
  const ephemeralPk = b64Decode(msg.ephemeralPublicKey);

  // Verify signature BEFORE doing any DH — reject forged messages immediately
  if (msg.signature) {
    const pinnedKey = signTofu[msg.senderUsername];
    if (pinnedKey) {
      const sigBytes = b64Decode(msg.signature);
      const sigPk    = b64Decode(pinnedKey);

      const signInput = new Uint8Array(ct.length + nonce.length + ephemeralPk.length);
      signInput.set(ct,          0);
      signInput.set(nonce,       ct.length);
      signInput.set(ephemeralPk, ct.length + nonce.length);

      const valid = sodium.crypto_sign_verify_detached(sigBytes, signInput, sigPk);
      if (!valid) {
        throw new Error(
          `[SECURITY] Sender authentication FAILED for ${msg.senderUsername} — message may have been forged or tampered`
        );
      }
    }
    // If signing key not yet pinned, still decrypt but skip verification
  }

  // X25519 DH: recipient's long-term secret key + sender's ephemeral public key
  const dhOut = sodium.crypto_scalarmult(secretKey, ephemeralPk);

  // HKDF-Extract: PRK = HMAC-SHA256(salt=epk, IKM=dhOut)
  const prk = sodium.crypto_auth_hmacsha256(dhOut, ephemeralPk);

  // HKDF-Expand: enc_key = HMAC-SHA256(key=PRK, msg="SecureMsg-v1-message-enc" || 0x01)
  const infoBytes = new TextEncoder().encode('SecureMsg-v1-message-enc');
  const infoBlock = new Uint8Array(infoBytes.length + 1);
  infoBlock.set(infoBytes, 0);
  infoBlock[infoBytes.length] = 0x01;
  const encKey = sodium.crypto_auth_hmacsha256(infoBlock, prk);

  // ChaCha20-Poly1305-IETF decrypt — throws on authentication tag failure
  const plain = sodium.crypto_aead_chacha20poly1305_ietf_decrypt(
    null,   // nsec unused
    ct,
    null,   // no additional data
    nonce,
    encKey
  );

  return sodium.to_string(plain);
}
