using System.Security.Claims;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace api.Features.Conversation;

[Authorize]
[ApiController]
[Route("api/[controller]")]
public class ConversationController : ControllerBase
{
    private readonly IConversationService _conversationService;

    public ConversationController(IConversationService conversationService)
    {
        _conversationService = conversationService;
    }

    private Guid UserId => Guid.Parse(User.FindFirstValue(ClaimTypes.NameIdentifier)!);

    [HttpGet]
    public async Task<ActionResult<IEnumerable<ConversationItemDto>>> GetAll()
    {
        var conversations = await _conversationService.GetConversations(UserId);
        return Ok(conversations);
    }

    [HttpGet("{id:guid}")]
    public async Task<ActionResult<ConversationDto>> Get(Guid id)
    {
        var conversation = await _conversationService.GetConversation(id, UserId);

        if (conversation is null)
            return NotFound();

        return Ok(conversation);
    }

    [HttpPost]
    public async Task<ActionResult<ConversationDto>> Create(CreateConversationDto dto)
    {
        var conversation = await _conversationService.CreateConversation(UserId, dto);
        return CreatedAtAction(nameof(Get), new { id = conversation.Id }, conversation);
    }
}
