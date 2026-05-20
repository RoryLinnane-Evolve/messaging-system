namespace api.Config;

public class AppConfig
{
    public string DatabaseConnection { get; init; }
    public string JwtSecret { get; init; }
    public int JwtExpiryHours { get; init; }
    public string ArgonPepper { get; init; }
    public string BlockchainRpcUrl { get; init; }
    public string BlockchainPrivateKey { get; init; }
    public string BlockchainContractAddress { get; init; }

    public AppConfig()
    {
        DatabaseConnection = Environment.GetEnvironmentVariable("DB_CONNECTION")
                             ?? throw new InvalidOperationException("DB_CONNECTION not set");

        JwtSecret = Environment.GetEnvironmentVariable("JWT_SECRET")
                    ?? throw new InvalidOperationException("JWT_SECRET not set");

        ArgonPepper = Environment.GetEnvironmentVariable("ARGON_PEPPER")
                      ?? throw new InvalidOperationException("ARGON_PEPPER not set");

        BlockchainRpcUrl = Environment.GetEnvironmentVariable("BLOCKCHAIN_RPC_URL")
                           ?? throw new InvalidOperationException("BLOCKCHAIN_RPC_URL not set");

        BlockchainPrivateKey = Environment.GetEnvironmentVariable("BLOCKCHAIN_PRIVATE_KEY")
                               ?? throw new InvalidOperationException("BLOCKCHAIN_PRIVATE_KEY not set");

        BlockchainContractAddress = Environment.GetEnvironmentVariable("BLOCKCHAIN_CONTRACT_ADDRESS")
                                    ?? throw new InvalidOperationException("BLOCKCHAIN_CONTRACT_ADDRESS not set");

        JwtExpiryHours = int.Parse(
            Environment.GetEnvironmentVariable("JWT_EXPIRY_HOURS") ?? "24"
        );
    }
}
