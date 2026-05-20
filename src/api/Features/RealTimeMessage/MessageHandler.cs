using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using api.Features.Message;
using IMessageService = api.Features.Message.IMessageService;

namespace api.Features.RealTimeMessage;

public class MessageHandler
{
    private readonly ConnectionManager _connections;
    private readonly IServiceScopeFactory _scopeFactory;

    // IServiceScopeFactory is used because this is a singleton that needs
    // scoped services (e.g. MessageService) per request
    public MessageHandler(ConnectionManager connections, IServiceScopeFactory scopeFactory)
    {
        _connections = connections;
        _scopeFactory = scopeFactory;
    }

    public async Task Handle(WebSocket socket, Guid userId)
    {
        _connections.Add(userId, socket);
        var buffer = new byte[4096];

        try
        {
            while (socket.State == WebSocketState.Open)
            {
                var result = await socket.ReceiveAsync(buffer, CancellationToken.None);

                if (result.MessageType == WebSocketMessageType.Close)
                    break;

                var json = Encoding.UTF8.GetString(buffer, 0, result.Count);
                await Dispatch(userId, json);
            }
        }
        finally
        {
            _connections.Remove(userId);
            if (socket.State == WebSocketState.Open)
                await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "Closed", CancellationToken.None);
        }
    }

    private async Task Dispatch(Guid userId, string json)
    {
        var message = JsonSerializer.Deserialize<IncomingMessage>(json);

        switch (message?.Type)
        {
            case "send_message": {
                await using var scope = _scopeFactory.CreateAsyncScope();
                var messageService = scope.ServiceProvider.GetRequiredService<IMessageService>();
                var dto = message.Data.Deserialize<SendMessageDto>();
                if (dto is null) break;
                await messageService.SendMessage(userId, dto);
                break;
            }

            case "ping":
                await _connections.SendToUser(userId, new { type = "pong" });
                break;
        }
    }
}
