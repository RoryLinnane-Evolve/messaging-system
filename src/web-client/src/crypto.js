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
// Uses an ephemeral X25519 keypair per message (forward secrecy).
// Signs (ciphertext || nonce || ephemeralPK) with the sender's Ed25519 key.
export function encryptMessage(plaintext, recipientPublicKeyB64, signingSecretKey) {
  const recipientPk = b64Decode(recipientPublicKeyB64);
  const ephemeral   = sodium.crypto_box_keypair();
  const nonce       = sodium.randombytes_buf(sodium.crypto_box_NONCEBYTES);

  const ct = sodium.crypto_box_easy(
    sodium.from_string(plaintext),
    nonce,
    recipientPk,
    ephemeral.privateKey
  );

  // Sign the raw bytes (not base64) — matches C++ client
  const signInput = new Uint8Array(ct.length + nonce.length + ephemeral.publicKey.length);
  signInput.set(ct, 0);
  signInput.set(nonce, ct.length);
  signInput.set(ephemeral.publicKey, ct.length + nonce.length);

  const signature = sodium.crypto_sign_detached(signInput, signingSecretKey);

  return {
    ciphertext:        b64Encode(ct),
    nonce:             b64Encode(nonce),
    ephemeralPublicKey: b64Encode(ephemeral.publicKey),
    signature:         b64Encode(signature),
  };
}

// Decrypt a message. Verifies Ed25519 signature if present and signing key is pinned.
// Throws a descriptive error on any failure.
export function decryptMessage(msg, secretKey, signTofu) {
  const ct         = b64Decode(msg.ciphertext);
  const nonce      = b64Decode(msg.nonce);
  const ephemeralPk = b64Decode(msg.ephemeralPublicKey);

  // Verify signature if present and we have the sender's signing key pinned
  if (msg.signature) {
    const pinnedKey = signTofu[msg.senderUsername];
    if (pinnedKey) {
      const sigBytes = b64Decode(msg.signature);
      const sigPk    = b64Decode(pinnedKey);

      const signInput = new Uint8Array(ct.length + nonce.length + ephemeralPk.length);
      signInput.set(ct, 0);
      signInput.set(nonce, ct.length);
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

  const plain = sodium.crypto_box_open_easy(ct, nonce, ephemeralPk, secretKey);
  return sodium.to_string(plain);
}
