using api.Data;
using api.Data.Entities;
using api.Features.Blockchain;
using api.Features.RealTimeMessage;
using AutoMapper;
using Microsoft.EntityFrameworkCore;

namespace api.Features.Message;

public interface IMessageService
{
    Task SendMessage(Guid senderId, SendMessageDto dto);
    Task<IEnumerable<MessageDto>> GetMessages(Guid conversationId, Guid userId);
    Task<MessageDto?> GetMessage(Guid messageId, Guid userId);
    Task<bool> DeleteMessage(Guid messageId, Guid userId);
    Task<bool> RevokeAccess(Guid conversationId, Guid targetUserId, Guid requestingUserId);
}

public class MessageService : IMessageService
{
    private readonly AppDbContext _db;
    private readonly IMapper _mapper;
    private readonly IBlockchainService _blockchain;
    private readonly ConnectionManager _connections;

    public MessageService(AppDbContext db, IMapper mapper, IBlockchainService blockchain, ConnectionManager connections)
    {
        _db = db;
        _mapper = mapper;
        _blockchain = blockchain;
        _connections = connections;
    }

    public async Task SendMessage(Guid senderId, SendMessageDto dto)
    {
        var participantIds = await _db.ConversationParticipants
            .Where(cp => cp.ConversationId == dto.ConversationId)
            .Select(cp => cp.UserId)
            .ToListAsync();

        if (!participantIds.Contains(senderId))
            throw new UnauthorizedAccessException("User is not a participant in this conversation.");

        var message = new Data.Entities.Message
        {
            ConversationId = dto.ConversationId,
            SenderId = senderId,
            Ciphertext = dto.Ciphertext,
            Nonce = dto.Nonce,
            EphemeralPublicKey = dto.EphemeralPublicKey
        };

        _db.Messages.Add(message);
        await _db.SaveChangesAsync();

        // Eagerly load sender so the DTO has a username
        await _db.Entry(message).Reference(m => m.Sender).LoadAsync();
        var messageDto = _mapper.Map<MessageDto>(message);

        // Push to all connected participants in real time
        foreach (var userId in participantIds)
            await _connections.SendToUser(userId, new { type = "new_message", data = messageDto });

        var messageCount = await _db.Messages
            .CountAsync(m => m.ConversationId == dto.ConversationId);

        _blockchain.ScheduleDigestIfNeeded(dto.ConversationId, messageCount);
    }

    public async Task<IEnumerable<MessageDto>> GetMessages(Guid conversationId, Guid userId)
    {
        var isParticipant = await _db.ConversationParticipants
            .AnyAsync(cp => cp.ConversationId == conversationId && cp.UserId == userId);

        if (!isParticipant)
            return [];

        var messages = await _db.Messages
            .Include(m => m.Sender)
            .Where(m => m.ConversationId == conversationId)
            .OrderBy(m => m.Timestamp)
            .ToListAsync();

        return _mapper.Map<IEnumerable<MessageDto>>(messages);
    }

    public async Task<MessageDto?> GetMessage(Guid messageId, Guid userId)
    {
        var message = await _db.Messages
            .Include(m => m.Sender)
            .Include(m => m.Conversation)
                .ThenInclude(c => c.Participants)
            .FirstOrDefaultAsync(m => m.Id == messageId);

        if (message == null)
            return null;

        var isParticipant = message.Conversation.Participants.Any(p => p.UserId == userId);
        if (!isParticipant)
            return null;

        return _mapper.Map<MessageDto>(message);
    }

    public async Task<bool> DeleteMessage(Guid messageId, Guid userId)
    {
        var message = await _db.Messages.FirstOrDefaultAsync(m => m.Id == messageId);

        if (message == null || message.SenderId != userId)
            return false;

        _db.Messages.Remove(message);
        await _db.SaveChangesAsync();
        return true;
    }

    public async Task<bool> RevokeAccess(Guid conversationId, Guid targetUserId, Guid requestingUserId)
    {
        var requestingParticipant = await _db.ConversationParticipants
            .FirstOrDefaultAsync(cp => cp.ConversationId == conversationId && cp.UserId == requestingUserId);

        if (requestingParticipant == null)
            return false;

        var targetParticipant = await _db.ConversationParticipants
            .FirstOrDefaultAsync(cp => cp.ConversationId == conversationId && cp.UserId == targetUserId);

        if (targetParticipant == null)
            return false;

        _db.ConversationParticipants.Remove(targetParticipant);
        await _db.SaveChangesAsync();
        return true;
    }
}
