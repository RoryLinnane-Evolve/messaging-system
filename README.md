# SecureMsg

End-to-end encrypted messaging system built for CS4455 Cybersecurity at the University of Limerick.

Messages are encrypted client-side before leaving the device. The server stores only ciphertext and never has access to plaintext. Conversation integrity is anchored to the Ethereum Sepolia testnet via keccak256 digests recorded on-chain every ten messages.

---

## Architecture

| Component | Technology |
|---|---|
| API server | C# ASP.NET Core (.NET 10), EF Core, PostgreSQL |
| Web client | Vanilla HTML/CSS/JS, libsodium-wrappers |
| C++ client | C++17, CMake, libsodium, libcurl |
| Blockchain | Solidity, Ethereum Sepolia testnet |
| Auth | JWT Bearer tokens, Argon2id password hashing |
| Deployment | Docker Compose, Ubuntu VM, Nginx reverse proxy |

### Encryption scheme

Both clients use **libsodium `crypto_box_easy`** (X25519 key exchange + XSalsa20-Poly1305 AEAD), making messages interoperable across clients.

- Each user generates an X25519 keypair on registration. The public key is stored on the server; the private key never leaves the client.
- Each message uses a fresh ephemeral sender keypair, providing forward secrecy.
- Nonces are 24 bytes generated from a CSPRNG, never reused.
- Private keys are stored encrypted at rest using PBKDF2 (200,000 iterations, SHA-256) + AES-256-GCM.

### Trust model

| Threat | Mitigation |
|---|---|
| Passive network attacker | TLS in transit |
| Active network attacker | TLS + AEAD + sender authentication |
| Honest-but-curious server | Server only stores ciphertext |
| Compromised server | Cannot read or forge messages |
| Key substitution (MITM) | TOFU — public key pinned on first fetch, mismatch aborts |

---

## Repository structure

```
messaging-system/
├── docker-compose.yml          # Development stack
├── docker-compose.prod.yml     # Production stack (pulls from GHCR)
├── .env                        # Secrets — never committed
├── resources/                  # Architecture docs, deployment guides
├── src/
│   ├── api/                    # ASP.NET Core API
│   │   ├── Config/             # Environment-based configuration
│   │   ├── Data/               # EF Core DbContext and entities
│   │   ├── Features/           # Vertical slice architecture
│   │   │   ├── Auth/
│   │   │   ├── Blockchain/
│   │   │   ├── Conversation/
│   │   │   ├── Message/
│   │   │   ├── RealTimeMessage/
│   │   │   └── User/
│   │   ├── Migrations/
│   │   └── Profiles/           # AutoMapper mappings
│   ├── contracts/              # Solidity smart contract
│   ├── cpp-client/             # C++ CLI client
│   └── web-client/             # Browser client
```

---

## Running locally

### Requirements

- Docker and Docker Compose
- For the C++ client: `libcurl`, `libsodium`, `pkg-config`, CMake 3.20+
- For the web client: any static file server (e.g. Python)

### Start the API and database

```bash
cp .env.example .env   # fill in your values
docker compose up --build
```

The API is available at `http://localhost:8080`. Migrations run automatically on startup.

### Web client

```bash
cd src/web-client
python3 -m http.server 3000
```

Open `http://localhost:3000`.

### C++ client

```bash
cd src/cpp-client
mkdir build && cd build
cmake ..
make
./securemsg http://localhost:8080
```

The client stores your keypair in `~/.securemsg_keys.bin` and TOFU pins in `~/.securemsg_tofu.json`. To run multiple users on the same machine use separate HOME directories:

```bash
HOME=/tmp/alice ./securemsg http://localhost:8080
HOME=/tmp/bob   ./securemsg http://localhost:8080
```

---

## Environment variables

All configuration is loaded from environment variables at startup. The application exits immediately if a required variable is missing.

| Variable | Required | Description |
|---|---|---|
| `DB_CONNECTION` | Yes | Npgsql connection string |
| `JWT_SECRET` | Yes | HMAC-SHA256 signing key (min. 32 bytes, base64) |
| `ARGON_PEPPER` | Yes | Server-side pepper for Argon2id (base64) |
| `JWT_EXPIRY_HOURS` | No | Token lifetime in hours (default: 24) |
| `BLOCKCHAIN_RPC_URL` | Yes | Ethereum RPC endpoint (e.g. Infura Sepolia) |
| `BLOCKCHAIN_PRIVATE_KEY` | Yes | Wallet private key for signing transactions |
| `BLOCKCHAIN_CONTRACT_ADDRESS` | Yes | Deployed MessageDigest contract address |

---

## API endpoints

All endpoints except registration and login require a `Bearer` token.

### Auth
| Method | Path | Description |
|---|---|---|
| POST | `/api/auth/sign-up` | Register with username, password, and public key |
| POST | `/api/auth/login` | Returns a JWT |
| POST | `/api/auth/logout` | Client-side token discard (returns 204) |

### Conversations
| Method | Path | Description |
|---|---|---|
| GET | `/api/conversation` | List your conversations |
| GET | `/api/conversation/{id}` | Get a conversation with messages |
| POST | `/api/conversation` | Create a conversation |

### Messages
| Method | Path | Description |
|---|---|---|
| GET | `/api/message/conversation/{id}` | Get messages in a conversation |
| POST | `/api/message` | Send a message |
| GET | `/api/message/{id}` | Get a single message |
| DELETE | `/api/message/{id}` | Delete a message |
| POST | `/api/message/{convId}/revoke/{userId}` | Remove a user from a conversation |

### Users
| Method | Path | Description |
|---|---|---|
| GET | `/api/user/{username}` | Get a user's public profile and public key |
| PUT | `/api/user/password` | Change password |
| DELETE | `/api/user` | Delete account |

### WebSocket

Connect to `/ws?token=<jwt>`. Send JSON frames:

```json
{ "type": "send_message", "data": { "conversationId": "...", "ciphertext": "...", "nonce": "...", "ephemeralPublicKey": "..." } }
```

The server pushes `{ "type": "new_message", "data": { ...MessageDto } }` to all connected participants when a message is received.

---

## Blockchain integrity

Every tenth message in a conversation triggers a background job that:

1. Fetches the ten messages from the database
2. Computes `keccak256(concat(ciphertexts))`
3. Calls `recordDigest(bytes32)` on the deployed Solidity contract
4. Stores the transaction hash in `ConversationDigests`

The contract is deployed on Ethereum Sepolia. Anyone can verify a digest by calling `getDigest(id)` on the contract and comparing it against the stored messages.

See `resources/smart-contract-deployment.md` for deployment instructions and `src/web-client/verify.html` for the standalone verification page.

---

## Production deployment

The GitHub Actions workflow (`.github/workflows/publish.yml`) builds and pushes a Docker image to GHCR on every version tag.

```bash
git tag v1.0.0
git push origin v1.0.0
```

On the production server:

```bash
docker login ghcr.io -u YOUR_GITHUB_USERNAME   # once only, use a read:packages PAT
docker compose -f docker-compose.prod.yml pull
docker compose -f docker-compose.prod.yml up -d
```

---

## Security notes

- Passwords are hashed with Argon2id (time=3, memory=65536 KiB, parallelism=4) with a random 16-byte salt and a server-side pepper.
- All hash comparisons use `CryptographicOperations.FixedTimeEquals` to prevent timing attacks.
- A dummy hash is always computed on login even when the username does not exist, preventing username enumeration via timing.
- JWT tokens are validated on every request; the WebSocket endpoint validates the token from the query string before upgrading.
- The `.env` file is listed in `.gitignore` and must never be committed.
