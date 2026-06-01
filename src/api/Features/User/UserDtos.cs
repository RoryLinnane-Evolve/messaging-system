using System.ComponentModel.DataAnnotations;

namespace api.Features.User;

public class UserDto
{
    public Guid Id { get; set; }
    public string Username { get; set; } = string.Empty;
    public string PublicKey { get; set; } = string.Empty;
    public string SigningPublicKey { get; set; } = string.Empty;
}

public class ChangePasswordDto
{
    [Required]
    public string CurrentPassword { get; set; } = string.Empty;

    [Required]
    [MinLength(8)]
    public string NewPassword { get; set; } = string.Empty;
}

public class KeyBlobDto
{
    public string? EncryptedKeyBlob { get; set; }
}

public class UpdateKeyBlobDto
{
    [Required]
    public string EncryptedKeyBlob { get; set; } = string.Empty;

    // When updating the blob (e.g. passphrase change), the public keys may also change
    public string? PublicKey { get; set; }
    public string? SigningPublicKey { get; set; }
}
