using System.Security.Cryptography;
using System.Text;
using api.Config;
using api.Data;
using AutoMapper;
using Konscious.Security.Cryptography;
using Microsoft.EntityFrameworkCore;

namespace api.Features.User;

public interface IUserService
{
    Task<UserDto?> GetUser(string username);
    Task<bool> ChangePassword(Guid userId, ChangePasswordDto dto);
    Task DeleteUser(Guid userId);
}

public class UserService : IUserService
{
    private readonly AppDbContext _db;
    private readonly AppConfig _config;
    private readonly IMapper _mapper;

    public UserService(AppDbContext db, AppConfig config, IMapper mapper)
    {
        _db = db;
        _config = config;
        _mapper = mapper;
    }

    public async Task<UserDto?> GetUser(string username)
    {
        var user = await _db.Users.FirstOrDefaultAsync(u => u.Username == username);
        return user == null ? null : _mapper.Map<UserDto>(user);
    }

    public async Task<bool> ChangePassword(Guid userId, ChangePasswordDto dto)
    {
        var user = await _db.Users.FindAsync(userId);
        if (user == null)
            return false;

        var currentHash = HashPassword(dto.CurrentPassword, Convert.FromBase64String(user.Salt));
        var storedHash = Convert.FromBase64String(user.PasswordHash);

        if (!CryptographicOperations.FixedTimeEquals(currentHash, storedHash))
            return false;

        var newSalt = RandomNumberGenerator.GetBytes(16);
        user.Salt = Convert.ToBase64String(newSalt);
        user.PasswordHash = Convert.ToBase64String(HashPassword(dto.NewPassword, newSalt));

        await _db.SaveChangesAsync();
        return true;
    }

    public async Task DeleteUser(Guid userId)
    {
        var user = await _db.Users.FindAsync(userId);
        if (user == null)
            return;

        // SenderId is nullable with SetNull — EF will null it out on delete
        _db.Users.Remove(user);
        await _db.SaveChangesAsync();
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
}
