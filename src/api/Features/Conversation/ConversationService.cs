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
        var otherUsers = await _db.Users
            .Where(u => dto.ParticipantUsernames.Contains(u.Username))
            .ToListAsync();

        var creator = await _db.Users.FindAsync(userId);

        var allParticipants = otherUsers
            .Append(creator!)
            .DistinctBy(u => u.Id)
            .ToList();

        var conversation = new Data.Entities.Conversation();
        _db.Conversations.Add(conversation);

        foreach (var user in allParticipants)
        {
            _db.ConversationParticipants.Add(new ConversationParticipant
            {
                ConversationId = conversation.Id,
                UserId = user.Id
            });
        }

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
