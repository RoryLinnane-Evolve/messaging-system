namespace api.Features.Auth;

public sealed class RegistrationResult();
public sealed class LogInResult(string token) {
    public string Token { get; set; } = token;
};
public sealed class LogOutResult();

public sealed class RegisterDto
{
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;
    public string PublicKey { get; set; } = string.Empty;
}

public sealed class LogInDto
{
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;
}
