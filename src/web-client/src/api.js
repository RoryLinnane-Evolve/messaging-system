async function request(method, path, body = null, token = null) {
  const headers = { 'Content-Type': 'application/json' };
  if (token) headers['Authorization'] = `Bearer ${token}`;

  const res = await fetch(path, {
    method,
    headers,
    body: body !== null ? JSON.stringify(body) : undefined,
  });

  if (!res.ok) {
    const text = await res.text().catch(() => '');
    throw new Error(`HTTP ${res.status}${text ? ': ' + text : ''}`);
  }

  const text = await res.text();
  return text ? JSON.parse(text) : null;
}

export const api = {
  // Auth
  signUp: (username, password, publicKey, signingPublicKey, encryptedKeyBlob) =>
    request('POST', '/api/auth/sign-up', { username, password, publicKey, signingPublicKey, encryptedKeyBlob }),

  login: (username, password) =>
    request('POST', '/api/auth/login', { username, password }),

  logout: (token) =>
    request('POST', '/api/auth/logout', null, token),

  // Users
  getUser: (username, token) =>
    request('GET', `/api/user/${username}`, null, token),

  changePassword: (currentPassword, newPassword, token) =>
    request('PUT', '/api/user/password', { currentPassword, newPassword }, token),

  deleteAccount: (token) =>
    request('DELETE', '/api/user', null, token),

  // Key blob (server-stored encrypted keypair)
  getKeyBlob: (token) =>
    request('GET', '/api/user/keys', null, token),

  updateKeyBlob: (dto, token) =>
    request('PUT', '/api/user/keys', dto, token),

  // Conversations
  getConversations: (token) =>
    request('GET', '/api/conversation', null, token),

  createConversation: (participantUsernames, token) =>
    request('POST', '/api/conversation', { participantUsernames }, token),

  getConversationDigests: (conversationId, token) =>
    request('GET', `/api/conversation/${conversationId}/digests`, null, token),

  // Messages
  getMessages: (conversationId, token) =>
    request('GET', `/api/message/conversation/${conversationId}`, null, token),

  sendMessage: (conversationId, ciphertext, nonce, ephemeralPublicKey, signature, token) =>
    request('POST', '/api/message', { conversationId, ciphertext, nonce, ephemeralPublicKey, signature }, token),

  deleteMessage: (id, token) =>
    request('DELETE', `/api/message/${id}`, null, token),

  revokeAccess: (conversationId, userId, token) =>
    request('POST', `/api/message/${conversationId}/revoke/${userId}`, {}, token),
};
