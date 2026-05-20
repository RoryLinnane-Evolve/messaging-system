using System.Text;
using api.Config;
using api.Data;
using api.Features.Auth;
using api.Features.Blockchain;
using api.Features.Conversation;
using api.Features.Message;
using api.Features.RealTimeMessage;
using api.Features.User;
using api.Profiles;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.EntityFrameworkCore;
using Microsoft.IdentityModel.Tokens;

var builder = WebApplication.CreateBuilder(args);

// Config
var config = new AppConfig();
builder.Services.AddSingleton(config);

// Database
builder.Services.AddDbContext<AppDbContext>(options =>
    options.UseNpgsql(config.DatabaseConnection));

// Auth
builder.Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
    .AddJwtBearer(options =>
    {
        options.TokenValidationParameters = new TokenValidationParameters
        {
            ValidateIssuer = false,
            ValidateAudience = false,
            ValidateLifetime = true,
            ValidateIssuerSigningKey = true,
            IssuerSigningKey = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(config.JwtSecret))
        };
    });
builder.Services.AddAuthorization();

// Feature services
builder.Services.AddScoped<IAuthService, AuthService>();
builder.Services.AddScoped<IMessageService, MessageService>();
builder.Services.AddScoped<IConversationService, ConversationService>();
builder.Services.AddScoped<IUserService, UserService>();

// Blockchain — singleton because Web3 is thread-safe and holds the RPC connection
builder.Services.AddSingleton<IBlockchainService, BlockchainService>();

// RealTimeMessage — singletons because they hold in-memory connection state
builder.Services.AddSingleton<ConnectionManager>();
builder.Services.AddSingleton<MessageHandler>();

// AutoMapper
builder.Services.AddAutoMapper(cfg => cfg.AddProfile<MappingProfile>());

builder.Services.AddControllers();

var app = builder.Build();

// Apply any pending migrations on startup
using (var scope = app.Services.CreateScope())
{
    var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();
    await db.Database.MigrateAsync();
}

app.UseHttpsRedirection();
app.UseAuthentication();
app.UseAuthorization();
app.UseRealTimeMessages();
app.MapControllers();

app.Run();
