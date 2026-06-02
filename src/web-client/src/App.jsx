import { useState, useEffect, useRef } from 'react';
import sodium from 'libsodium-wrappers';
import { loadTofu, saveTofu, loadSignTofu, saveSignTofu } from './keystore';
import { generateKeypairs, encryptKeypairs, decryptKeypairs, b64Encode } from './crypto';
import { api } from './api';
import Auth from './components/Auth';
import ConversationList from './components/ConversationList';
import Chat from './components/Chat';
import NewConversation from './components/NewConversation';
import Settings from './components/Settings';
import Verify from './components/Verify';

export default function App() {
  // phase: 'loading' | 'auth' | 'passphrase' | 'app'
  const [phase, setPhase]       = useState('loading');
  const [initError, setInitError] = useState('');

  // Held between login and passphrase phases
  const [pendingToken, setPendingToken]     = useState(null);
  const [pendingUsername, setPendingUsername] = useState(null);
  const [pendingBlob, setPendingBlob]       = useState(null); // null = no blob on server

  // Crypto state
  const [keys, setKeys]         = useState(null);
  const [tofu, setTofu]         = useState({});
  const [signTofu, setSignTofu] = useState({});

  // Session state
  const [token, setToken]       = useState(null);
  const [username, setUsername] = useState(null);

  // App navigation
  const [view, setView]                     = useState('conversations');
  const [conversations, setConversations]   = useState([]);
  const [selectedConv, setSelectedConv]     = useState(null);
  const [messages, setMessages]             = useState([]);

  const selectedConvRef = useRef(null);
  useEffect(() => { selectedConvRef.current = selectedConv; }, [selectedConv]);

  const wsRef = useRef(null);

  // Initialise libsodium
  useEffect(() => {
    sodium.ready.then(() => {
      setTofu(loadTofu());
      setSignTofu(loadSignTofu());
      setPhase('auth');
    }).catch(() => setInitError('Failed to initialise libsodium.'));
  }, []);

  // ── Step 1: Login ─────────────────────────────────────────────────────────
  async function handleLogin(uname, password) {
    const result = await api.login(uname, password);
    // Fetch the encrypted key blob from the server
    const blobResult = await api.getKeyBlob(result.token);
    setPendingToken(result.token);
    setPendingUsername(uname);
    setPendingBlob(blobResult?.encryptedKeyBlob ?? null);
    setPhase('passphrase');
  }

  // ── Step 1 (alt): Sign up ─────────────────────────────────────────────────
  // Sign up collects username + password + passphrase all at once.
  // Keypair is generated here, encrypted with passphrase, and sent to server.
  async function handleSignUp(uname, password, passphrase) {
    const kp      = generateKeypairs();
    const blob    = encryptKeypairs(kp, passphrase);
    const blobStr = JSON.stringify(blob);

    await api.signUp(
      uname,
      password,
      b64Encode(kp.publicKey),
      b64Encode(kp.signingPublicKey),
      blobStr
    );

    // Auto-login, then skip passphrase phase since we already have keys
    const result = await api.login(uname, password);
    await enterApp(result.token, uname, kp);
  }

  // ── Step 2: Unlock keypair with passphrase ────────────────────────────────
  async function handlePassphrase(passphrase) {
    let kp;
    if (pendingBlob) {
      // Decrypt existing blob from server
      const stored = JSON.parse(pendingBlob);
      kp = decryptKeypairs(stored, passphrase); // throws on wrong passphrase
    } else {
      // No blob on server — account created via C++ client.
      // Generate a new keypair, upload blob, and note that old messages won't decrypt.
      kp = generateKeypairs();
      const blob    = encryptKeypairs(kp, passphrase);
      const blobStr = JSON.stringify(blob);
      await api.updateKeyBlob(
        { encryptedKeyBlob: blobStr, publicKey: b64Encode(kp.publicKey), signingPublicKey: b64Encode(kp.signingPublicKey) },
        pendingToken
      );
    }
    await enterApp(pendingToken, pendingUsername, kp);
  }

  // ── Common: enter app with token + keys ───────────────────────────────────
  async function enterApp(jwt, uname, kp) {
    setToken(jwt);
    setUsername(uname);
    setKeys(kp);
    const convs = await api.getConversations(jwt);
    setConversations(convs);
    connectWebSocket(jwt);
    setPendingToken(null);
    setPendingUsername(null);
    setPendingBlob(null);
    setPhase('app');
  }

  // ── Logout ────────────────────────────────────────────────────────────────
  async function handleLogout() {
    try { await api.logout(token); } catch {}
    if (wsRef.current) wsRef.current.close();
    setToken(null); setUsername(null); setKeys(null);
    setConversations([]); setSelectedConv(null); setMessages([]);
    setView('conversations');
    setPhase('auth');
  }

  // ── WebSocket ─────────────────────────────────────────────────────────────
  function connectWebSocket(jwt) {
    const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${wsProto}//${window.location.host}/ws?token=${jwt}`);
    ws.onmessage = (event) => {
      try {
        const { type, data } = JSON.parse(event.data);
        if (type === 'new_message') {
          const conv = selectedConvRef.current;
          if (conv && data.conversationId === conv.id) {
            setMessages(prev =>
              prev.some(m => m.id === data.id) ? prev : [...prev, data]
            );
          }
        } else if (type === 'access_revoked') {
          setConversations(prev => prev.filter(c => c.id !== data.conversationId));
          if (selectedConvRef.current?.id === data.conversationId) {
            setSelectedConv(null);
            setMessages([]);
            setView('conversations');
          }
        }
      } catch {}
    };
    ws.onclose = () => { setTimeout(() => { if (token) connectWebSocket(jwt); }, 3000); };
    wsRef.current = ws;
  }

  // ── Conversation helpers ──────────────────────────────────────────────────
  async function selectConversation(conv) {
    setSelectedConv(conv);
    setView('chat');
    const msgs = await api.getMessages(conv.id, token);
    setMessages(msgs);
  }

  async function refreshConversations() {
    const convs = await api.getConversations(token);
    setConversations(convs);
  }

  // ── Render ────────────────────────────────────────────────────────────────
  if (phase === 'loading') {
    return <div className="screen"><p>Loading…</p>{initError && <p className="error">{initError}</p>}</div>;
  }

  if (phase === 'auth' || phase === 'passphrase') {
    return (
      <Auth
        phase={phase}
        hasBlob={pendingBlob !== null}
        onLogin={handleLogin}
        onSignUp={handleSignUp}
        onPassphrase={handlePassphrase}
      />
    );
  }

  return (
    <div className="app">
      <header className="app-header">
        <span className="title">SecureMsg</span>
        <div className="nav">
          <span>Logged in as <strong>{username}</strong></span>
          <button onClick={() => setView('verify')}>Verify</button>
          <button onClick={() => setView('settings')}>Settings</button>
          <button onClick={handleLogout}>Logout</button>
        </div>
      </header>

      <div className="app-body">
        <ConversationList
          conversations={conversations}
          selectedConv={selectedConv}
          username={username}
          onSelect={selectConversation}
          onNewConversation={() => setView('new-conversation')}
        />

        <div className="panel">
          {view === 'chat' && selectedConv && (
            <Chat
              conv={selectedConv}
              messages={messages}
              setMessages={setMessages}
              username={username}
              keys={keys}
              token={token}
              tofu={tofu}
              setTofu={setTofu}
              signTofu={signTofu}
              setSignTofu={setSignTofu}
              onVerify={() => setView('verify')}
              onRevoked={refreshConversations}
            />
          )}
          {view === 'new-conversation' && (
            <NewConversation
              token={token}
              onCreated={async (conv) => { await refreshConversations(); selectConversation(conv); }}
              onCancel={() => setView('conversations')}
            />
          )}
          {view === 'settings' && (
            <Settings
              token={token}
              username={username}
              keys={keys}
              onAccountDeleted={handleLogout}
            />
          )}
          {view === 'verify' && (
            <Verify token={token} selectedConv={selectedConv} messages={messages} />
          )}
          {view === 'conversations' && (
            <div className="empty">Select a conversation or create a new one.</div>
          )}
        </div>
      </div>
    </div>
  );
}
