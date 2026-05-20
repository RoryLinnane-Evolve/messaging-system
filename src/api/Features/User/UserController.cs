using System.Security.Claims;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace api.Features.User;

[Authorize]
[ApiController]
[Route("api/[controller]")]
public class UserController : ControllerBase
{
    private readonly IUserService _userService;

    public UserController(IUserService userService)
    {
        _userService = userService;
    }

    private Guid UserId => Guid.Parse(User.FindFirstValue(ClaimTypes.NameIdentifier)!);

    [HttpGet("{username}")]
    public async Task<ActionResult<UserDto>> Get(string username)
    {
        var user = await _userService.GetUser(username);

        if (user is null)
            return NotFound();

        return Ok(user);
    }

    [HttpPut("password")]
    public async Task<IActionResult> ChangePassword(ChangePasswordDto dto)
    {
        var success = await _userService.ChangePassword(UserId, dto);

        if (!success)
            return BadRequest();

        return NoContent();
    }

    [HttpDelete]
    public async Task<IActionResult> Delete()
    {
        await _userService.DeleteUser(UserId);
        return NoContent();
    }
}
