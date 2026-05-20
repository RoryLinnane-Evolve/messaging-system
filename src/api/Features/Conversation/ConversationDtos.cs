using api.Features.Message;

namespace api.Features.Conversation;

public class ConversationDto
{
    public Guid Id { get; set; }
    public List<string> Participants { get; set; } = [];
    public List<MessageDto> Messages { get; set; } = [];
    public DateTime CreatedAt { get; set; }
}

public class ConversationItemDto
{
    public Guid Id { get; set; }
    public List<string> Participants { get; set; } = [];
    public DateTime CreatedAt { get; set; }
}

public class CreateConversationDto
{
    public List<string> ParticipantUsernames { get; set; } = [];
}
