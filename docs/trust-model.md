# Trust Model: TOFU Key Pinning

**Module:** CS4455 Cybersecurity, University of Limerick

---

## What is TOFU?

Trust On First Use (TOFU) is a key authentication model where a public key is unconditionally trusted the first time it is seen, then pinned. All subsequent contacts must present the same key. This is analogous to how SSH handles host keys.

---

## Why TOFU for SecureMsg?

The alternatives are:

| Model | Requirement | Drawback for this deployment |
|---|---|---|
| Public Key Infrastructure (PKI) | Certificate Authority (CA) | Requires a CA, certificate issuance, revocation infrastructure (CRL/OCSP). Out of scope for a 2nd-year project and adds complexity without marginal benefit for a closed user base. |
| Web of Trust (WoT) | Users sign each other's keys | Requires out-of-band signing ceremonies. Impractical for a general-purpose messenger. |
| Trust On First Use (TOFU) | First contact establishes trust | Simple, widely used (SSH, Signal's initial key exchange), provides post-first-contact security. |

TOFU is explicitly listed as acceptable and expected in the assignment specification.

---

## What is Pinned

For each contact, the client stores **two** public keys:

| Key | Field | Used for |
|---|---|---|
| X25519 encryption public key | `enc` | Key agreement (recipient's public key in ECDH) |
| Ed25519 signing public key | `sign` | Verifying message signatures |

Both keys are stored in `~/.securemsg_tofu.json` with the following structure:

```json
{
  "alice": {
    "enc":  "base64url(pk_enc_alice)",
    "sign": "base64url(pk_sign_alice)"
  },
  "bob": {
    "enc":  "base64url(pk_enc_bob)",
    "sign": "base64url(pk_sign_bob)"
  }
}
```

---

## When Pinning Occurs

Pinning happens in `Client::verifyOrPin`, called at these points:

1. **Before entering a chat** (`menuLiveChat` in `main.cpp`): the recipient's keys are fetched from the server and pinned (or verified). If either key differs from the pinned value, the chat is refused.

2. **Before forwarding a message** (`Client::forwardMessage`): the target recipient's keys are pinned/verified before encrypting the forwarded content for them.

---

## What Happens on a Mismatch

If either the encryption or signing key differs from the pinned value:

1. A clear warning is printed to stderr:
   ```
   [TOFU] WARNING: Signing key for alice has changed! Possible MITM.
   ```
2. The operation is aborted — no message is encrypted or sent.
3. The user must resolve the mismatch out-of-band (e.g. confirm the correct fingerprint with the contact via a separate channel) before communications can resume.

---

## Security Properties of TOFU

**What TOFU protects against:**
- A server that substitutes a contact's public key **after** first contact. Once the key is pinned, any change is detected.
- A compromised server trying to perform a man-in-the-middle attack on an established conversation.

**What TOFU does not protect against:**
- Key substitution **before** first contact: if the server is compromised before Alice and Bob have ever exchanged messages, it can serve attacker-controlled keys to both, and TOFU will pin the attacker's keys.
- This is the fundamental limitation of TOFU: it provides no security against an active attacker present at the moment of first contact.

**Mitigation of the pre-TOFU window:**
Users can verify key fingerprints out-of-band (e.g. compare `pk_sign` fingerprints over a phone call or in person). The client displays the pinned key for each contact, allowing manual verification. This is consistent with the approach used by Signal and WhatsApp ("Safety Numbers" / "Security Code").

---

## Key Revocation

There is no automated key revocation mechanism. This is a known limitation. If a user's key file is compromised:

1. The user should register a new account with new keys.
2. Contacts must manually re-establish TOFU by deleting the old entry from `~/.securemsg_tofu.json`.

In a production system, this would be addressed by a PKI with revocation (CRL/OCSP) or a server-published revocation list with a transparency log. These are out of scope for this project.

---

## Comparison: TOFU vs PKI for this Deployment

| Property | TOFU | PKI |
|---|---|---|
| Initial setup | None | Requires CA and certificate issuance |
| Post-first-contact security | Strong (mismatch detected) | Strong (certificate validates identity) |
| Pre-first-contact security | None | Strong (CA vouches for key) |
| Key revocation | Manual | Automated (CRL/OCSP) |
| Suitable for closed deployment | Yes | Yes (with private CA) |
| Implementation complexity | Low | High |

For a closed university project with a small, known user base, TOFU provides an appropriate balance of security and complexity. The pre-TOFU window is an acceptable and explicitly acknowledged limitation.
