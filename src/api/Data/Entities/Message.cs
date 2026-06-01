using System.ComponentModel.DataAnnotations;

namespace api.Data.Entities;

public class Message
{
    public Guid Id { get; set; } = Guid.NewGuid();

    public Guid ConversationId { get; set; }
    public Guid? SenderId { get; set; }

    [Required]
    public required string Ciphertext { get; set; }

    [Required]
    public required string Nonce { get; set; }

    [Required]
    public required string EphemeralPublicKey { get; set; }

    // Ed25519 detached signature over (ciphertext_bytes || nonce_bytes || ephemeralPublicKey_bytes).
    // Nullable to allow reading messages sent before sender authentication was implemented.
    public string? Signature { get; set; }

    public DateTime Timestamp { get; set; } = DateTime.UtcNow;

    // Navigation properties
    public Conversation Conversation { get; set; } = null!;
    public User? Sender { get; set; }
}