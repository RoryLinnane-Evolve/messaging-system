using System.Security.Claims;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace api.Features.Message;

[Authorize]
[ApiController]
[Route("api/[controller]")]
public class MessageController : ControllerBase
{
    private readonly IMessageService _messageService;

    public MessageController(IMessageService messageService)
    {
        _messageService = messageService;
    }

    private Guid UserId => Guid.Parse(User.FindFirstValue(ClaimTypes.NameIdentifier)!);

    [HttpGet("conversation/{conversationId:guid}")]
    public async Task<ActionResult<IEnumerable<MessageDto>>> GetByConversation(Guid conversationId)
    {
        var messages = await _messageService.GetMessages(conversationId, UserId);
        return Ok(messages);
    }

    [HttpPost]
    public async Task<IActionResult> Send(SendMessageDto dto)
    {
        await _messageService.SendMessage(UserId, dto);
        return Ok();
    }

    [HttpGet("{id:guid}")]
    public async Task<ActionResult<MessageDto>> Get(Guid id)
    {
        var message = await _messageService.GetMessage(id, UserId);

        if (message is null)
            return NotFound();

        return Ok(message);
    }

    [HttpDelete("{id:guid}")]
    public async Task<IActionResult> Delete(Guid id)
    {
        var success = await _messageService.DeleteMessage(id, UserId);

        if (!success)
            return NotFound();

        return NoContent();
    }

    [HttpPost("{conversationId:guid}/revoke/{targetUserId:guid}")]
    public async Task<IActionResult> RevokeAccess(Guid conversationId, Guid targetUserId)
    {
        var success = await _messageService.RevokeAccess(conversationId, targetUserId, UserId);

        if (!success)
            return NotFound();

        return NoContent();
    }
}
