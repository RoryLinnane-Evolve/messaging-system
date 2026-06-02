import { useState, useEffect } from 'react';
import { ethers } from 'ethers';
import { api } from '../api';

const RPC_URL          = 'https://sepolia.infura.io/v3/4afc9a8ebfa5426393fbc1624cca0e42';
const CONTRACT_ADDRESS = '0x94Cb9083B3ACDCaCe25ebb3E29ceAE5bF436e850';

const ABI = [
  {
    inputs: [{ internalType: 'uint256', name: 'id', type: 'uint256' }],
    name: 'getDigest',
    outputs: [
      { internalType: 'bytes32', name: 'hash',      type: 'bytes32' },
      { internalType: 'uint256', name: 'timestamp', type: 'uint256' },
    ],
    stateMutability: 'view',
    type: 'function',
  },
  {
    inputs: [],
    name: 'digestCount',
    outputs: [{ internalType: 'uint256', name: '', type: 'uint256' }],
    stateMutability: 'view',
    type: 'function',
  },
];

// Hash computation must match BlockchainService.cs exactly:
//   string.Concat(ciphertexts) → UTF-8 bytes → keccak256
function computeLocalHash(ciphertexts) {
  const combined  = ciphertexts.join('');
  const utf8Bytes = ethers.toUtf8Bytes(combined);
  return ethers.keccak256(utf8Bytes);
}

export default function Verify({ token, selectedConv, messages }) {
  const [digests, setDigests]           = useState([]);
  const [selectedDigest, setSelectedDigest] = useState(null);
  const [digestId, setDigestId]         = useState('');
  const [ciphertexts, setCiphertexts]   = useState('');
  const [result, setResult]             = useState(null);   // { pass, onChainHash, localHash, timestamp, digestId }
  const [error, setError]               = useState('');
  const [loading, setLoading]           = useState(false);
  const [digestsLoading, setDigestsLoading] = useState(false);

  // Load digests for the selected conversation
  useEffect(() => {
    if (!selectedConv || !token) {
      setDigests([]);
      return;
    }
    setDigestsLoading(true);
    api.getConversationDigests(selectedConv.id, token)
      .then(setDigests)
      .catch(() => setDigests([]))
      .finally(() => setDigestsLoading(false));
  }, [selectedConv?.id]);

  // When a digest record is selected, auto-populate the ciphertexts from
  // the messages that fall between firstMessageId and lastMessageId.
  function selectDigest(digest) {
    setSelectedDigest(digest);
    setResult(null);
    setError('');

    // Find the slice of messages for this digest
    const firstIdx = messages.findIndex(m => m.id === digest.firstMessageId);
    const lastIdx  = messages.findIndex(m => m.id === digest.lastMessageId);

    if (firstIdx !== -1 && lastIdx !== -1) {
      const batch = messages.slice(firstIdx, lastIdx + 1);
      setCiphertexts(batch.map(m => m.ciphertext).join('\n'));
    } else {
      // Messages might not be loaded — show hash from DB as a hint
      setCiphertexts('');
    }
  }

  async function handleVerify(e) {
    e.preventDefault();
    setError(''); setResult(null);

    const id = parseInt(digestId, 10);
    if (isNaN(id)) return setError('Digest ID must be a number.');

    const ctLines = ciphertexts.split('\n').map(l => l.trim()).filter(Boolean);
    if (ctLines.length === 0) return setError('Paste at least one ciphertext.');

    setLoading(true);
    try {
      const provider = new ethers.JsonRpcProvider(RPC_URL);
      const contract = new ethers.Contract(CONTRACT_ADDRESS, ABI, provider);

      const [onChainHash, timestamp] = await contract.getDigest(id);
      const localHash = computeLocalHash(ctLines);
      const pass = onChainHash.toLowerCase() === localHash.toLowerCase();

      setResult({ pass, onChainHash, localHash, timestamp, digestId: id });
    } catch (err) {
      if (err.message?.includes('bad result from backend')) {
        setError('Digest ID not found on chain. Has this batch been recorded yet?');
      } else {
        setError('Error: ' + err.message);
      }
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="verify-panel">
      <h3>Blockchain Integrity Verification</h3>
      <p style={{ fontSize: 12, color: '#555' }}>
        Every 10 messages, a keccak256 hash of the ciphertexts is recorded on Ethereum Sepolia.
        Select a recorded digest below to auto-populate the ciphertexts, then enter the on-chain
        digest ID (0-indexed) and verify.
      </p>

      {/* Digest records for the selected conversation */}
      {selectedConv ? (
        <>
          <strong style={{ fontSize: 12 }}>
            Recorded digests for: {selectedConv.participants.join(', ')}
          </strong>
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
                <div>Recorded: {new Date(d.recordedAt).toLocaleString()}</div>
                <div className="hash-text">TX: {d.transactionHash}</div>
              </div>
            ))}
          </div>
        </>
      ) : (
        <p style={{ fontSize: 12, color: '#999' }}>
          Select a conversation to auto-populate digests, or fill in manually below.
        </p>
      )}

      {/* Verification form */}
      <form onSubmit={handleVerify} style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <div>
          <label style={{ fontSize: 12, display: 'block', marginBottom: 4 }}>
            On-chain Digest ID (0 = first batch, 1 = second, etc.)
          </label>
          <input
            type="number"
            min="0"
            placeholder="e.g. 0"
            value={digestId}
            onChange={e => setDigestId(e.target.value)}
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
