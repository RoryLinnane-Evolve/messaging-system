using System.ComponentModel.DataAnnotations;

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
    public string? Signature { get; set; }
    public DateTime Timestamp { get; set; }
}

public class SendMessageDto
{
    [Required]
    public Guid ConversationId { get; set; }

    [Required]
    [MaxLength(65536)]
    public string Ciphertext { get; set; } = string.Empty;

    [Required]
    [MaxLength(256)]
    public string Nonce { get; set; } = string.Empty;

    [Required]
    [MaxLength(256)]
    public string EphemeralPublicKey { get; set; } = string.Empty;

    [MaxLength(256)]
    public string? Signature { get; set; }
}
