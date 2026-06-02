import { useState } from 'react';

// Auth handles two sequential phases:
//   auth       → login or signup (signup also collects passphrase)
//   passphrase → unlock keypair after successful login

export default function Auth({ phase, hasBlob, onLogin, onSignUp, onPassphrase }) {
  return (
    <>
      {phase === 'auth'       && <AuthScreen onLogin={onLogin} onSignUp={onSignUp} />}
      {phase === 'passphrase' && <PassphraseScreen hasBlob={hasBlob} onPassphrase={onPassphrase} />}
    </>
  );
}

// ── Login / Sign up ───────────────────────────────────────────────────────────

function AuthScreen({ onLogin, onSignUp }) {
  const [tab, setTab]           = useState('login');
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [passphrase, setPassphrase] = useState('');
  const [error, setError]       = useState('');
  const [loading, setLoading]   = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    setError('');
    if (!username || !password) return setError('Username and password are required.');
    if (tab === 'signup' && !passphrase) return setError('Passphrase is required.');
    setLoading(true);
    try {
      if (tab === 'login') {
        await onLogin(username, password);
      } else {
        await onSignUp(username, password, passphrase);
      }
    } catch (err) {
      if (tab === 'login') {
        setError('User not found. Please check your username / password and try again.');
      } else {
        setError(err.message);
      }
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="screen">
      <h2>SecureMsg</h2>

      <div className="tabs">
        <button className={tab === 'login'  ? 'active' : ''} onClick={() => { setTab('login');  setError(''); }}>Login</button>
        <button className={tab === 'signup' ? 'active' : ''} onClick={() => { setTab('signup'); setError(''); }}>Sign up</button>
      </div>

      <form onSubmit={handleSubmit} style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <input
          type="text"
          placeholder="Username"
          value={username}
          onChange={e => setUsername(e.target.value)}
          autoFocus
          autoComplete="username"
        />
        <input
          type="password"
          placeholder="Password"
          value={password}
          onChange={e => setPassword(e.target.value)}
          autoComplete={tab === 'login' ? 'current-password' : 'new-password'}
        />
        {tab === 'signup' && (
          <>
            <input
              type="password"
              placeholder="Passphrase (protects your private key)"
              value={passphrase}
              onChange={e => setPassphrase(e.target.value)}
              autoComplete="new-password"
            />
            <p className="hint">
              Your passphrase encrypts your private key. It is never sent to the server.
              Do not forget it — it cannot be reset.
            </p>
          </>
        )}
        {error && <p className="error">{error}</p>}
        <button type="submit" disabled={loading}>
          {loading ? '…' : tab === 'login' ? 'Login' : 'Sign up'}
        </button>
      </form>
    </div>
  );
}

// ── Passphrase unlock (after login) ──────────────────────────────────────────

function PassphraseScreen({ hasBlob, onPassphrase }) {
  const [passphrase, setPassphrase] = useState('');
  const [error, setError]           = useState('');
  const [loading, setLoading]       = useState(false);

  async function handleSubmit(e) {
    e.preventDefault();
    setError('');
    if (!passphrase) return setError('Passphrase is required.');
    setLoading(true);
    try {
      await onPassphrase(passphrase);
    } catch {
      setError('Wrong passphrase. Please try again.');
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="screen">
      <h2>SecureMsg</h2>
      {!hasBlob && (
        <p className="hint" style={{ color: 'orange' }}>
          No keypair found for this account (it was created on another client).
          A new keypair will be generated. Previous messages will not be decryptable.
        </p>
      )}
      <p className="hint">Enter your passphrase to unlock your keypair.</p>
      <form onSubmit={handleSubmit} style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <input
          type="password"
          placeholder="Passphrase"
          value={passphrase}
          onChange={e => setPassphrase(e.target.value)}
          autoFocus
        />
        {error && <p className="error">{error}</p>}
        <button type="submit" disabled={loading}>
          {loading ? 'Unlocking…' : 'Unlock'}
        </button>
      </form>
    </div>
  );
}
