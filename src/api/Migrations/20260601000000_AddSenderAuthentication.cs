using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace api.Migrations
{
    /// <inheritdoc />
    public partial class AddSenderAuthentication : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            // Add Ed25519 signing public key to Users.
            // defaultValue: "" covers any rows that predate this migration.
            migrationBuilder.AddColumn<string>(
                name: "SigningPublicKey",
                table: "Users",
                type: "text",
                nullable: false,
                defaultValue: "");

            // Add optional signature field to Messages.
            // Nullable so messages sent before this feature was deployed are still readable.
            migrationBuilder.AddColumn<string>(
                name: "Signature",
                table: "Messages",
                type: "text",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "SigningPublicKey",
                table: "Users");

            migrationBuilder.DropColumn(
                name: "Signature",
                table: "Messages");
        }
    }
}
