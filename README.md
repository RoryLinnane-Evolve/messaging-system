# SecureMsg

**CS4455 EPIC Project 2026 — University of Limerick**
Óran Fleming and Rory Linnane

---

SecureMsg is an end-to-end encrypted messaging system. Messages are encrypted client-side before leaving the device — the server stores only ciphertext and never has access to plaintext. The system consists of a C# ASP.NET Core API, a React web client, and a C++ CLI client.

The web client is hosted at **https://teamwfh.theburkenator.com/** — no local setup required to use it.

---

## Dependencies

### API and web client (local development only)
- [Docker](https://docs.docker.com/get-docker/) and Docker Compose
- Node.js 18+

### C++ client
- CMake 3.20+
- A C++17-compatible compiler (GCC 9+ or Clang 10+)
- libsodium, libcurl, OpenSSL, pkg-config

On Ubuntu/Debian:
```bash
sudo apt update
sudo apt install cmake build-essential libsodium-dev libcurl4-openssl-dev libssl-dev pkg-config
```

On macOS (Homebrew):
```bash
brew install cmake libsodium curl openssl pkg-config
```

---

## Setup (local development only)

Copy the environment variable template and fill in your values:

```bash
cp .env.example .env
```

Then start the API and database:

```bash
docker compose up --build
```

Run the web client locally:

```bash
cd src/web-client
npm install
npm run dev
```

---

## C++ Client

### Building

```bash
cd src/cpp-client
mkdir build && cd build
cmake ..
make
```

CMake will automatically fetch `nlohmann/json` and `IXWebSocket` during the first build — an internet connection is required.

### Running

To connect to the hosted server:

```bash
./securemsg https://teamwfh.theburkenator.com
```

### Running multiple users on the same machine

```bash
HOME=/tmp/alice ./securemsg https://teamwfh.theburkenator.com
HOME=/tmp/bob   ./securemsg https://teamwfh.theburkenator.com
```
