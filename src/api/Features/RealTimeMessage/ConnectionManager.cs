using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;

namespace api.Features.RealTimeMessage;

public class ConnectionManager
{
    private static readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    private readonly ConcurrentDictionary<Guid, WebSocket> _connections = new();

    public void Add(Guid userId, WebSocket socket) =>
        _connections[userId] = socket;

    public void Remove(Guid userId) =>
        _connections.TryRemove(userId, out _);

    public async Task SendToUser(Guid userId, object payload)
    {
        if (_connections.TryGetValue(userId, out var socket) &&
            socket.State == WebSocketState.Open)
        {
            var bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(payload, _jsonOptions));
            await socket.SendAsync(bytes, WebSocketMessageType.Text, true, CancellationToken.None);
        }
    }
}
