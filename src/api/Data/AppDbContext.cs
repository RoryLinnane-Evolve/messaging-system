using api.Data.Entities;
using Microsoft.EntityFrameworkCore;

namespace api.Data;

public class AppDbContext : DbContext
{
    public AppDbContext(DbContextOptions<AppDbContext> options) : base(options) { }

    public DbSet<User> Users { get; set; }
    public DbSet<Conversation> Conversations { get; set; }
    public DbSet<ConversationParticipant> ConversationParticipants { get; set; }
    public DbSet<Message> Messages { get; set; }
    public DbSet<ConversationDigest> ConversationDigests { get; set; }

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<ConversationParticipant>()
            .HasKey(cp => new { cp.ConversationId, cp.UserId });

        // Null out SenderId when a user is deleted — messages are preserved
        modelBuilder.Entity<Message>()
            .HasOne(m => m.Sender)
            .WithMany(u => u.SentMessages)
            .HasForeignKey(m => m.SenderId)
            .OnDelete(DeleteBehavior.SetNull);

        modelBuilder.Entity<User>()
            .HasIndex(u => u.Username)
            .IsUnique();

        modelBuilder.Entity<User>()
            .Property(u => u.Id)
            .ValueGeneratedOnAdd();

        modelBuilder.Entity<Conversation>()
            .Property(c => c.Id)
            .ValueGeneratedOnAdd();

        modelBuilder.Entity<Message>()
            .Property(m => m.Id)
            .ValueGeneratedOnAdd();

        modelBuilder.Entity<ConversationDigest>()
            .Property(d => d.Id)
            .ValueGeneratedOnAdd();

        // Prevent cascade delete conflicts from two FKs to Message
        modelBuilder.Entity<ConversationDigest>()
            .HasOne(d => d.FirstMessage)
            .WithMany()
            .HasForeignKey(d => d.FirstMessageId)
            .OnDelete(DeleteBehavior.Restrict);

        modelBuilder.Entity<ConversationDigest>()
            .HasOne(d => d.LastMessage)
            .WithMany()
            .HasForeignKey(d => d.LastMessageId)
            .OnDelete(DeleteBehavior.Restrict);
    }
}
