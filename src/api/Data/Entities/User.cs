using System.ComponentModel.DataAnnotations;

namespace api.Data.Entities;

public class User
{
    public Guid Id { get; set; } = Guid.NewGuid();

    [Required]
    [MaxLength(50)]
    public required string Username { get; set; }

    [Required]
    public required string PasswordHash { get; set; }

    [Required]
    public required string Salt { get; set; }

    [Required]
    public required string PublicKey { get; set; }

    [Required]
    public required string SigningPublicKey { get; set; }

    // Argon2id-encrypted keypair blob — stored server-side for cross-device access.
    // Server never sees the plaintext private key; only the passphrase-encrypted blob.
    public string? EncryptedKeyBlob { get; set; }

    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;

    // Navigation properties
    public ICollection<ConversationParticipant> Conversations { get; set; } = new List<ConversationParticipant>();
    public ICollection<Message> SentMessages { get; set; } = new List<Message>();
}