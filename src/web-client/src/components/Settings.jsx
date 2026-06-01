import { useState } from 'react';
import { api } from '../api';
import { encryptKeypairs, b64Encode } from '../crypto';

export default function Settings({ token, username, keys, onAccountDeleted }) {
  const [currentPw, setCurrentPw] = useState('');
  const [newPw, setNewPw]         = useState('');
  const [pwError, setPwError]     = useState('');
  const [pwSuccess, setPwSuccess] = useState('');
  const [pwLoading, setPwLoading] = useState(false);

  const [currentPhrase, setCurrentPhrase] = useState('');
  const [newPhrase, setNewPhrase]         = useState('');
  const [phraseError, setPhraseError]     = useState('');
  const [phraseSuccess, setPhraseSuccess] = useState('');
  const [phraseLoading, setPhraseLoading] = useState(false);

  const [deleteConfirm, setDeleteConfirm] = useState('');
  const [deleteError, setDeleteError]     = useState('');
  const [deleteLoading, setDeleteLoading] = useState(false);

  async function handleChangePassword(e) {
    e.preventDefault();
    setPwError(''); setPwSuccess('');
    if (!currentPw || !newPw) return setPwError('Both fields are required.');
    setPwLoading(true);
    try {
      await api.changePassword(currentPw, newPw, token);
      setPwSuccess('Password changed.');
      setCurrentPw(''); setNewPw('');
    } catch (err) {
      setPwError(err.message);
    } finally {
      setPwLoading(false);
    }
  }

  async function handleChangePassphrase(e) {
    e.preventDefault();
    setPhraseError(''); setPhraseSuccess('');
    if (!newPhrase) return setPhraseError('New passphrase is required.');
    setPhraseLoading(true);
    try {
      // Re-encrypt the in-memory keypair with the new passphrase and upload
      const blob    = encryptKeypairs(keys, newPhrase);
      const blobStr = JSON.stringify(blob);
      await api.updateKeyBlob({ encryptedKeyBlob: blobStr }, token);
      setPhraseSuccess('Passphrase updated. Use the new passphrase next time you log in.');
      setCurrentPhrase(''); setNewPhrase('');
    } catch (err) {
      setPhraseError(err.message);
    } finally {
      setPhraseLoading(false);
    }
  }

  async function handleDeleteAccount(e) {
    e.preventDefault();
    setDeleteError('');
    if (deleteConfirm !== 'DELETE') return setDeleteError('Type DELETE to confirm.');
    setDeleteLoading(true);
    try {
      await api.deleteAccount(token);
      onAccountDeleted();
    } catch (err) {
      setDeleteError(err.message);
    } finally {
      setDeleteLoading(false);
    }
  }

  return (
    <div className="form-panel">
      <h3>Account Settings</h3>
      <p style={{ color: '#555', fontSize: 12 }}>Logged in as <strong>{username}</strong></p>

      {/* Change password */}
      <form onSubmit={handleChangePassword} className="section">
        <h4>Change Password</h4>
        <div>
          <label>Current password</label>
          <input type="password" value={currentPw} onChange={e => setCurrentPw(e.target.value)} autoComplete="current-password" />
        </div>
        <div>
          <label>New password</label>
          <input type="password" value={newPw} onChange={e => setNewPw(e.target.value)} autoComplete="new-password" />
        </div>
        {pwError   && <p className="error">{pwError}</p>}
        {pwSuccess && <p className="success">{pwSuccess}</p>}
        <button type="submit" disabled={pwLoading}>{pwLoading ? 'Saving…' : 'Change password'}</button>
      </form>

      {/* Change passphrase */}
      <form onSubmit={handleChangePassphrase} className="section">
        <h4>Change Passphrase</h4>
        <p style={{ fontSize: 12, color: '#555' }}>
          Your passphrase protects your private key. Changing it re-encrypts your keypair on the server.
        </p>
        <div>
          <label>New passphrase</label>
          <input type="password" value={newPhrase} onChange={e => setNewPhrase(e.target.value)} autoComplete="new-password" />
        </div>
        {phraseError   && <p className="error">{phraseError}</p>}
        {phraseSuccess && <p className="success">{phraseSuccess}</p>}
        <button type="submit" disabled={phraseLoading}>{phraseLoading ? 'Saving…' : 'Change passphrase'}</button>
      </form>

      {/* Delete account */}
      <form onSubmit={handleDeleteAccount} className="section">
        <h4>Delete Account</h4>
        <p style={{ color: 'red', fontSize: 12 }}>This is permanent. All your conversations and messages will be deleted.</p>
        <div>
          <label>Type DELETE to confirm</label>
          <input type="text" value={deleteConfirm} onChange={e => setDeleteConfirm(e.target.value)} />
        </div>
        {deleteError && <p className="error">{deleteError}</p>}
        <button type="submit" disabled={deleteLoading} style={{ color: 'red' }}>
          {deleteLoading ? 'Deleting…' : 'Delete account'}
        </button>
      </form>
    </div>
  );
}
