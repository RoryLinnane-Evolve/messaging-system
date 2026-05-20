using System.IdentityModel.Tokens.Jwt;
using System.Security.Claims;
using System.Security.Cryptography;
using System.Text;
using api.Config;
using api.Data;
using api.Data.Entities;
using Konscious.Security.Cryptography;
using Microsoft.EntityFrameworkCore;
using Microsoft.IdentityModel.Tokens;

namespace api.Features.Auth;

public interface IAuthService
{
    Task<RegistrationResult> Register(RegisterDto dto);
    Task<LogInResult?> LogIn(LogInDto dto);
}

public class AuthService : IAuthService
{
    private readonly AppDbContext _db;
    private readonly AppConfig _config;

    public AuthService(AppDbContext db, AppConfig config)
    {
        _db = db;
        _config = config;
    }

    public async Task<RegistrationResult> Register(RegisterDto dto)
    {
        var salt = RandomNumberGenerator.GetBytes(16);
        var hash = HashPassword(dto.Password, salt);

        var user = new Data.Entities.User
        {
            Username = dto.Username,
            PasswordHash = Convert.ToBase64String(hash),
            Salt = Convert.ToBase64String(salt),
            PublicKey = dto.PublicKey
        };

        _db.Users.Add(user);
        await _db.SaveChangesAsync();

        return new RegistrationResult();
    }

    public async Task<LogInResult?> LogIn(LogInDto dto)
    {
        var user = await _db.Users.FirstOrDefaultAsync(u => u.Username == dto.Username);

        // Always compute hash to prevent timing attacks — use dummy salt if user not found
        var salt = user != null
            ? Convert.FromBase64String(user.Salt)
            : RandomNumberGenerator.GetBytes(16);

        var hash = HashPassword(dto.Password, salt);
        var storedHash = user != null
            ? Convert.FromBase64String(user.PasswordHash)
            : new byte[32];

        if (user == null || !CryptographicOperations.FixedTimeEquals(hash, storedHash))
            return null;

        return new LogInResult(GenerateToken(user));
    }

    private byte[] HashPassword(string password, byte[] salt)
    {
        var argon2 = new Argon2id(Encoding.UTF8.GetBytes(password))
        {
            Salt = salt,
            KnownSecret = Encoding.UTF8.GetBytes(_config.ArgonPepper),
            DegreeOfParallelism = 4,
            MemorySize = 65536,
            Iterations = 3
        };

        return argon2.GetBytes(32);
    }

    private string GenerateToken(Data.Entities.User user)
    {
        var key = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(_config.JwtSecret));
        var creds = new SigningCredentials(key, SecurityAlgorithms.HmacSha256);

        var token = new JwtSecurityToken(
            claims:
            [
                new Claim(ClaimTypes.NameIdentifier, user.Id.ToString()),
                new Claim(ClaimTypes.Name, user.Username)
            ],
            expires: DateTime.UtcNow.AddHours(_config.JwtExpiryHours),
            signingCredentials: creds
        );

        return new JwtSecurityTokenHandler().WriteToken(token);
    }
}
