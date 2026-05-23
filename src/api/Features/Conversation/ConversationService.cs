using api.Data;
using api.Data.Entities;
using AutoMapper;
using Microsoft.EntityFrameworkCore;

namespace api.Features.Conversation;

public interface IConversationService
{
    Task<IEnumerable<ConversationItemDto>> GetConversations(Guid userId);
    Task<ConversationDto?> GetConversation(Guid conversationId, Guid userId);
    Task<ConversationDto> CreateConversation(Guid userId, CreateConversationDto dto);
}

public sealed class ConversationService : IConversationService
{
    private readonly AppDbContext _db;
    private readonly IMapper _mapper;

    public ConversationService(AppDbContext db, IMapper mapper)
    {
        _db = db;
        _mapper = mapper;
    }

    public async Task<IEnumerable<ConversationItemDto>> GetConversations(Guid userId)
    {
        var conversations = await _db.Conversations
            .Include(c => c.Participants)
                .ThenInclude(p => p.User)
            .Where(c => c.Participants.Any(p => p.UserId == userId))
            .ToListAsync();

        return _mapper.Map<IEnumerable<ConversationItemDto>>(conversations);
    }

    public async Task<ConversationDto?> GetConversation(Guid conversationId, Guid userId)
    {
        var conversation = await _db.Conversations
            .Include(c => c.Participants)
                .ThenInclude(p => p.User)
            .Include(c => c.Messages)
                .ThenInclude(m => m.Sender)
            .FirstOrDefaultAsync(c => c.Id == conversationId);

        if (conversation == null)
            return null;

        var isParticipant = conversation.Participants.Any(p => p.UserId == userId);
        if (!isParticipant)
            return null;

        return _mapper.Map<ConversationDto>(conversation);
    }

    public async Task<ConversationDto> CreateConversation(Guid userId, CreateConversationDto dto)
    {
        var recipient = await _db.Users
            .FirstOrDefaultAsync(u => u.Username == dto.RecipientUsername)
            ?? throw new InvalidOperationException($"User '{dto.RecipientUsername}' not found.");

        if (recipient.Id == userId)
            throw new InvalidOperationException("Cannot start a conversation with yourself.");

        // Return existing conversation if one already exists between these two users
        var existing = await _db.Conversations
            .Include(c => c.Participants)
                .ThenInclude(p => p.User)
            .Include(c => c.Messages)
                .ThenInclude(m => m.Sender)
            .Where(c => c.Participants.Count == 2
                     && c.Participants.Any(p => p.UserId == userId)
                     && c.Participants.Any(p => p.UserId == recipient.Id))
            .FirstOrDefaultAsync();

        if (existing is not null)
            return _mapper.Map<ConversationDto>(existing);

        var conversation = new Data.Entities.Conversation();
        _db.Conversations.Add(conversation);

        _db.ConversationParticipants.Add(new ConversationParticipant { ConversationId = conversation.Id, UserId = userId });
        _db.ConversationParticipants.Add(new ConversationParticipant { ConversationId = conversation.Id, UserId = recipient.Id });

        await _db.SaveChangesAsync();

        var created = await _db.Conversations
            .Include(c => c.Participants)
                .ThenInclude(p => p.User)
            .Include(c => c.Messages)
                .ThenInclude(m => m.Sender)
            .FirstAsync(c => c.Id == conversation.Id);

        return _mapper.Map<ConversationDto>(created);
    }
}
