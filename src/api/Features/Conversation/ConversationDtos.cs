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

public class ConversationDigestDto
{
    public Guid Id { get; set; }
    public Guid ConversationId { get; set; }
    public Guid FirstMessageId { get; set; }
    public Guid LastMessageId { get; set; }
    public string Hash { get; set; } = string.Empty;
    public string TransactionHash { get; set; } = string.Empty;
    public DateTime RecordedAt { get; set; }
}
