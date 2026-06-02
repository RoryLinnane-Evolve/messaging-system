import { useState } from 'react';
import { api } from '../api';

export default function NewConversation({ token, onCreated, onCancel }) {
  const [input, setInput]   = useState('');
  const [error, setError]   = useState('');
  const [loading, setLoading] = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    setError('');

    const username = input.trim();
    if (!username) {
      return setError('Enter a username.');
    }

    setLoading(true);
    try {
      const conv = await api.createConversation(username, token);
      onCreated(conv);
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="form-panel">
      <h3>New Conversation</h3>
      <form onSubmit={handleSubmit} className="section">
        <div>
          <label>Recipient username</label>
          <input
            type="text"
            placeholder="alice"
            value={input}
            onChange={e => setInput(e.target.value)}
            autoFocus
          />
        </div>
        {error && <p className="error">{error}</p>}
        <div style={{ display: 'flex', gap: 8 }}>
          <button type="submit" disabled={loading}>
            {loading ? 'Creating…' : 'Create'}
          </button>
          <button type="button" onClick={onCancel}>Cancel</button>
        </div>
      </form>
    </div>
  );
}
