namespace api.Features.RealTimeMessage;

public static class RealTimeMessageExtensions
{
    public static IApplicationBuilder UseRealTimeMessages(this IApplicationBuilder app)
    {
        app.UseWebSockets(new WebSocketOptions
        {
            KeepAliveInterval = TimeSpan.FromSeconds(15)
        });

        return app.UseMiddleware<RealTimeMiddleware>();
    }
}
