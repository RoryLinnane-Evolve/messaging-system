import { useState, useEffect, useRef } from 'react';
import { api } from '../api';
import { encryptMessage, decryptMessage, b64Encode } from '../crypto';
import { verifyOrPin, verifyOrPinSignKey } from '../tofu';

export default function Chat({
  conv,
  messages,
  setMessages,
  username,
  keys,
  token,
  tofu,
  setTofu,
  signTofu,
  setSignTofu,
  onVerify,
  onRevoked,
}) {
  const [input, setInput]         = useState('');
  const [sending, setSending]     = useState(false);
  const [tofuError, setTofuError] = useState('');
  const [recipient, setRecipient] = useState(null); // The other user's profile
  const bottomRef = useRef(null);

  // Find the other participant and load their profile (for TOFU + their public key)
  useEffect(() => {
    setTofuError('');
    const otherUsername = conv.participants.find(p => p !== username);
    if (!otherUsername) return;

    api.getUser(otherUsername, token).then(user => {
      if (!user) return;

      // TOFU: verify encryption key
      const encResult = verifyOrPin(otherUsername, user.publicKey, tofu, setTofu);
      if (!encResult.ok) {
        setTofuError(encResult.message);
        return;
      }

      // TOFU: verify signing key
      if (user.signingPublicKey) {
        const signResult = verifyOrPinSignKey(otherUsername, user.signingPublicKey, signTofu, setSignTofu);
        if (!signResult.ok) {
          setTofuError(signResult.message);
          return;
        }
      }

      setRecipient(user);
    }).catch(() => {});
  }, [conv.id]);

  // Scroll to bottom when messages change
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  async function handleSend(e) {
    e.preventDefault();
    if (!input.trim() || !recipient || tofuError) return;

    setSending(true);
    try {
      const encrypted = encryptMessage(input.trim(), recipient.publicKey, keys.signingSecretKey);
      await api.sendMessage(
        conv.id,
        encrypted.ciphertext,
        encrypted.nonce,
        encrypted.ephemeralPublicKey,
        encrypted.signature,
        token
      );
      setInput('');
    } catch (err) {
      console.error('Send failed:', err);
    } finally {
      setSending(false);
    }
  }

  async function handleDelete(msgId) {
    try {
      await api.deleteMessage(msgId, token);
      setMessages(prev => prev.filter(m => m.id !== msgId));
    } catch (err) {
      console.error('Delete failed:', err);
    }
  }

  async function handleForward(msg) {
    const targetConvId = window.prompt('Target conversation ID:');
    if (!targetConvId) return;

    const recipientUsername = window.prompt('Recipient username:');
    if (!recipientUsername) return;

    try {
      // Decrypt the original message
      const plaintext = decryptMessage(msg, keys.secretKey, signTofu);

      // Fetch recipient and check TOFU
      const user = await api.getUser(recipientUsername, token);
      if (!user) return alert('User not found.');

      const encResult = verifyOrPin(recipientUsername, user.publicKey, tofu, setTofu);
      if (!encResult.ok) return alert(encResult.message);

      // Re-encrypt for the target recipient
      const encrypted = encryptMessage(plaintext, user.publicKey, keys.signingSecretKey);
      await api.sendMessage(
        targetConvId,
        encrypted.ciphertext,
        encrypted.nonce,
        encrypted.ephemeralPublicKey,
        encrypted.signature,
        token
      );
      alert('Forwarded.');
    } catch (err) {
      alert('Forward failed: ' + err.message);
    }
  }

  function handleSave(msg) {
    try {
      const plaintext = decryptMessage(msg, keys.secretKey, signTofu);
      const content = [
        `From:      ${msg.senderUsername}`,
        `Timestamp: ${msg.timestamp}`,
        `MessageID: ${msg.id}`,
        '---',
        plaintext,
      ].join('\n');

      const blob = new Blob([content], { type: 'text/plain' });
      const url  = URL.createObjectURL(blob);
      const a    = document.createElement('a');
      a.href     = url;
      a.download = `message-${msg.id.slice(0, 8)}.txt`;
      a.click();
      URL.revokeObjectURL(url);
    } catch (err) {
      alert('Could not decrypt message: ' + err.message);
    }
  }

  async function handleRevoke() {
    const targetUsername = window.prompt('Revoke access for username:');
    if (!targetUsername) return;

    try {
      const user = await api.getUser(targetUsername, token);
      if (!user) return alert('User not found.');
      await api.revokeAccess(conv.id, user.id, token);
      alert(`Access revoked for ${targetUsername}.`);
      onRevoked();
    } catch (err) {
      alert('Revoke failed: ' + err.message);
    }
  }

  const otherName = conv.participants.filter(p => p !== username).join(', ') || '(just you)';

  return (
    <>
      <div className="panel-header">
        <span><strong>{otherName}</strong></span>
        <div style={{ display: 'flex', gap: 6 }}>
          <button onClick={onVerify} title="Verify message integrity on blockchain">Verify</button>
          <button onClick={handleRevoke}>Revoke access</button>
        </div>
      </div>

      {tofuError && <div className="tofu-warning">{tofuError}</div>}

      <div className="messages">
        {messages.map(msg => {
          const isMine = msg.senderUsername === username;
          let plaintext = null;
          let decryptError = null;

          try {
            plaintext = decryptMessage(msg, keys.secretKey, signTofu);
          } catch (err) {
            decryptError = err.message;
          }

          return (
            <div key={msg.id} className={`message-row ${isMine ? 'mine' : 'theirs'}`}>
              {!isMine && (
                <div className="message-meta">{msg.senderUsername}</div>
              )}
              <div className="message-bubble">
                {decryptError
                  ? isMine
                    ? <span style={{ color: '#888', fontStyle: 'italic' }}>[sent]</span>
                    : <span style={{ color: 'red', fontStyle: 'italic' }}>{decryptError}</span>
                  : plaintext
                }
              </div>
              <div className="message-meta">
                {new Date(msg.timestamp).toLocaleTimeString()}
              </div>
              <div className="message-actions">
                {isMine && (
                  <button onClick={() => handleDelete(msg.id)}>Delete</button>
                )}
                <button onClick={() => handleForward(msg)}>Forward</button>
                <button onClick={() => handleSave(msg)}>Save</button>
              </div>
            </div>
          );
        })}
        <div ref={bottomRef} />
      </div>

      <form className="chat-input-row" onSubmit={handleSend}>
        <input
          type="text"
          placeholder={tofuError ? 'Messaging blocked — TOFU mismatch' : 'Type a message…'}
          value={input}
          onChange={e => setInput(e.target.value)}
          disabled={!!tofuError || !recipient}
          autoComplete="off"
        />
        <button type="submit" disabled={sending || !!tofuError || !recipient}>
          {sending ? '…' : 'Send'}
        </button>
      </form>
    </>
  );
}
