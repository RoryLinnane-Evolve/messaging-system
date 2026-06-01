using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.RateLimiting;

namespace api.Features.Auth;

[ApiController]
[Route("api/[controller]")]
public class AuthController : ControllerBase
{
    private readonly IAuthService _authService;

    public AuthController(IAuthService authService)
    {
        _authService = authService;
    }

    [EnableRateLimiting("auth")]
    [HttpPost("sign-up")]
    public async Task<ActionResult<RegistrationResult>> SignUp(RegisterDto dto)
    {
        var result = await _authService.Register(dto);
        return Ok(result);
    }

    [EnableRateLimiting("auth")]
    [HttpPost("login")]
    public async Task<ActionResult<LogInResult>> LogIn(LogInDto dto)
    {
        var result = await _authService.LogIn(dto);

        if (result is null)
            return Unauthorized();

        return Ok(result);
    }

    [Authorize]
    [HttpPost("logout")]
    public IActionResult LogOut()
    {
        // Token invalidation is handled client-side — client discards the JWT
        return NoContent();
    }
}
