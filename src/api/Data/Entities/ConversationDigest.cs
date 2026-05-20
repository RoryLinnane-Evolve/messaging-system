namespace api.Data.Entities;

public class ConversationDigest
{
    public Guid Id { get; set; } = Guid.NewGuid();

    public Guid ConversationId { get; set; }

    // Inclusive batch boundary — which messages are covered by this digest
    public Guid FirstMessageId { get; set; }
    public Guid LastMessageId { get; set; }

    // keccak256 of the batch — what was written to the chain
    public required string Hash { get; set; }

    // Ethereum transaction hash — used to fetch and verify the on-chain record
    public required string TransactionHash { get; set; }

    public DateTime RecordedAt { get; set; } = DateTime.UtcNow;

    // Navigation properties
    public Conversation Conversation { get; set; } = null!;
    public Message FirstMessage { get; set; } = null!;
    public Message LastMessage { get; set; } = null!;
}
