import { useState } from 'react';
import { api } from '../api';

export default function NewConversation({ token, onCreated, onCancel }) {
  const [input, setInput]   = useState('');
  const [error, setError]   = useState('');
  const [loading, setLoading] = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    setError('');

    const usernames = input.split(/[\s,]+/).map(s => s.trim()).filter(Boolean);
    if (usernames.length === 0) {
      return setError('Enter at least one username.');
    }

    setLoading(true);
    try {
      const conv = await api.createConversation(usernames, token);
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
          <label>Participant usernames (comma or space separated)</label>
          <input
            type="text"
            placeholder="alice, bob"
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
