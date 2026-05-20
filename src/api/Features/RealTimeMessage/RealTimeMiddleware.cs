using api.Config;

namespace api.Features.RealTimeMessage;

public class RealTimeMiddleware
{
    private readonly RequestDelegate _next;

    public RealTimeMiddleware(RequestDelegate next)
    {
        _next = next;
    }

    public async Task InvokeAsync(HttpContext context, MessageHandler handler, AppConfig config)
    {
        if (context.Request.Path != "/ws")
        {
            await _next(context);
            return;
        }

        if (!context.WebSockets.IsWebSocketRequest)
        {
            context.Response.StatusCode = StatusCodes.Status400BadRequest;
            return;
        }

        var token = context.Request.Query["token"].ToString();
        var userId = TokenHelper.ValidateToken(token, config.JwtSecret);

        if (userId is null)
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            return;
        }

        var socket = await context.WebSockets.AcceptWebSocketAsync();
        await handler.Handle(socket, userId.Value);
    }
}
