// Sidebar: list of conversations + button to start a new one.

export default function ConversationList({
  conversations,
  selectedConv,
  username,
  onSelect,
  onNewConversation,
}) {
  return (
    <div className="sidebar">
      <div className="sidebar-header">
        <span>Conversations</span>
        <button onClick={onNewConversation} title="New conversation">+</button>
      </div>

      <div className="conv-list">
        {conversations.length === 0 && (
          <div style={{ padding: 10, color: '#999', fontSize: 12 }}>No conversations yet.</div>
        )}
        {conversations.map(conv => {
          // Show the other participants' names as the conversation label
          const others = conv.participants.filter(p => p !== username);
          const label  = others.length > 0 ? others.join(', ') : '(just you)';
          const isActive = selectedConv?.id === conv.id;

          return (
            <div
              key={conv.id}
              className={`conv-item${isActive ? ' active' : ''}`}
              onClick={() => onSelect(conv)}
              title={label}
            >
              {label}
            </div>
          );
        })}
      </div>
    </div>
  );
}
