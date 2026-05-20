// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// @title MessageDigest
/// @notice Records keccak256 hashes of conversation segments for tamper-evident integrity verification.
///         Deployed on Ethereum Sepolia testnet as part of CS4455 Cybersecurity project.
contract MessageDigest {

    struct Digest {
        bytes32 hash;
        uint256 timestamp;
    }

    mapping(uint256 => Digest) private digests;
    uint256 public digestCount;

    address public owner;

    event DigestRecorded(
        uint256 indexed id,
        bytes32 indexed hash,
        uint256 timestamp
    );

    modifier onlyOwner() {
        require(msg.sender == owner, "Not authorised");
        _;
    }

    constructor() {
        owner = msg.sender;
    }

    /// @notice Record a conversation segment digest on-chain.
    /// @param hash keccak256 hash of the concatenated message ciphertexts in the segment.
    /// @return id The index of the stored digest, used for later retrieval.
    function recordDigest(bytes32 hash) external onlyOwner returns (uint256) {
        uint256 id = digestCount;
        digests[id] = Digest(hash, block.timestamp);
        digestCount++;
        emit DigestRecorded(id, hash, block.timestamp);
        return id;
    }

    /// @notice Retrieve a stored digest by its id.
    /// @param id The digest index returned by recordDigest.
    /// @return hash The stored keccak256 hash.
    /// @return timestamp The block timestamp when the digest was recorded.
    function getDigest(uint256 id) external view returns (bytes32 hash, uint256 timestamp) {
        require(id < digestCount, "Digest does not exist");
        Digest memory d = digests[id];
        return (d.hash, d.timestamp);
    }
}
