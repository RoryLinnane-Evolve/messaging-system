using System.Text;
using api.Config;
using api.Data;
using api.Data.Entities;
using Microsoft.EntityFrameworkCore;
using Nethereum.Hex.HexTypes;
using Nethereum.Util;
using Nethereum.Web3;
using Nethereum.Web3.Accounts;

namespace api.Features.Blockchain;

public interface IBlockchainService
{
    void ScheduleDigestIfNeeded(Guid conversationId, int messageCount);
}

public class BlockchainService : IBlockchainService
{
    private const int BatchSize = 10;

    // Minimal ABI — only the functions we call
    private const string Abi = """
        [
            {
                "inputs": [{"internalType": "bytes32", "name": "hash", "type": "bytes32"}],
                "name": "recordDigest",
                "outputs": [{"internalType": "uint256", "name": "", "type": "uint256"}],
                "stateMutability": "nonpayable",
                "type": "function"
            },
            {
                "inputs": [{"internalType": "uint256", "name": "id", "type": "uint256"}],
                "name": "getDigest",
                "outputs": [
                    {"internalType": "bytes32", "name": "hash", "type": "bytes32"},
                    {"internalType": "uint256", "name": "timestamp", "type": "uint256"}
                ],
                "stateMutability": "view",
                "type": "function"
            }
        ]
        """;

    private readonly Web3 _web3;
    private readonly string _contractAddress;
    private readonly IServiceScopeFactory _scopeFactory;

    // Singleton — Web3 is thread-safe and holds the account/RPC connection
    public BlockchainService(AppConfig config, IServiceScopeFactory scopeFactory)
    {
        var account = new Account(config.BlockchainPrivateKey);
        _web3 = new Web3(account, config.BlockchainRpcUrl);
        _contractAddress = config.BlockchainContractAddress;
        _scopeFactory = scopeFactory;
    }

    public void ScheduleDigestIfNeeded(Guid conversationId, int messageCount)
    {
        if (messageCount % BatchSize != 0)
            return;

        // Fire and forget — caller gets an immediate response
        _ = Task.Run(() => WriteDigest(conversationId, messageCount));
    }

    private async Task WriteDigest(Guid conversationId, int messageCount)
    {
        try
        {
            await using var scope = _scopeFactory.CreateAsyncScope();
            var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();

            var messages = await db.Messages
                .Where(m => m.ConversationId == conversationId)
                .OrderBy(m => m.Timestamp)
                .Skip(messageCount - BatchSize)
                .Take(BatchSize)
                .ToListAsync();

            if (messages.Count < BatchSize)
                return;

            var hash = ComputeHash(messages);
            var txHash = await SendToChain(hash);

            db.ConversationDigests.Add(new ConversationDigest
            {
                ConversationId = conversationId,
                FirstMessageId = messages.First().Id,
                LastMessageId = messages.Last().Id,
                Hash = hash,
                TransactionHash = txHash
            });

            await db.SaveChangesAsync();
        }
        catch (Exception ex)
        {
            // Log but don't crash — blockchain write failure must not affect messaging
            Console.Error.WriteLine($"[Blockchain] Failed to write digest: {ex.Message}");
        }
    }

    private static string ComputeHash(IEnumerable<Data.Entities.Message> messages)
    {
        var combined = string.Concat(messages.Select(m => m.Ciphertext));
        var bytes = Encoding.UTF8.GetBytes(combined);
        return Convert.ToHexString(new Sha3Keccack().CalculateHash(bytes)).ToLowerInvariant();
    }

    private async Task<string> SendToChain(string hexHash)
    {
        var contract = _web3.Eth.GetContract(Abi, _contractAddress);
        var recordDigest = contract.GetFunction("recordDigest");

        var hashBytes = Convert.FromHexString(hexHash.TrimStart('0', 'x').PadLeft(64, '0'));

        return await recordDigest.SendTransactionAsync(
            from: _web3.TransactionManager.Account.Address,
            gas: new HexBigInteger(200000),
            value: new HexBigInteger(0),
            functionInput: hashBytes
        );
    }
}
