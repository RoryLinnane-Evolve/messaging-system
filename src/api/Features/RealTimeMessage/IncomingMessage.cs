using System.Text.Json;

namespace api.Features.RealTimeMessage;

public class IncomingMessage
{
    public string Type { get; set; } = string.Empty;
    public JsonElement Data { get; set; }
}
