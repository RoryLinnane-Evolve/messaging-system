import { saveTofu, saveSignTofu } from './keystore';

// Verify or pin an encryption public key for a user.
// Returns { ok: true } on success, { ok: false, message } on key mismatch.
export function verifyOrPin(username, publicKeyB64, tofu, setTofu) {
  if (!tofu[username]) {
    const updated = { ...tofu, [username]: publicKeyB64 };
    setTofu(updated);
    saveTofu(updated);
    return { ok: true };
  }
  if (tofu[username] !== publicKeyB64) {
    return {
      ok: false,
      message: `[TOFU] Encryption key for "${username}" has changed. Possible MITM attack. Contact aborted.`,
    };
  }
  return { ok: true };
}

// Verify or pin a signing public key for a user.
export function verifyOrPinSignKey(username, signingPublicKeyB64, signTofu, setSignTofu) {
  if (!signTofu[username]) {
    const updated = { ...signTofu, [username]: signingPublicKeyB64 };
    setSignTofu(updated);
    saveSignTofu(updated);
    return { ok: true };
  }
  if (signTofu[username] !== signingPublicKeyB64) {
    return {
      ok: false,
      message: `[TOFU] Signing key for "${username}" has changed. Possible MITM attack. Contact aborted.`,
    };
  }
  return { ok: true };
}
