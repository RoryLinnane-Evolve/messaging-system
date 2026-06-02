using System.ComponentModel.DataAnnotations;

namespace api.Features.Auth;

public sealed class RegistrationResult();
public sealed class LogInResult(string token) {
    public string Token { get; set; } = token;
};
public sealed class LogOutResult();

public sealed class RegisterDto
{
    [Required]
    [MinLength(3)]
    [MaxLength(50)]
    public string Username { get; set; } = string.Empty;

    [Required]
    [MinLength(8)]
    public string Password { get; set; } = string.Empty;

    [Required]
    public string PublicKey { get; set; } = string.Empty;

    [Required]
    public string SigningPublicKey { get; set; } = string.Empty;

    public string? EncryptedKeyBlob { get; set; }
}

public sealed class LogInDto
{
    [Required]
    public string Username { get; set; } = string.Empty;

    [Required]
    public string Password { get; set; } = string.Empty;
}
