import { useState, useEffect } from 'react';
import { ethers } from 'ethers';
import { api } from '../api';

const RPC_URL = 'https://sepolia.infura.io/v3/4afc9a8ebfa5426393fbc1624cca0e42';

const EVENT_IFACE = new ethers.Interface([
  'event DigestRecorded(uint256 indexed id, bytes32 indexed hash, uint256 timestamp)',
]);

// Hash computation must match BlockchainService.cs exactly:
//   string.Concat(ciphertexts) → UTF-8 bytes → keccak256
function computeLocalHash(ciphertexts) {
  const combined  = ciphertexts.join('');
  const utf8Bytes = ethers.toUtf8Bytes(combined);
  return ethers.keccak256(utf8Bytes);
}

export default function Verify({ token, selectedConv, messages, conversations = [] }) {
  const [activeConvId, setActiveConvId]     = useState(selectedConv?.id ?? '');
  const [digests, setDigests]               = useState([]);
  const [selectedDigest, setSelectedDigest] = useState(null);
  const [txHash, setTxHash]                 = useState('');
  const [ciphertexts, setCiphertexts]       = useState('');
  const [result, setResult]                 = useState(null);
  const [error, setError]                   = useState('');
  const [loading, setLoading]               = useState(false);
  const [digestsLoading, setDigestsLoading] = useState(false);

  // Sync dropdown with selectedConv when navigating from a chat
  useEffect(() => {
    if (selectedConv?.id) setActiveConvId(selectedConv.id);
  }, [selectedConv?.id]);

  // Load digests whenever the active conversation changes
  useEffect(() => {
    if (!activeConvId || !token) {
      setDigests([]);
      return;
    }
    setDigests([]);
    setSelectedDigest(null);
    setResult(null);
    setDigestsLoading(true);
    api.getConversationDigests(activeConvId, token)
      .then(setDigests)
      .catch(() => setDigests([]))
      .finally(() => setDigestsLoading(false));
  }, [activeConvId]);

  function selectDigest(digest) {
    setSelectedDigest(digest);
    setResult(null);
    setError('');

    // Auto-populate the transaction hash from the stored record
    setTxHash(digest.transactionHash);

    // Find the slice of messages for this digest
    const firstIdx = messages.findIndex(m => m.id === digest.firstMessageId);
    const lastIdx  = messages.findIndex(m => m.id === digest.lastMessageId);

    if (firstIdx !== -1 && lastIdx !== -1) {
      const batch = messages.slice(firstIdx, lastIdx + 1);
      setCiphertexts(batch.map(m => m.ciphertext).join('\n'));
    } else {
      setCiphertexts('');
    }
  }

  async function handleVerify(e) {
    e.preventDefault();
    setError(''); setResult(null);

    const tx = txHash.trim();
    if (!tx.startsWith('0x') || tx.length !== 66)
      return setError('Enter a valid Ethereum transaction hash (0x + 64 hex characters).');

    const ctLines = ciphertexts.split('\n').map(l => l.trim()).filter(Boolean);
    if (ctLines.length === 0) return setError('Paste at least one ciphertext.');

    setLoading(true);
    try {
      const provider = new ethers.JsonRpcProvider(RPC_URL);
      const receipt  = await provider.getTransactionReceipt(tx);

      if (!receipt)
        return setError('Transaction not found on Sepolia. Check the hash and try again.');

      let onChainHash, timestamp, digestId;
      for (const log of receipt.logs) {
        try {
          const parsed = EVENT_IFACE.parseLog(log);
          if (parsed && parsed.name === 'DigestRecorded') {
            digestId    = parsed.args.id.toString();
            onChainHash = parsed.args.hash;
            timestamp   = parsed.args.timestamp;
            break;
          }
        } catch {}
      }

      if (!onChainHash)
        return setError('No DigestRecorded event found in this transaction.');

      const localHash = computeLocalHash(ctLines);
      const pass      = onChainHash.toLowerCase() === localHash.toLowerCase();

      setResult({ pass, onChainHash, localHash, timestamp, digestId });
    } catch (err) {
      setError('Error: ' + err.message);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="verify-panel">
      <h3>Blockchain Integrity Verification</h3>
      <p style={{ fontSize: 12, color: '#555' }}>
        Every 10 messages, a keccak256 hash of the ciphertexts is recorded on Ethereum Sepolia.
        Select a digest below to auto-populate, then verify against the chain.
      </p>

      {/* Conversation picker */}
      <div style={{ marginBottom: 8 }}>
        <label style={{ fontSize: 12, display: 'block', marginBottom: 4 }}>Conversation</label>
        <select
          value={activeConvId}
          onChange={e => setActiveConvId(e.target.value)}
          style={{ width: '100%' }}
        >
          <option value="">— select a conversation —</option>
          {conversations.map(c => (
            <option key={c.id} value={c.id}>{c.participants.join(', ')}</option>
          ))}
        </select>
      </div>

      {/* Digest list */}
      {activeConvId && (
        <>
          {digestsLoading && <p style={{ fontSize: 12 }}>Loading digests…</p>}
          {!digestsLoading && digests.length === 0 && (
            <p style={{ fontSize: 12, color: '#999' }}>
              No digests recorded yet (need at least 10 messages).
            </p>
          )}
          <div className="digest-list">
            {digests.map(d => (
              <div
                key={d.id}
                className={`digest-item${selectedDigest?.id === d.id ? ' selected' : ''}`}
                onClick={() => selectDigest(d)}
              >
                <div>
                  {d.onChainId != null ? `Digest #${d.onChainId}` : 'Digest'} — {new Date(d.recordedAt).toLocaleString()}
                </div>
                <div className="hash-text">TX: {d.transactionHash}</div>
              </div>
            ))}
          </div>
        </>
      )}

      {/* Verification form */}
      <form onSubmit={handleVerify} style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <div>
          <label style={{ fontSize: 12, display: 'block', marginBottom: 4 }}>
            Transaction Hash
          </label>
          <input
            type="text"
            placeholder="0x..."
            value={txHash}
            onChange={e => setTxHash(e.target.value)}
            spellCheck={false}
          />
        </div>
        <div>
          <label style={{ fontSize: 12, display: 'block', marginBottom: 4 }}>
            Ciphertexts — one base64 per line, in send order
          </label>
          <textarea
            rows={8}
            placeholder="Paste ciphertexts here, one per line…"
            value={ciphertexts}
            onChange={e => setCiphertexts(e.target.value)}
            style={{ resize: 'vertical' }}
          />
        </div>
        {error && <p style={{ color: 'red', fontSize: 12 }}>{error}</p>}
        <button type="submit" disabled={loading}>
          {loading ? 'Verifying…' : 'Verify on Sepolia'}
        </button>
      </form>

      {/* Result */}
      {result && (
        <div className={`verify-result ${result.pass ? 'pass' : 'fail'}`}>
          <strong>{result.pass ? '✓ PASS — Integrity verified' : '✗ FAIL — Tampered or mismatched'}</strong>
          <table style={{ marginTop: 8, fontSize: 12, borderCollapse: 'collapse', width: '100%' }}>
            <tbody>
              <tr><td style={{ width: 120, color: '#555' }}>Digest ID</td><td>{result.digestId}</td></tr>
              <tr>
                <td style={{ color: '#555' }}>Recorded at</td>
                <td>{new Date(Number(result.timestamp) * 1000).toUTCString()}</td>
              </tr>
              <tr><td style={{ color: '#555' }}>On-chain hash</td><td className="hash-text">{result.onChainHash}</td></tr>
              <tr><td style={{ color: '#555' }}>Computed hash</td><td className="hash-text">{result.localHash}</td></tr>
            </tbody>
          </table>
          {!result.pass && (
            <p style={{ color: 'red', fontSize: 12, marginTop: 8 }}>
              The computed hash does not match the on-chain record. Either the ciphertexts have
              been altered since the digest was recorded, or they were provided in the wrong order.
            </p>
          )}
        </div>
      )}
    </div>
  );
}
