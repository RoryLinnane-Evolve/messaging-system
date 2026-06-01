using System.ComponentModel.DataAnnotations;

namespace api.Features.Message;

public class MessageDto
{
    public Guid Id { get; set; }
    public Guid ConversationId { get; set; }
    public string SenderUsername { get; set; } = string.Empty;
    public string Ciphertext { get; set; } = string.Empty;
    public string Nonce { get; set; } = string.Empty;
    public string EphemeralPublicKey { get; set; } = string.Empty;
    public string? Signature { get; set; }
    public DateTime Timestamp { get; set; }
}

public class SendMessageDto
{
    [Required]
    public Guid ConversationId { get; set; }

    [Required]
    public string Ciphertext { get; set; } = string.Empty;

    [Required]
    public string Nonce { get; set; } = string.Empty;

    [Required]
    public string EphemeralPublicKey { get; set; } = string.Empty;

    public string? Signature { get; set; }
}
