namespace api.Features.Message;

public class MessageDto
{
    public Guid Id { get; set; }
    public Guid ConversationId { get; set; }
    public string SenderUsername { get; set; } = string.Empty;
    public string SenderSigningKey { get; set; } = string.Empty;
    public string Ciphertext { get; set; } = string.Empty;
    public string Nonce { get; set; } = string.Empty;
    public string EphemeralPublicKey { get; set; } = string.Empty;
    public string Signature { get; set; } = string.Empty;
    public DateTime Timestamp { get; set; }
}

public class SendMessageDto
{
    public Guid ConversationId { get; set; }
    public string Ciphertext { get; set; } = string.Empty;
    public string Nonce { get; set; } = string.Empty;
    public string EphemeralPublicKey { get; set; } = string.Empty;
    public string Signature { get; set; } = string.Empty;
}
